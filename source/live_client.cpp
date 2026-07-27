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
	// Pause between editor-version probe attempts, so walking a long candidate list
	// stays a polite trickle of connections rather than a burst.
	constexpr int LIVE_VERSION_PROBE_DELAY_MS = 150;

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
	initVersionProbe();
}

void LiveClient::initVersionProbe() {
	// A pinned version is the starting point rather than an override of the probe,
	// so a user who knows roughly which build the server runs can seed the search
	// instead of waiting for it to walk all the way down from ours.
	probeVersionId = __RME_VERSION_ID__;
	const wxString pinnedVersion = wxstr(g_settings.getString(Config::LIVE_SPOOF_VERSION));
	if (!pinnedVersion.empty() && !parseEditorVersionId(pinnedVersion, probeVersionId)) {
		probeVersionId = __RME_VERSION_ID__;
	}

	probeStartVersionId = probeVersionId;
	versionProbeEnabled = g_settings.getInteger(Config::LIVE_AUTO_VERSION_PROBE) != 0;
	probeLimit = static_cast<uint32_t>(std::max(1, g_settings.getInteger(Config::LIVE_VERSION_PROBE_LIMIT)));
	probeMaxSubversion = static_cast<uint32_t>(std::max(0, g_settings.getInteger(Config::LIVE_VERSION_PROBE_MAX_SUBVERSION)));
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

	// Stop the version search before the socket goes: a probe retry firing after
	// teardown has begun would reconnect a client that is about to be deleted.
	versionProbeSettled = true;
	if (probeTimer) {
		probeTimer->cancel();
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

void LiveClient::onProbeFailure(uint32_t generation, const wxString& reason) {
	// Both the kick packet and the socket EOF report the same refusal, and either
	// can arrive first. The generation makes whichever lands first the one that
	// advances, and turns the other into a no-op.
	if (versionProbeSettled || generation != probeGeneration) {
		return;
	}
	++probeGeneration;

	// The first refusal is the moment we learn the server is not our build. Get the
	// user's consent before announcing a version that is not ours -- deferred so the
	// modal opens after the parsePacket loop that got us here has unwound.
	if (!versionMismatchPrompted) {
		wxTheApp->CallAfter([this, reason]() {
			if (versionProbeSettled) {
				return;
			}
			versionMismatchPrompted = true;
			if (!confirmVersionMismatch()) {
				versionProbeSettled = true;
				abortAfterRefusal(reason);
				return;
			}
			advanceProbe(reason);
		});
		return;
	}

	advanceProbe(reason);
}

bool LiveClient::confirmVersionMismatch() {
	const long answer = g_gui.PopupDialog(
		"Editor Version Mismatch",
		"This server is not running editor version " + __W_RME_VERSION__ + ".\n\n"
		"The editor can announce an older version to get in, but the two builds will "
		"not agree on everything. Map data, brushes, or live edits may not work "
		"correctly, and in the worst case you could corrupt the map you are editing.\n\n"
		"Search for a version this server accepts and connect anyway?",
		wxYES | wxNO | wxICON_EXCLAMATION
	);
	return answer == wxID_YES;
}

void LiveClient::advanceProbe(const wxString& reason) {
	const uint32_t nextVersionId = previousEditorVersionId(probeVersionId, probeMaxSubversion);
	if (probeAttempt + 1 >= probeLimit || nextVersionId == 0) {
		versionProbeSettled = true;

		const wxString firstTried = formatEditorVersionId(probeStartVersionId);
		const wxString lastTried = formatEditorVersionId(probeVersionId);

		logMessage("Gave up after " + wxString::Format("%u", probeAttempt + 1) + " version(s), " + firstTried + " down to " + lastTried + ".");

		abortAfterRefusal(
			(reason.empty() ? wxString("The server refused the connection.") : reason)
			+ "\n\nTried editor versions " + firstTried + " down to " + lastTried
			+ " without success. If you know which version the server runs, enter it "
			  "under \"Report version\" in the connect dialog."
		);
		return;
	}

	++probeAttempt;
	probeVersionId = nextVersionId;
	scheduleNextProbe();
}

void LiveClient::abortAfterRefusal(const wxString& reason) {
	if (stopped) {
		return;
	}
	stopped = true;

	wxTheApp->CallAfter([this, reason]() {
		g_gui.PopupDialog("Disconnected", reason, wxOK);
		closeAndTeardown();
	});
}

void LiveClient::scheduleNextProbe() {
	logMessage("Refused; retrying as editor version " + formatEditorVersionId(probeVersionId) + "...");

	// Hop to the network thread before touching the socket: the read handlers for
	// the attempt we are abandoning may still be in flight there, and replacing the
	// socket from the GUI thread would race them.
	NetworkConnection::getInstance().post([this]() {
		if (versionProbeSettled) {
			return;
		}

		if (socket) {
			net_error_code closeError;
			socket->close(closeError);
		}
		// The abandoned attempt may have left a queued or in-flight write behind;
		// clear it so the new connection starts with an empty pipe.
		writeQueue.clear();
		writing = false;
		helloSent = false;

		if (!probeTimer) {
			probeTimer = std::make_shared<asio::steady_timer>(NetworkConnection::getInstance().get_service());
		}
		// Space the attempts out so a deep search does not look like a flood to the
		// server (or to anything sitting in front of it).
		probeTimer->expires_after(std::chrono::milliseconds(LIVE_VERSION_PROBE_DELAY_MS));
		probeTimer->async_wait([this](const net_error_code& error) {
			if (error || versionProbeSettled) {
				return;
			}
			if (!connect(connectionAddress, connectionPort)) {
				versionProbeSettled = true;
				disconnectFromServer("Could not reconnect: " + getLastError());
			}
		});
	});
}

void LiveClient::settleVersionProbe(bool accepted) {
	if (versionProbeSettled) {
		return;
	}
	versionProbeSettled = true;

	if (!accepted || probeVersionId == __RME_VERSION_ID__) {
		return;
	}

	logMessage("Server accepted editor version " + formatEditorVersionId(probeVersionId) + ", but this editor is " + __W_RME_VERSION__ + ".");
	logMessage("Build mismatch: some edits may not behave correctly. Save a backup before making large changes.");
	// Persist the winner so the next connect to this host skips the search.
	g_settings.setString(Config::LIVE_SPOOF_VERSION, nstr(formatEditorVersionId(probeVersionId)));
}

void LiveClient::disconnectFromServer(const wxString& reason) {
	if (versionProbeEnabled && !versionProbeSettled && helloSent) {
		// The server hung up on our hello. That is the version refusal -- its kick
		// packet may still be queued for the GUI thread -- so hand this to the probe
		// instead of tearing the session down.
		const uint32_t generation = probeGeneration;
		wxTheApp->CallAfter([this, generation, reason]() {
			onProbeFailure(generation, reason);
		});
		return;
	}

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
	// Only the editor version can differ from this build's own -- the live protocol
	// version below governs packet layout and is always reported truthfully.
	if (probeVersionId != __RME_VERSION_ID__) {
		logMessage("Announcing editor version " + formatEditorVersionId(probeVersionId) + " instead of " + __W_RME_VERSION__ + ".");
	}

	NetworkMessage message;
	message.write<uint8_t>(PACKET_HELLO_FROM_CLIENT);
	message.write<uint32_t>(probeVersionId);
	message.write<uint32_t>(__LIVE_NET_VERSION__);
	message.write<uint32_t>(g_gui.GetCurrentVersionID());
	message.write<std::string>(nstr(name));
	message.write<std::string>(nstr(password));
	message.write<uint8_t>(ownClientColor.Red());
	message.write<uint8_t>(ownClientColor.Green());
	message.write<uint8_t>(ownClientColor.Blue());
	message.write<uint8_t>(ownClientColor.Alpha());

	send(message, [this]() {
		// From here on a dropped connection means the server judged our hello, so a
		// failure is a version candidate being rejected rather than an unreachable host.
		helloSent = true;
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

		// Anything other than a kick means the server got past the version check,
		// so the version we announced is the one it wants.
		if (packetType != PACKET_KICK) {
			settleVersionProbe(true);
		}

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
	const bool wrongVersion = (kickMessage == "Wrong editor version.");

	if (versionProbeEnabled && !versionProbeSettled) {
		if (wrongVersion) {
			// Checked before the `stopped` guard below: the socket EOF that follows
			// this kick may already have set it, and losing the refusal here would
			// strand the probe.
			onProbeFailure(probeGeneration, wxstr(kickMessage));
			return;
		}
		// Wrong password, wrong protocol version, failed asset sync -- none of these
		// get better on an older editor version, so stop searching and report it.
		settleVersionProbe(false);
	}

	// Defer teardown: this runs inside the parsePacket loop, and CloseLiveEditors
	// deletes this client. Setting stopped first makes it idempotent and stops the
	// loop from touching freed state afterwards.
	if (stopped) {
		return;
	}
	stopped = true;
	wxString reason = wxstr(kickMessage);
	if (wrongVersion) {
		reason << "\n\nThis server runs a different editor build and only accepts its own "
				  "version. Tick \"If refused, try earlier editor versions\" in the connect "
				  "dialog to search for one it accepts, or enter the host's version under "
				  "\"Report version\" if you know it.";
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
