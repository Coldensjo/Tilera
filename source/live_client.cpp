//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "live_client.h"
#include "live_tab.h"
#include "live_action.h"
#include "live_assets.h"
#include "live_item_blocklist.h"
#include "editor.h"
#include "gui.h"
#include "map_view_state.h"
#include "common_windows.h"
#include "map_tab.h"
#include "map_region.h"
#include "settings.h"
#include "items.h"
#include "brush.h"

#include <wx/event.h>
#include <wx/timer.h>

namespace {
	// How often to send a keepalive packet while otherwise idle. Well under the
	// idle-connection timeouts commonly enforced by NAT gateways and firewalls
	// (some as low as 60s), and far shorter than the OS's default multi-hour
	// TCP keepalive probe interval.
	constexpr int LIVE_KEEPALIVE_INTERVAL_MS = 25000;

	// GUI-thread timer that drives node-request retries independently of repaints.
	class NodeRetryTimer : public wxTimer {
	public:
		explicit NodeRetryTimer(LiveClient* owner) :
			client(owner) { }
		void Notify() override {
			client->onNodeRetryTick();
		}

	private:
		LiveClient* client;
	};

	// GUI-thread timer that keeps the connection carrying traffic while the user
	// is idle or the editor window is tabbed out, independent of mouse movement.
	class KeepAliveTimer : public wxTimer {
	public:
		explicit KeepAliveTimer(LiveClient* owner) :
			client(owner) { }
		void Notify() override {
			client->sendKeepAlive();
		}

	private:
		LiveClient* client;
	};
}

LiveClient::LiveClient() :
	LiveSocket(),
	readMessage(), queryNodeList(), pendingNodeRequests(), viewportRefreshPending(false),
	hasPendingCommentList(false), pendingCommentList(),
	currentOperation(),
	resolver(nullptr), socket(nullptr), editor(nullptr), stopped(false),
	ownClientColor(
		g_settings.getInteger(Config::LIVE_CURSOR_RED),
		g_settings.getInteger(Config::LIVE_CURSOR_GREEN),
		g_settings.getInteger(Config::LIVE_CURSOR_BLUE),
		g_settings.getInteger(Config::LIVE_CURSOR_ALPHA)
	), pendingVersionId(CLIENT_VERSION_NONE), pendingAssetManifest(),
	assetReceiveState(), ignoreIncomingAssets(false), waitingForServerAssets(false),
	assetBytesExpected(0), assetBytesReceived(0), assetProgressReported(0),
	updateReceiveState(), updateBytesExpected(0), updateBytesReceived(0),
	updateProgressReported(0), receivingUpdate(false),
	connectionAddress(), connectionPort(0) {
}

LiveClient::~LiveClient() {
	//
}

bool LiveClient::connect(const std::string& address, uint16_t port) {
	connectionAddress = address;
	connectionPort = port;

	NetworkConnection& connection = NetworkConnection::getInstance();
	if (!connection.start()) {
		setLastError("The previous connection has not been terminated yet.");
		return false;
	}

	stopped = false;

	auto& service = connection.get_service();
	if (!resolver) {
		resolver = std::make_shared<asio::ip::tcp::resolver>(service);
	} else {
		resolver->cancel();
	}

	if (socket) {
		net_error_code closeError;
		socket->close(closeError);
	}
	socket = std::make_shared<asio::ip::tcp::socket>(service);

	resolver->async_resolve(address, std::to_string(port), [this](const net_error_code& error, asio::ip::tcp::resolver::results_type results) -> void {
		if (error) {
			logMessage("Error: " + error.message());
		} else {
			tryConnect(std::move(results));
		}
	});

	return true;
}

void LiveClient::tryConnect(asio::ip::tcp::resolver::results_type endpoints) {
	if (stopped) {
		return;
	}

	if (endpoints.empty()) {
		logMessage("Could not resolve host.");
		return;
	}

	logMessage("Joining server...");

	asio::async_connect(*socket, endpoints, [this](const net_error_code& error, const asio::ip::tcp::endpoint&) -> void {
		if (error) {
			if (handleError(error)) {
				return;
			}
			disconnectFromServer("Connection failed: " + error.message());
		} else {
			net_error_code optionError;
			socket->set_option(asio::ip::tcp::no_delay(true), optionError);
			// Match the server's keep_alive(true) so the OS also probes this side of
			// an idle connection (see live_server.cpp's acceptClient for the fuller
			// rationale). This is a backstop -- the app-level keepalive packet below
			// is what actually keeps NAT/firewall mappings alive on typical timeouts.
			socket->set_option(asio::socket_base::keep_alive(true), optionError);
			if (optionError) {
				disconnectFromServer("Failed to set socket options: " + optionError.message());
				return;
			}
			logMessage("TCP connected to " + wxstr(getHostName()) + ", starting handshake...");
			sendHello();
		}
	});
}

void LiveClient::close() {
	if (nodeRetryTimer) {
		nodeRetryTimer->Stop();
	}

	if (keepAliveTimer) {
		keepAliveTimer->Stop();
	}

	if (resolver) {
		resolver->cancel();
	}

	if (socket) {
		socket->close();
	}

	if (log) {
		log->Message("Disconnected from server.");
		log->Disconnect();
		log = nullptr;
	}

	stopped = true;
}

void LiveClient::closeAndTeardown(std::function<void()> onTornDown) {
	if (teardownInitiated) {
		// Already tearing down -- e.g. a network error raced with the user
		// closing the tab. Scheduling a second CloseLiveEditors here would
		// delete this client twice.
		return;
	}
	teardownInitiated = true;

	close();

	// close() cancels the outstanding read/write and its completion handler
	// (which touches `this`) gets queued on the network thread with
	// operation_aborted. Post a no-op there so it runs after that completion
	// has drained, then hop back to the GUI thread to actually delete `this`.
	// Deleting synchronously here would race the still-in-flight handler.
	NetworkConnection::getInstance().post([this, onTornDown = std::move(onTornDown)]() mutable {
		wxTheApp->CallAfter([this, onTornDown = std::move(onTornDown)]() {
			g_gui.CloseLiveEditors(this); // deletes `this`
			if (onTornDown) {
				onTornDown();
			}
		});
	});
}

bool LiveClient::handleError(const net_error_code& error) {
	if (error == asio::error::eof || error == asio::error::connection_reset) {
		disconnectFromServer(wxString() + getHostName() + ": disconnected.");
		return true;
	} else if (error == asio::error::connection_aborted) {
		logMessage("You have left the server.");
		return true;
	}
	return false;
}

void LiveClient::disconnectFromServer(const wxString& reason) {
	if (!reason.empty()) {
		logMessage(reason);
	}
	// Idempotent: the read loop keeps running until close() cancels the socket, so
	// more than one failure can race in. Mark teardown started right away so a
	// second call (or a packet already queued on the GUI thread) doesn't schedule
	// a second CloseLiveEditors and double-delete this client.
	if (stopped) {
		return;
	}
	stopped = true;
	// The read/write callbacks run on the network thread, and CloseLiveEditors
	// deletes this LiveClient, so the teardown must happen on the GUI thread.
	wxTheApp->CallAfter([this]() {
		closeAndTeardown();
	});
}


std::string LiveClient::getHostName() const {
	if (!socket) {
		return "not connected";
	}
	net_error_code error;
	auto endpoint = socket->remote_endpoint(error);
	if (error) {
		return "not connected";
	}
	return endpoint.address().to_string();
}

void LiveClient::receiveHeader() {
	readMessage.clear();
	readMessage.position = 0;
	asio::async_read(*socket,
		asio::buffer(readMessage.buffer, 4),
		[this](const net_error_code& error, size_t bytesReceived) -> void {
			if (error) {
				if (!handleError(error)) {
					disconnectFromServer(wxString() + getHostName() + ": " + error.message());
				}
			} else if (bytesReceived < 4) {
				disconnectFromServer(wxString() + getHostName() + ": Could not receive header[size: " + std::to_string(bytesReceived) + "], disconnecting client.");
			} else {
				receive(readMessage.read<uint32_t>());
			}
		});
}

void LiveClient::receive(uint32_t packetSize) {
	readMessage.buffer.resize(readMessage.position + packetSize);
	asio::async_read(*socket,
		asio::buffer(&readMessage.buffer[readMessage.position], packetSize),
		[this](const net_error_code& error, size_t bytesReceived) -> void {
			if (error) {
				if (!handleError(error)) {
					disconnectFromServer(wxString() + getHostName() + ": " + error.message());
				}
			} else if (bytesReceived < readMessage.buffer.size() - 4) {
				disconnectFromServer(wxString() + getHostName() + ": Could not receive packet[size: " + std::to_string(bytesReceived) + "], disconnecting client.");
			} else {
				NetworkMessage packet = std::move(readMessage);
				wxTheApp->CallAfter([this, packet = std::move(packet)]() mutable {
					parsePacket(std::move(packet));
				});
				NetworkConnection::getInstance().dispatch([this]() {
					receiveHeader();
				});
			}
		});
}

void LiveClient::send(NetworkMessage& message) {
	send(message, nullptr);
}

void LiveClient::send(NetworkMessage& message, std::function<void()> onSent) {
	auto packet = std::make_shared<NetworkMessage>();
	packet->buffer = message.buffer;
	packet->position = message.position;
	packet->size = message.size;

	NetworkConnection::getInstance().dispatch([this, packet, onSent = std::move(onSent)]() mutable {
		memcpy(&packet->buffer[0], &packet->size, 4);
		writeQueue.push_back(OutboundPacket { std::move(packet), std::move(onSent) });
		// Serialize writes: overlapping async_writes on one socket interleave bytes
		// and corrupt the stream. The completion handler drains the queue in order.
		if (!writing) {
			writing = true;
			doWrite();
		}
	});
}

void LiveClient::doWrite() {
	// Runs on the network thread with writing == true and a non-empty queue.
	const OutboundPacket& front = writeQueue.front();
	const size_t packetLength = front.message->size + 4;
	asio::async_write(*socket,
		asio::buffer(front.message->buffer.data(), packetLength),
		[this](const net_error_code& error, size_t bytesTransferred) -> void {
			OutboundPacket completed = std::move(writeQueue.front());
			writeQueue.pop_front();

			if (error) {
				writing = false;
				writeQueue.clear();
				wxTheApp->CallAfter([this, error]() {
					logMessage(wxString() + getHostName() + ": " + error.message());
				});
				return;
			}

			if (completed.onSent) {
				completed.onSent();
			}

			if (!writeQueue.empty()) {
				doWrite();
			} else {
				writing = false;
			}
		});
}

void LiveClient::updateCursor(const Position& position) {
	if (!shouldSendCursorUpdate(position)) {
		return;
	}

	LiveCursor cursor;
	cursor.id = ownClientId;
	cursor.pos = position;
	cursor.color = ownClientColor;

	NetworkMessage message;
	message.write<uint8_t>(PACKET_CLIENT_UPDATE_CURSOR);
	writeCursor(message, cursor);

	send(message);
}

void LiveClient::setCursorColor(const wxColor& color) {
	ownClientColor = color;
	for (LiveParticipant& participant : participants) {
		if (participant.id == ownClientId) {
			participant.color = color;
			break;
		}
	}
}

void LiveClient::sendCursorColor() {
	NetworkMessage message;
	message.write<uint8_t>(PACKET_CLIENT_UPDATE_COLOR);
	message.write<uint8_t>(ownClientColor.Red());
	message.write<uint8_t>(ownClientColor.Green());
	message.write<uint8_t>(ownClientColor.Blue());
	message.write<uint8_t>(ownClientColor.Alpha());
	send(message);
}

LiveLogTab* LiveClient::createLogWindow(wxWindow* parent) {
	MapTabbook* mtb = dynamic_cast<MapTabbook*>(parent);
	ASSERT(mtb);

	LiveLogTab* tab = newd LiveLogTab(mtb, this);
	tab->Message("New Live mapping session started.");

	return tab;
}

MapTab* LiveClient::createEditorWindow() {
	MapTabbook* mtb = dynamic_cast<MapTabbook*>(g_gui.tabbook);
	ASSERT(mtb);

	MapTab* edit = newd MapTab(mtb, editor);
	edit->OnSwitchEditorMode(g_gui.IsSelectionMode() ? SELECTION_MODE : DRAWING_MODE);

	return edit;
}

void LiveClient::sendHello() {
	NetworkMessage message;
	message.write<uint8_t>(PACKET_HELLO_FROM_CLIENT);
	message.write<uint32_t>(__RME_VERSION_ID__);
	message.write<uint32_t>(__LIVE_NET_VERSION__);
	message.write<uint32_t>(g_gui.GetCurrentVersionID());
	message.write<std::string>(nstr(name));
	message.write<std::string>(nstr(password));
	message.write<uint8_t>(ownClientColor.Red());
	message.write<uint8_t>(ownClientColor.Green());
	message.write<uint8_t>(ownClientColor.Blue());
	message.write<uint8_t>(ownClientColor.Alpha());

	send(message, [this]() {
		logMessage("Hello sent, waiting for server response...");
		NetworkConnection::getInstance().post([this]() {
			receiveHeader();
		});
	});
}

void LiveClient::sendNodeRequests() {
	tickNodeRequestsIfDue();

	if (queryNodeList.empty()) {
		return;
	}

	NetworkMessage message;
	message.write<uint8_t>(PACKET_REQUEST_NODES);

	message.write<uint32_t>(queryNodeList.size());
	for (uint32_t node : queryNodeList) {
		message.write<uint32_t>(node);
	}

	send(message);
	queryNodeList.clear();
}

void LiveClient::sendChanges(DirtyList& dirtyList) {
	ChangeList& changeList = dirtyList.GetChanges();
	if (changeList.empty()) {
		return;
	}

	mapWriter.reset();
	const LiveSessionBounds& bounds = getSessionBounds();
	for (Change* change : changeList) {
		switch (change->getType()) {
			case CHANGE_TILE: {
				const Position& position = static_cast<Tile*>(change->getData())->getPosition();
				if (bounds.enabled && !bounds.contains(position.x, position.y)) {
					break;
				}
				sendTile(mapWriter, editor->getMap().getTile(position), &position);
				break;
			}
			default:
				break;
		}
	}
	mapWriter.endNode();

	if (mapWriter.getSize() == 0) {
		return;
	}

	NetworkMessage message;
	message.write<uint8_t>(PACKET_CHANGE_LIST);

	std::string data(reinterpret_cast<const char*>(mapWriter.getMemory()), mapWriter.getSize());
	message.write<std::string>(data);

	send(message);
}

void LiveClient::sendCommentAdd(const Position& pos, const std::string& text) {
	if (text.empty()) {
		return;
	}

	NetworkMessage message;
	message.write<uint8_t>(PACKET_COMMENT_ADD);
	message.write<uint16_t>(static_cast<uint16_t>(pos.x));
	message.write<uint16_t>(static_cast<uint16_t>(pos.y));
	message.write<uint8_t>(static_cast<uint8_t>(pos.z));
	message.write<std::string>(text);
	send(message);
}

void LiveClient::sendCommentRemove(uint32_t commentId) {
	NetworkMessage message;
	message.write<uint8_t>(PACKET_COMMENT_REMOVE);
	message.write<uint32_t>(commentId);
	send(message);
}

void LiveClient::sendCommentEdit(uint32_t commentId, const std::string& text) {
	if (text.empty()) {
		return;
	}

	NetworkMessage message;
	message.write<uint8_t>(PACKET_COMMENT_EDIT);
	message.write<uint32_t>(commentId);
	message.write<std::string>(text);
	send(message);
}

void LiveClient::sendPing(const Position& pos) {
	NetworkMessage message;
	message.write<uint8_t>(PACKET_CLIENT_PING);
	message.write<uint16_t>(static_cast<uint16_t>(pos.x));
	message.write<uint16_t>(static_cast<uint16_t>(pos.y));
	message.write<uint8_t>(static_cast<uint8_t>(pos.z));
	send(message);
}

void LiveClient::sendKeepAlive() {
	NetworkMessage message;
	message.write<uint8_t>(PACKET_CLIENT_KEEPALIVE);
	send(message);
}

void LiveClient::sendReady() {
	NetworkMessage message;
	message.write<uint8_t>(PACKET_READY_CLIENT);
	send(message);
}

void LiveClient::queryNode(int32_t ndx, int32_t ndy, bool underground) {
	uint32_t nd = 0;
	nd |= ((ndx >> 2) << 18);
	nd |= ((ndy >> 2) << 4);
	nd |= (underground ? 1 : 0);

	// Don't re-queue a node we're already waiting on. The map drawer can't mark a
	// not-yet-created leaf as "requested", so it calls queryNode every frame until
	// the response lands; without this guard each frame re-sends the whole batch and
	// the server re-sends every node's full data, saturating the connection. The
	// retry timer (tickNodeRequests) re-asks if a node really was dropped.
	if (pendingNodeRequests.find(nd) != pendingNodeRequests.end()) {
		return;
	}

	queryNodeList.insert(nd);
	pendingNodeRequests[nd] = std::chrono::steady_clock::now();
}

void LiveClient::tickNodeRequestsIfDue() {
	if (pendingNodeRequests.empty()) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	constexpr auto retryInterval = std::chrono::milliseconds(500);
	if (now - lastNodeRequestTick < retryInterval) {
		return;
	}

	lastNodeRequestTick = now;
	tickNodeRequests();
}

void LiveClient::startNodeRetryTimer() {
	// Runs on the GUI thread (called from parseHello). The timer fires Notify()
	// from the wx event loop, so node retries keep flowing even when nothing is
	// repainting the canvas.
	if (!nodeRetryTimer) {
		nodeRetryTimer = std::make_unique<NodeRetryTimer>(this);
	}
	if (!nodeRetryTimer->IsRunning()) {
		nodeRetryTimer->Start(500);
	}
}

void LiveClient::startKeepAliveTimer() {
	// Runs on the GUI thread, same as the node-retry timer above, so it keeps
	// firing whether or not the map canvas is repainting or receiving mouse
	// input -- including while the editor window is tabbed out.
	if (!keepAliveTimer) {
		keepAliveTimer = std::make_unique<KeepAliveTimer>(this);
	}
	if (!keepAliveTimer->IsRunning()) {
		keepAliveTimer->Start(LIVE_KEEPALIVE_INTERVAL_MS);
	}
}

void LiveClient::onNodeRetryTick() {
	if (stopped || !editor) {
		return;
	}
	if (pendingNodeRequests.empty()) {
		return;
	}
	// sendNodeRequests() runs the due-retry sweep and flushes any re-queued nodes.
	sendNodeRequests();
	g_gui.RefreshView();
}

void LiveClient::tickNodeRequests() {
	if (!editor) {
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	constexpr auto retryAfter = std::chrono::seconds(5);

	for (auto it = pendingNodeRequests.begin(); it != pendingNodeRequests.end();) {
		if (now - it->second < retryAfter) {
			++it;
			continue;
		}

		const uint32_t nd = it->first;
		const bool underground = nd & 1;
		const int32_t ndy = (nd >> 4) & 0x3FFF;
		const int32_t ndx = nd >> 18;

		QTreeNode* node = editor->getMap().getLeaf(ndx * 4, ndy * 4);
		if (node && node->isVisible(underground)) {
			it = pendingNodeRequests.erase(it);
			continue;
		}

		if (node) {
			node->setRequested(underground, false);
		}

		queryNodeList.insert(nd);
		it->second = now;
		++it;
	}
}

void LiveClient::requestViewportRefresh() {
	viewportRefreshPending = true;
}

bool LiveClient::consumeViewportRefresh() {
	if (!viewportRefreshPending) {
		return false;
	}
	viewportRefreshPending = false;
	return true;
}

void LiveClient::invalidateViewport(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y) {
	if (!editor) {
		return;
	}

	const int nd_start_x = start_x & ~3;
	const int nd_start_y = start_y & ~3;
	const int nd_end_x = (end_x & ~3) + 4;
	const int nd_end_y = (end_y & ~3) + 4;

	for (int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
		for (int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
			QTreeNode* node = editor->getMap().getLeaf(nd_map_x, nd_map_y);
			if (node) {
				node->setVisible(false, false);
				node->setVisible(true, false);
				node->setRequested(false, false);
				node->setRequested(true, false);
			}
			queryNode(nd_map_x, nd_map_y, false);
			queryNode(nd_map_x, nd_map_y, true);
		}
	}
}

void LiveClient::parsePacket(NetworkMessage message) {
	// Teardown may have started after this packet was queued on the GUI thread;
	// don't touch editor state (or schedule another teardown) once it has.
	if (stopped) {
		return;
	}

	uint8_t packetType;
	while (message.position < message.buffer.size()) {
		packetType = message.read<uint8_t>();

		switch (packetType) {
			case PACKET_HELLO_FROM_SERVER:
				parseHello(message);
				break;
			case PACKET_KICK:
				parseKick(message);
				break;
			case PACKET_ACCEPTED_CLIENT:
				parseClientAccepted(message);
				break;
			case PACKET_CHANGE_CLIENT_VERSION:
				parseChangeClientVersion(message);
				break;
			case PACKET_ASSET_FILE_BEGIN:
				parseAssetFileBegin(message);
				break;
			case PACKET_ASSET_FILE_CHUNK:
				parseAssetFileChunk(message);
				break;
			case PACKET_ASSET_FILE_END:
				parseAssetFileEnd(message);
				break;
			case PACKET_ASSET_FILES_DONE:
				parseAssetFilesDone(message);
				break;
			case PACKET_UPDATE_AVAILABLE:
				parseUpdateAvailable(message);
				break;
			case PACKET_UPDATE_FILE_BEGIN:
				parseUpdateFileBegin(message);
				break;
			case PACKET_UPDATE_FILE_CHUNK:
				parseUpdateFileChunk(message);
				break;
			case PACKET_UPDATE_FILE_END:
				parseUpdateFileEnd(message);
				break;
			case PACKET_UPDATE_DONE:
				parseUpdateDone(message);
				break;
			case PACKET_NODE:
				parseNode(message);
				break;
			case PACKET_CURSOR_UPDATE:
				parseCursorUpdate(message);
				break;
			case PACKET_PING:
				parsePing(message);
				break;
			case PACKET_START_OPERATION:
				parseStartOperation(message);
				break;
			case PACKET_UPDATE_OPERATION:
				parseUpdateOperation(message);
				break;
			case PACKET_CLIENT_LIST:
				parseClientList(message);
				break;
			case PACKET_COMMENT_LIST:
				parseCommentList(message);
				break;
			case PACKET_COMMENT:
				parseComment(message);
				break;
			case PACKET_COMMENT_REMOVED:
				parseCommentRemoved(message);
				break;
			case PACKET_ITEM_BLOCK_LIST:
				parseItemBlockList(message);
				break;
			default: {
				// A bad packet type means this buffer's contents desynced - the rest
				// is garbage. Stop parsing THIS buffer but keep the connection: packet
				// framing is length-prefixed and read independently, so the next packet
				// is still intact, and the node-retry timer re-fetches anything dropped.
				// (Tearing the session down here is what made connects "load then close"
				// for any data-dependent desync, plus it hit the close-during-render path.)
				if (!warnedMalformedStream) {
					warnedMalformedStream = true;
					logMessage(wxString::Format("Unknown packet received (type 0x%02X); skipping the rest of this packet.", packetType));
				}
				return;
			}
		}

		// A handler started a (deferred) teardown; stop parsing the rest of this
		// buffer. 'this' is still valid because teardown is deferred to CallAfter.
		if (stopped) {
			return;
		}

		// A read ran past the end of the buffer (truncated/oversized field). Stop
		// parsing this buffer rather than reading garbage; stay connected so the
		// retry timer can re-request whatever nodes were dropped.
		if (message.overflow) {
			if (!warnedMalformedStream) {
				warnedMalformedStream = true;
				logMessage(wxString() + getHostName() + ": Truncated packet field; skipping the rest of this packet.");
			}
			return;
		}
	}
}

void LiveClient::parseHello(NetworkMessage& message) {
	ASSERT(editor == nullptr);
	editor = newd Editor(g_gui.copybuffer, this);

	Map& map = editor->getMap();
	map.setName("Live Map - " + message.read<std::string>());
	map.setWidth(message.read<uint16_t>());
	map.setHeight(message.read<uint16_t>());

	ownClientId = message.read<uint32_t>();
	const uint8_t r = message.read<uint8_t>();
	const uint8_t g = message.read<uint8_t>();
	const uint8_t b = message.read<uint8_t>();
	const uint8_t a = message.read<uint8_t>();
	ownClientColor = wxColor(r, g, b, a);

	readSessionBounds(message);

	mapVersion = map.getVersion();

	MapTab* tab = createEditorWindow();

	const std::string viewKey = makeLiveMapViewKey(connectionAddress, connectionPort);
	Position savedPosition;
	const bool hasSavedPosition = loadMapViewPosition(viewKey, savedPosition);

	g_gui.FitViewToMap(tab, !hasSavedPosition && !sessionBounds.enabled);

	if (hasSavedPosition) {
		g_gui.RestoreMapTabViewPosition(tab, savedPosition);
	} else if (sessionBounds.enabled) {
		tab->SetScreenCenterPosition(Position(sessionBounds.centerX, sessionBounds.centerY, sessionBounds.centerZ));
	}

	if (sessionBounds.enabled) {
		logMessage(wxString::Format(
			"Session area: center (%u,%u,%u) radius %u",
			sessionBounds.centerX,
			sessionBounds.centerY,
			sessionBounds.centerZ,
			sessionBounds.radius
		));
	}

	if (hasPendingCommentList) {
		editor->getMap().getComments().setAll(std::move(pendingCommentList), 1);
		pendingCommentList.clear();
		hasPendingCommentList = false;
		g_gui.RefreshView();
	}

	startNodeRetryTimer();
	startKeepAliveTimer();

	// The new tab isn't guaranteed to fire the notebook's page-changed event (it
	// won't if this is the very first tab), which is the only other place that
	// refreshes the palettes. Without this, live-only palette buttons (e.g.
	// Comments) can stay hidden after joining until the user switches tabs.
	g_gui.RefreshPalettes(&editor->getMap());

	logMessage("Connected to live map.");
	g_gui.UpdateMenubar();
}

void LiveClient::parseKick(NetworkMessage& message) {
	const std::string kickMessage = message.read<std::string>();

	// Defer teardown: this runs inside the parsePacket loop, and CloseLiveEditors
	// deletes this client. Setting stopped first makes it idempotent and stops the
	// loop from touching freed state afterwards.
	if (stopped) {
		return;
	}
	stopped = true;
	wxString reason = wxstr(kickMessage);
	// Only servers older than the warn-instead-of-kick handshake still send this.
	if (kickMessage == "Wrong editor version.") {
		reason << "\n\nThis server runs an older Tilera build that only accepts clients "
				  "of exactly its own version (this editor is " + __W_RME_VERSION__ + ").";
	}

	wxTheApp->CallAfter([this, reason]() {
		g_gui.PopupDialog("Disconnected", reason, wxOK);
		closeAndTeardown();
	});
}

void LiveClient::parseClientAccepted(NetworkMessage& message) {
	logMessage("Server accepted connection, requesting map...");
	sendReady();
}

void LiveClient::parseChangeClientVersion(NetworkMessage& message) {
	const uint32_t serverRmeVersion = message.read<uint32_t>();
	if (serverRmeVersion != __RME_VERSION_ID__) {
		const wxString serverVersion = formatEditorVersionId(serverRmeVersion);
		logMessage("This server is running Tilera " + serverVersion + "; your editor is " + __W_RME_VERSION__ + ".");
		logMessage("Version mismatch: things may not work correctly. Save a backup before making large changes.");
		// Deferred so the modal opens after this parsePacket call has finished
		// updating the handshake state below; the asset download continues while
		// the dialog is up.
		wxTheApp->CallAfter([this, serverVersion]() {
			if (stopped) {
				return;
			}
			g_gui.PopupDialog(
				"Server Version Mismatch",
				"This server is running Tilera " + serverVersion + ", but your editor is "
					+ __W_RME_VERSION__ + ".\n\n"
					  "You can still work on the map, but the two builds may not agree on "
					  "everything -- items, brushes, or live edits may not work correctly.",
				wxOK | wxICON_EXCLAMATION
			);
		});
	}

	pendingVersionId = static_cast<ClientVersionID>(message.read<uint32_t>());
	const uint32_t fileCount = message.read<uint32_t>();

	pendingAssetManifest.clear();
	pendingAssetManifest.reserve(fileCount);
	for (uint32_t i = 0; i < fileCount; ++i) {
		LiveAssetFile file;
		file.kind = static_cast<LiveAssetKind>(message.read<uint8_t>());
		file.filename = message.read<std::string>();
		file.size = message.read<uint32_t>();
		pendingAssetManifest.push_back(file);
	}

	resetLiveAssetReceiveState(assetReceiveState);
	waitingForServerAssets = true;
	ignoreIncomingAssets = false;
	assetBytesExpected = 0;
	assetBytesReceived = 0;
	assetProgressReported = 0;
	for (const LiveAssetFile& file : pendingAssetManifest) {
		assetBytesExpected += file.size;
	}

	if (isLiveAssetCacheComplete(pendingVersionId, pendingAssetManifest)) {
		logMessage("Using cached assets from a previous live session.");
		ignoreIncomingAssets = true;
		wxTheApp->CallAfter([this]() {
			finishLiveVersionLoad();
		});
		return;
	}

	logMessage("Downloading assets from server...");
}

void LiveClient::parseAssetFileBegin(NetworkMessage& message) {
	const LiveAssetKind kind = static_cast<LiveAssetKind>(message.read<uint8_t>());
	const std::string filename = message.read<std::string>();
	const uint32_t size = message.read<uint32_t>();

	if (ignoreIncomingAssets) {
		return;
	}

	wxString error;
	if (!beginLiveAssetReceive(assetReceiveState, pendingVersionId, kind, filename, size, error)) {
		disconnectFromServer("Asset download failed: " + error);
	}
}

void LiveClient::parseAssetFileChunk(NetworkMessage& message) {
	const std::string chunk = message.read<std::string>();

	if (ignoreIncomingAssets) {
		return;
	}

	assetBytesReceived += chunk.size();
	if (assetBytesExpected > 0) {
		const int percent = static_cast<int>((assetBytesReceived * 100) / assetBytesExpected);
		if (percent >= assetProgressReported + 5) {
			while (assetProgressReported + 5 <= percent) {
				assetProgressReported += 5;
			}
			logMessage(wxString::Format("Downloading assets... %d%%", assetProgressReported));
		}
	}

	wxString error;
	if (!appendLiveAssetChunk(assetReceiveState, chunk, error)) {
		disconnectFromServer("Asset download failed: " + error);
	}
}

void LiveClient::parseAssetFileEnd(NetworkMessage& WXUNUSED(message)) {
	if (ignoreIncomingAssets) {
		return;
	}

	wxString error;
	if (!finishLiveAssetReceive(assetReceiveState, error)) {
		disconnectFromServer("Asset download failed: " + error);
	}
}

void LiveClient::parseAssetFilesDone(NetworkMessage& WXUNUSED(message)) {
	if (ignoreIncomingAssets) {
		return;
	}

	waitingForServerAssets = false;
	if (assetBytesExpected > 0 && assetProgressReported < 100) {
		assetProgressReported = 100;
		logMessage("Downloading assets... 100%");
	}
	wxTheApp->CallAfter([this]() {
		finishLiveVersionLoad();
	});
}

void LiveClient::finishLiveVersionLoad() {
	waitingForServerAssets = false;
	logMessage("Loading server client version...");

	// Loading the server's client assets swaps the global asset state, so any open
	// local maps must be closed first. Warn before discarding the user's work — at
	// this point only local maps are open (the live map tab is created later), so
	// GetOpenMapCount() counts exactly what would be closed.
	if (!g_gui.IsHeadless() && g_gui.GetOpenMapCount() > 0) {
		const long response = g_gui.PopupDialog(
			"Close open maps?",
			"Connecting to the live server loads the server's client assets and will close the map(s) you currently have open.\n\nDo you want to continue?",
			wxYES | wxNO
		);
		if (response != wxID_YES) {
			disconnectFromServer("Live connection cancelled - open maps were kept.");
			return;
		}
	}

	if (!g_gui.CloseAllEditors()) {
		disconnectFromServer("Could not close open maps for the live session.");
		return;
	}

	if (!applyLiveAssetCacheToVersion(pendingVersionId)) {
		disconnectFromServer("Failed to prepare downloaded assets.");
		return;
	}

	wxString error;
	wxArrayString warnings;
	if (!g_gui.LoadVersion(pendingVersionId, error, warnings, true, false)) {
		disconnectFromServer("Failed to load server client version: " + error);
		return;
	}

	if (!warnings.empty()) {
		g_gui.ListDialog("Version load warnings", warnings);
	}

	sendReady();
}

void LiveClient::failUpdate(const wxString& reason) {
	receivingUpdate = false;
	resetLiveUpdateReceiveState(updateReceiveState);
	logMessage("Editor update failed: " + reason);

	// Defer teardown: called from inside the parsePacket loop, and CloseLiveEditors
	// deletes this client. stopped guards against double teardown.
	if (stopped) {
		return;
	}
	stopped = true;
	wxTheApp->CallAfter([this, reason]() {
		g_gui.PopupDialog("Update Failed", reason, wxOK);
		closeAndTeardown();
	});
}

void LiveClient::parseUpdateAvailable(NetworkMessage& message) {
	const uint32_t serverVersion = message.read<uint32_t>();
	const uint32_t fileCount = message.read<uint32_t>();

	(void)serverVersion;

	resetLiveUpdateReceiveState(updateReceiveState);
	receivingUpdate = true;
	updateBytesExpected = 0;
	updateBytesReceived = 0;
	updateProgressReported = 0;

	for (uint32_t i = 0; i < fileCount; ++i) {
		(void)message.read<std::string>(); // filename (sent again per-file)
		updateBytesExpected += message.read<uint32_t>();
	}

	logMessage("Your editor is out of date. Downloading update from server...");
}

void LiveClient::parseUpdateFileBegin(NetworkMessage& message) {
	const std::string filename = message.read<std::string>();
	const uint32_t size = message.read<uint32_t>();

	if (!receivingUpdate) {
		return;
	}

	wxString error;
	if (!beginLiveUpdateReceive(updateReceiveState, filename, size, error)) {
		failUpdate(error);
	}
}

void LiveClient::parseUpdateFileChunk(NetworkMessage& message) {
	const std::string chunk = message.read<std::string>();

	if (!receivingUpdate) {
		return;
	}

	updateBytesReceived += chunk.size();
	if (updateBytesExpected > 0) {
		const int percent = static_cast<int>((updateBytesReceived * 100) / updateBytesExpected);
		if (percent >= updateProgressReported + 5) {
			while (updateProgressReported + 5 <= percent) {
				updateProgressReported += 5;
			}
			logMessage(wxString::Format("Downloading update... %d%%", updateProgressReported));
		}
	}

	wxString error;
	if (!appendLiveUpdateChunk(updateReceiveState, chunk, error)) {
		failUpdate(error);
	}
}

void LiveClient::parseUpdateFileEnd(NetworkMessage& WXUNUSED(message)) {
	if (!receivingUpdate) {
		return;
	}

	wxString error;
	if (!finishLiveUpdateReceive(updateReceiveState, error)) {
		failUpdate(error);
	}
}

void LiveClient::parseUpdateDone(NetworkMessage& WXUNUSED(message)) {
	if (!receivingUpdate) {
		return;
	}

	receivingUpdate = false;
	logMessage("Update downloaded.");

	// Snapshot everything the updater needs into plain locals. The teardown below
	// deletes this LiveClient (CloseLiveEditors -> delete), and the server's FIN
	// can independently trigger handleError -> CloseLiveEditors, so the deferred
	// prompt/apply must not touch 'this' or updateReceiveState afterwards.
	const wxFileName stagingDir = updateReceiveState.stagingDir;
	const std::vector<std::string> files = updateReceiveState.receivedFiles;
	resetLiveUpdateReceiveState(updateReceiveState);

	if (stopped) {
		return;
	}
	stopped = true;

	// Defer teardown off the parsePacket loop so 'this' is not deleted mid-parse.
	// closeAndTeardown() tears down the live session and then, once 'this' is
	// gone, runs the prompt + swap fully decoupled from the live client, using
	// only the snapshotted locals.
	wxTheApp->CallAfter([this, stagingDir, files]() {
		closeAndTeardown([stagingDir, files]() {
			if (files.empty()) {
				g_gui.PopupDialog("Update Failed", "The server did not send any update files.", wxOK);
				return;
			}

			const long choice = g_gui.PopupDialog(
				"Editor Update",
				"A newer version of the editor is required to join this live server.\n\n"
				"Update now? The editor will restart and reconnect automatically.",
				wxYES | wxNO
			);

			if (choice != wxID_YES) {
				return;
			}

			wxString error;
			if (!applyLiveUpdateAndRestart(stagingDir, files, true, error)) {
				g_gui.PopupDialog("Update Failed", error, wxOK);
				return;
			}

			// New instance is launching; shut this one down so it can replace us.
			if (g_gui.root) {
				g_gui.root->Close(true);
			}
		});
	});
}

void LiveClient::parseNode(NetworkMessage& message) {
	uint32_t ind = message.read<uint32_t>();
	pendingNodeRequests.erase(ind);

	// Extract node position
	int32_t ndx = ind >> 18;
	int32_t ndy = (ind >> 4) & 0x3FFF;
	bool underground = ind & 1;

	Action* action = editor->createAction(ACTION_REMOTE);
	receiveNode(message, *editor, action, ndx, ndy, underground);
	if (action->size() > 0) {
		// Apply streamed tiles locally without undo history or echoing to the server.
		action->commit(nullptr);
	}
	delete action;

	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void LiveClient::parseCursorUpdate(NetworkMessage& message) {
	LiveCursor cursor = readCursor(message);
	if (cursor.id != ownClientId) {
		cursors[cursor.id] = cursor;
	}

	g_gui.RefreshView();
}

void LiveClient::parsePing(NetworkMessage& message) {
	addPing(readPing(message));
	g_gui.RefreshView();
}

void LiveClient::parseClientList(NetworkMessage& message) {
	readParticipantList(message);
	for (const LiveParticipant& participant : participants) {
		if (participant.id == ownClientId) {
			ownClientColor = participant.color;
			break;
		}
	}

	// Drop cursors for clients that are no longer connected. The server only ever
	// pushes cursor *updates*, so without this a disconnected peer's cursor would
	// stay rendered at its last position forever.
	for (auto it = cursors.begin(); it != cursors.end();) {
		const uint32_t id = it->first;
		bool stillConnected = false;
		for (const LiveParticipant& participant : participants) {
			if (participant.id == id) {
				stillConnected = true;
				break;
			}
		}
		it = stillConnected ? std::next(it) : cursors.erase(it);
	}

	requestViewportRefresh();
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void LiveClient::parseCommentList(NetworkMessage& message) {
	const uint32_t count = message.read<uint32_t>();
	std::vector<MapComment> loaded;
	loaded.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		loaded.push_back(readMapComment(message));
	}

	if (!editor) {
		pendingCommentList = std::move(loaded);
		hasPendingCommentList = true;
		return;
	}

	editor->getMap().getComments().setAll(std::move(loaded), 1);
	g_gui.RefreshView();
	g_gui.RefreshCommentsWindow();
}

void LiveClient::parseComment(NetworkMessage& message) {
	if (!editor) {
		return;
	}

	editor->getMap().getComments().upsert(readMapComment(message));
	g_gui.RefreshView();
	g_gui.RefreshCommentsWindow();
}

void LiveClient::parseCommentRemoved(NetworkMessage& message) {
	if (!editor) {
		return;
	}

	const uint32_t commentId = message.read<uint32_t>();
	editor->getMap().getComments().removeById(commentId);
	g_gui.RefreshView();
	g_gui.RefreshCommentsWindow();
}

void LiveClient::parseStartOperation(NetworkMessage& message) {
	const std::string& operation = message.read<std::string>();

	currentOperation = wxstr(operation);
	g_gui.SetStatusText("Server Operation in Progress: " + currentOperation + "... (0%)");
}

void LiveClient::parseUpdateOperation(NetworkMessage& message) {
	int32_t percent = message.read<uint32_t>();
	if (percent >= 100) {
		g_gui.SetStatusText("Server Operation Finished.");
	} else {
		g_gui.SetStatusText("Server Operation in Progress: " + currentOperation + "... (" + std::to_string(percent) + "%)");
	}
}

void LiveClient::parseItemBlockList(NetworkMessage& message) {
	setBlockedItemIds({});
	readBlockedItemList(message, blockedItemIds);
}

void LiveClient::setBlockedItemIds(std::set<uint16_t> ids) {
	blockedItemIds = std::move(ids);
}

void LiveClient::warnIfBlockedBrushUse(const Brush* brush) {
	const uint16_t blockedItemId = findBlockedItemInBrush(brush, blockedItemIds, dismissedBlockedWarnings);
	if (blockedItemId != 0) {
		showBlockedItemWarning(blockedItemId);
	}
}

void LiveClient::showBlockedItemWarning(uint16_t itemId) {
	const ItemType& itemType = g_items.getItemType(itemId);
	wxString itemLabel;
	if (itemType.id != 0) {
		itemLabel = wxString::Format("Item %u (%s)", itemId, wxstr(itemType.name));
	} else {
		itemLabel = wxString::Format("Item %u", itemId);
	}

	const wxString message = wxString::Format(
		"An admin has blacklisted %s.\n\nIt is deemed not fit for this map. You may still use it, but please consider an alternative.",
		itemLabel
	);

	bool dontShowAgain = false;
	if (g_gui.IsHeadless() || !g_gui.root) {
		std::cout << "[Blocked Item] " << message.ToStdString() << std::endl;
	} else {
		BlockedItemWarningDialog dialog(g_gui.root, message);
		dialog.ShowModal();
		dontShowAgain = dialog.GetDontShowAgain();
	}

	if (dontShowAgain) {
		dismissedBlockedWarnings.insert(itemId);
	}
}
