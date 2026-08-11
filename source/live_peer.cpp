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

#include "live_peer.h"
#include "live_server.h"
#include "live_tab.h"
#include "live_action.h"
#include "live_assets.h"
#include "net_connection.h"

#include "editor.h"
#include "gui.h"

#include <iostream>
#include <vector>

// How many chunks to keep queued ahead of the write that's actually in flight.
// Chunk transfers used to enqueue one chunk, then wait for it to be written
// before reading/enqueueing the next -- capping throughput at one chunk per
// network round-trip regardless of available bandwidth, which dominated on
// higher-latency links. Windowing lets the write queue (see doWrite()) keep
// draining back-to-back instead of idling between chunks.
static const size_t LIVE_TRANSFER_WINDOW = 8;

static void livePeerLog(LiveLogTab* log, const wxString& message) {
	std::cout << "[live] " << message.ToStdString() << std::endl;
	std::cout.flush();
	if (log) {
		wxTheApp->CallAfter([log, message]() {
			log->Message(message);
		});
	}
}

LivePeer::LivePeer(LiveServer* server, asio::ip::tcp::socket socket) :
	LiveSocket(),
	readMessage(), server(server), socket(std::move(socket)), color(), id(0), clientId(0), connected(false),
	assetFiles(), assetFileIndex(0), assetStream(), assetBytesRemaining(0),
	updateFiles(), updateFileIndex(0), updateStream(), updateBytesRemaining(0),
	assetChunkBuffer(LIVE_ASSET_CHUNK_SIZE, '\0'),
	updateChunkBuffer(LIVE_UPDATE_CHUNK_SIZE, '\0') {
	ASSERT(server != nullptr);
	mapVersion = server->getEditor()->getMap().getVersion();
}

LivePeer::~LivePeer() {
	if (socket.is_open()) {
		socket.close();
	}
}

void LivePeer::close() {
	if (socket.is_open()) {
		net_error_code ec;
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		socket.close(ec);
	}
	server->removeClient(id);
}

bool LivePeer::handleError(const net_error_code& error) {
	// The read loop and the write queue can both surface the same disconnect; log
	// and close only once so a dropped peer can't spam the server console.
	if (disconnecting) {
		return true;
	}
	disconnecting = true;

	if (error == asio::error::eof || error == asio::error::connection_reset) {
		livePeerLog(log, wxString() + getHostName() + ": disconnected.");
	} else if (error == asio::error::connection_aborted) {
		livePeerLog(log, name + " have left the server.");
	} else {
		// Any other socket error (keepalive timeout, network drop, host
		// unreachable, ...) also means the peer is gone. These do NOT report as
		// eof/connection_reset, so they must still tear the connection down --
		// otherwise the peer lingers in the server's client map with its client
		// id still allocated, and a reconnect from the same user shows up as a
		// duplicate.
		livePeerLog(log, wxString() + getHostName() + ": " + error.message());
	}
	// Always remove the peer; every error path here is terminal for the socket.
	close();
	return true;
}

std::string LivePeer::getHostName() const {
	net_error_code error;
	auto endpoint = socket.remote_endpoint(error);
	if (error) {
		return "unknown";
	}
	return endpoint.address().to_string();
}

void LivePeer::receiveHeader() {
	readMessage.clear();
	readMessage.position = 0;
	asio::async_read(socket,
		asio::buffer(readMessage.buffer, 4),
		[this](const net_error_code& error, size_t bytesReceived) -> void {
			if (error) {
				if (!handleError(error)) {
					livePeerLog(log, wxString() + getHostName() + ": " + error.message());
				}
			} else if (bytesReceived < 4) {
				livePeerLog(log, wxString() + getHostName() + ": Could not receive header[size: " + std::to_string(bytesReceived) + "], disconnecting client.");
				close();
			} else {
				const uint32_t packetSize = readMessage.read<uint32_t>();
				receive(packetSize);
			}
		});
}

void LivePeer::receive(uint32_t packetSize) {
	readMessage.buffer.resize(readMessage.position + packetSize);
	asio::async_read(socket,
		asio::buffer(&readMessage.buffer[readMessage.position], packetSize),
		[this](const net_error_code& error, size_t bytesReceived) -> void {
			if (error) {
				if (!handleError(error)) {
					livePeerLog(log, wxString() + getHostName() + ": " + error.message());
				}
			} else if (bytesReceived < readMessage.buffer.size() - 4) {
				livePeerLog(log, wxString() + getHostName() + ": Could not receive packet[size: " + std::to_string(bytesReceived) + "], disconnecting client.");
				close();
			} else {
				NetworkMessage packet = std::move(readMessage);
				if (connected) {
					if (g_gui.IsHeadless()) {
						parseEditorPacket(std::move(packet));
					} else {
						wxTheApp->CallAfter([this, packet = std::move(packet)]() mutable {
							parseEditorPacket(std::move(packet));
						});
					}
				} else {
					// Login handshake runs on the network thread so the headless
					// MapServer does not depend on wx CallAfter dispatch.
					parseLoginPacket(std::move(packet));
				}
				NetworkConnection::getInstance().post([this]() {
					receiveHeader();
				});
			}
		});
}

void LivePeer::send(NetworkMessage& message) {
	send(message, nullptr);
}

void LivePeer::send(NetworkMessage& message, std::function<void()> onSent) {
	auto packet = std::make_shared<NetworkMessage>();
	packet->buffer = message.buffer;
	packet->position = message.position;
	packet->size = message.size;

	NetworkConnection::getInstance().dispatch([this, packet, onSent = std::move(onSent)]() mutable {
		memcpy(&packet->buffer[0], &packet->size, 4);
		writeQueue.push_back(OutboundPacket { std::move(packet), std::move(onSent) });
		// Only kick off a write if none is in flight; the completion handler drains
		// the rest in order. This serialization is what prevents the interleaved
		// async_writes that were corrupting the live stream.
		if (!writing) {
			writing = true;
			doWrite();
		}
	});
}

void LivePeer::doWrite() {
	// Runs on the network thread with writing == true and a non-empty queue.
	const OutboundPacket& front = writeQueue.front();
	const size_t packetLength = front.message->size + 4;
	asio::async_write(socket,
		asio::buffer(front.message->buffer.data(), packetLength),
		[this](const net_error_code& error, size_t bytesTransferred) -> void {
			OutboundPacket completed = std::move(writeQueue.front());
			writeQueue.pop_front();

			if (error) {
				// One write error is terminal for the socket; drop the rest and tear
				// down once (handleError is guarded so queued failures don't spam).
				writing = false;
				writeQueue.clear();
				handleError(error);
				return;
			}

			if (completed.onSent) {
				completed.onSent(); // may enqueue the next chunk (asset/update transfer)
			}

			if (!writeQueue.empty()) {
				doWrite();
			} else {
				writing = false;
			}
		});
}

void LivePeer::parseLoginPacket(NetworkMessage message) {
	// A login message carries exactly one packet (HELLO or READY). Handle it and
	// stop: HELLO branches that reject/redirect the client (wrong version, update
	// push) intentionally read only part of the buffer, so continuing the loop
	// would misread the remaining HELLO fields as bogus packets and sever the
	// connection mid-transfer.
	if (message.position >= message.buffer.size()) {
		return;
	}

	const uint8_t packetType = message.read<uint8_t>();
	switch (packetType) {
		case PACKET_HELLO_FROM_CLIENT:
			parseHello(message);
			break;
		case PACKET_READY_CLIENT:
			parseReady(message);
			break;
		default: {
			livePeerLog(log, "Invalid login packet received, connection severed.");
			close();
			break;
		}
	}
}

void LivePeer::parseEditorPacket(NetworkMessage message) {
	uint8_t packetType;
	while (message.position < message.buffer.size()) {
		packetType = message.read<uint8_t>();
		switch (packetType) {
			case PACKET_REQUEST_NODES:
				parseNodeRequest(message);
				break;
			case PACKET_CHANGE_LIST:
				parseReceiveChanges(message);
				break;
			case PACKET_CLIENT_UPDATE_CURSOR:
				parseCursorUpdate(message);
				break;
			case PACKET_CLIENT_UPDATE_COLOR:
				parseColorUpdate(message);
				break;
			case PACKET_CLIENT_PING:
				parseClientPing(message);
				break;
			case PACKET_CLIENT_KEEPALIVE:
				// No payload, nothing to do -- receiving it is the point: it's what
				// keeps the connection carrying traffic while the client is idle or
				// tabbed out, so NAT/firewall idle timeouts don't drop the session.
				break;
			case PACKET_COMMENT_ADD:
				parseCommentAdd(message);
				break;
			case PACKET_COMMENT_REMOVE:
				parseCommentRemove(message);
				break;
			case PACKET_COMMENT_EDIT:
				parseCommentEdit(message);
				break;
			default: {
				livePeerLog(log, "Invalid editor packet received, connection severed.");
				close();
				break;
			}
		}
	}
}

void LivePeer::parseHello(NetworkMessage& message) {
	if (connected) {
		close();
		return;
	}

	uint32_t rmeVersion = message.read<uint32_t>();
	uint32_t netVersion = message.read<uint32_t>();

	// The live protocol version governs packet layout, so a mismatch there is a
	// hard refusal. A different editor version is tolerated (the client is warned
	// after the handshake) as long as it speaks the same protocol.
	if (netVersion != __LIVE_NET_VERSION__) {
		NetworkMessage outMessage;
		outMessage.write<uint8_t>(PACKET_KICK);
		wxString kickText;
		if (netVersion < __LIVE_NET_VERSION__) {
			livePeerLog(log, "Client tried to connect with an outdated live protocol version, connection refused.");
			kickText = "Your editor is too old to talk to this server (it runs Tilera "
				+ __W_RME_VERSION__ + "). Please update to a newer version.";
		} else {
			livePeerLog(log, "Client tried to connect with a newer live protocol version, connection refused.");
			kickText = "Your editor is newer than this server (it runs Tilera "
				+ __W_RME_VERSION__ + ") and their live protocols are incompatible.";
		}
		outMessage.write<std::string>(nstr(kickText));

		send(outMessage);
		close();
		return;
	}

	if (rmeVersion != __RME_VERSION_ID__) {
		// If the client is running an older build and the server has an update
		// package configured, push the newer editor so the client can self-update
		// and reconnect instead of connecting with a mismatched build.
		if (rmeVersion < __RME_VERSION_ID__ && server->hasUpdatePackage()) {
			livePeerLog(log, "Client is outdated; sending editor update.");
			sendUpdatePackage();
			return;
		}

		livePeerLog(
			log,
			"Client runs editor version " + formatEditorVersionId(rmeVersion)
				+ " (this server is " + __W_RME_VERSION__ + "); allowing the connection with a warning."
		);
	}

	uint32_t clientVersion = message.read<uint32_t>(); // client-reported version; server assets are authoritative
	std::string nickname = message.read<std::string>();
	std::string password = message.read<std::string>();
	const uint8_t r = message.read<uint8_t>();
	const uint8_t g = message.read<uint8_t>();
	const uint8_t b = message.read<uint8_t>();
	const uint8_t a = message.read<uint8_t>();
	setUsedColor(wxColor(r, g, b, a));

	if (server->getPassword() != wxString(password.c_str(), wxConvUTF8)) {
		livePeerLog(log, "Client tried to connect, but used the wrong password, connection refused.");

		NetworkMessage outMessage;
		outMessage.write<uint8_t>(PACKET_KICK);
		outMessage.write<std::string>("Wrong password.");
		send(outMessage);
		close();
		return;
	}

	name = wxString(nickname.c_str(), wxConvUTF8);
	livePeerLog(log, name + " (" + getHostName() + ") connected (editor version " + std::to_string(clientVersion) + ").");

	NetworkMessage outMessage;
	outMessage.write<uint8_t>(PACKET_CHANGE_CLIENT_VERSION);
	// This server's own editor version, so the client can warn the user (with
	// both versions) when the two builds differ.
	outMessage.write<uint32_t>(__RME_VERSION_ID__);
	outMessage.write<uint32_t>(g_gui.GetCurrentVersionID());

	wxString assetError;
	if (!collectServerAssetFiles(assetFiles, assetError)) {
		livePeerLog(log, "Asset sync failed: " + assetError);

		NetworkMessage kickMessage;
		kickMessage.write<uint8_t>(PACKET_KICK);
		kickMessage.write<std::string>(nstr(assetError));
		send(kickMessage);
		close();
		return;
	}

	outMessage.write<uint32_t>(static_cast<uint32_t>(assetFiles.size()));
	for (const LiveAssetFile& file : assetFiles) {
		outMessage.write<uint8_t>(file.kind);
		outMessage.write<std::string>(file.filename);
		outMessage.write<uint32_t>(file.size);
	}

	send(outMessage, [this]() {
		startAssetTransfer();
	});
}

void LivePeer::startAssetTransfer() {
	assetFileIndex = 0;
	assetChunksInFlight = 0;
	pumpAssetTransfer();
}

void LivePeer::pumpAssetTransfer() {
	while (assetChunksInFlight < LIVE_TRANSFER_WINDOW && assetFileIndex < assetFiles.size()) {
		if (!sendNextAssetChunk()) {
			return; // error path already closed the connection
		}
	}

	if (assetChunksInFlight == 0 && assetFileIndex >= assetFiles.size()) {
		NetworkMessage doneMessage;
		doneMessage.write<uint8_t>(PACKET_ASSET_FILES_DONE);
		send(doneMessage);
	}
}

bool LivePeer::sendNextAssetChunk() {
	if (!assetStream.is_open()) {
		const LiveAssetFile& file = assetFiles[assetFileIndex];
		assetStream.open(nstr(file.sourcePath.GetFullPath()), std::ios::binary);
		if (!assetStream.is_open()) {
			livePeerLog(log, "Failed to open asset file: " + file.sourcePath.GetFullPath());
			close();
			return false;
		}

		assetBytesRemaining = file.size;

		NetworkMessage beginMessage;
		beginMessage.write<uint8_t>(PACKET_ASSET_FILE_BEGIN);
		beginMessage.write<uint8_t>(file.kind);
		beginMessage.write<std::string>(file.filename);
		beginMessage.write<uint32_t>(file.size);
		send(beginMessage);
	}

	const size_t toRead = std::min<size_t>(LIVE_ASSET_CHUNK_SIZE, assetBytesRemaining);
	assetStream.read(assetChunkBuffer.data(), static_cast<std::streamsize>(toRead));
	const std::streamsize bytesRead = assetStream.gcount();
	if (bytesRead <= 0) {
		livePeerLog(log, "Failed while reading asset file for transfer.");
		close();
		return false;
	}

	std::string chunk(assetChunkBuffer.data(), static_cast<size_t>(bytesRead));
	assetBytesRemaining -= static_cast<uint32_t>(bytesRead);

	NetworkMessage chunkMessage;
	chunkMessage.write<uint8_t>(PACKET_ASSET_FILE_CHUNK);
	chunkMessage.write<std::string>(chunk);
	++assetChunksInFlight;
	send(chunkMessage, [this]() {
		--assetChunksInFlight;
		pumpAssetTransfer();
	});

	if (assetBytesRemaining == 0) {
		assetStream.close();

		NetworkMessage endMessage;
		endMessage.write<uint8_t>(PACKET_ASSET_FILE_END);
		send(endMessage);
		++assetFileIndex;
	}

	return true;
}

void LivePeer::sendUpdatePackage() {
	updateFiles = server->getUpdateFiles();
	if (updateFiles.empty()) {
		NetworkMessage outMessage;
		outMessage.write<uint8_t>(PACKET_KICK);
		outMessage.write<std::string>("Wrong editor version.");
		send(outMessage);
		close();
		return;
	}

	NetworkMessage outMessage;
	outMessage.write<uint8_t>(PACKET_UPDATE_AVAILABLE);
	outMessage.write<uint32_t>(__RME_VERSION_ID__);
	outMessage.write<uint32_t>(static_cast<uint32_t>(updateFiles.size()));
	for (const LiveUpdateFile& file : updateFiles) {
		outMessage.write<std::string>(file.filename);
		outMessage.write<uint32_t>(file.size);
	}

	send(outMessage, [this]() {
		startUpdateTransfer();
	});
}

void LivePeer::startUpdateTransfer() {
	updateFileIndex = 0;
	updateChunksInFlight = 0;
	pumpUpdateTransfer();
}

void LivePeer::pumpUpdateTransfer() {
	while (updateChunksInFlight < LIVE_TRANSFER_WINDOW && updateFileIndex < updateFiles.size()) {
		if (!sendNextUpdateChunk()) {
			return; // error path already closed the connection
		}
	}

	if (updateChunksInFlight == 0 && updateFileIndex >= updateFiles.size()) {
		NetworkMessage doneMessage;
		doneMessage.write<uint8_t>(PACKET_UPDATE_DONE);
		// Close once the final packet has flushed; the client takes over from here.
		send(doneMessage, [this]() {
			close();
		});
	}
}

bool LivePeer::sendNextUpdateChunk() {
	if (!updateStream.is_open()) {
		const LiveUpdateFile& file = updateFiles[updateFileIndex];
		updateStream.open(nstr(file.sourcePath.GetFullPath()), std::ios::binary);
		if (!updateStream.is_open()) {
			livePeerLog(log, "Failed to open update file: " + file.sourcePath.GetFullPath());
			close();
			return false;
		}

		updateBytesRemaining = file.size;

		NetworkMessage beginMessage;
		beginMessage.write<uint8_t>(PACKET_UPDATE_FILE_BEGIN);
		beginMessage.write<std::string>(file.filename);
		beginMessage.write<uint32_t>(file.size);
		send(beginMessage);
	}

	const size_t toRead = std::min<size_t>(LIVE_UPDATE_CHUNK_SIZE, updateBytesRemaining);
	updateStream.read(updateChunkBuffer.data(), static_cast<std::streamsize>(toRead));
	const std::streamsize bytesRead = updateStream.gcount();
	if (bytesRead <= 0) {
		livePeerLog(log, "Failed while reading update file for transfer.");
		close();
		return false;
	}

	std::string chunk(updateChunkBuffer.data(), static_cast<size_t>(bytesRead));
	updateBytesRemaining -= static_cast<uint32_t>(bytesRead);

	NetworkMessage chunkMessage;
	chunkMessage.write<uint8_t>(PACKET_UPDATE_FILE_CHUNK);
	chunkMessage.write<std::string>(chunk);
	++updateChunksInFlight;
	send(chunkMessage, [this]() {
		--updateChunksInFlight;
		pumpUpdateTransfer();
	});

	if (updateBytesRemaining == 0) {
		updateStream.close();

		NetworkMessage endMessage;
		endMessage.write<uint8_t>(PACKET_UPDATE_FILE_END);
		send(endMessage);
		++updateFileIndex;
	}

	return true;
}

void LivePeer::parseReady(NetworkMessage& message) {
	if (connected) {
		close();
		return;
	}

	connected = true;

	// Find free client id
	clientId = server->getFreeClientId();
	if (clientId == 0) {
		NetworkMessage outMessage;
		outMessage.write<uint8_t>(PACKET_KICK);
		outMessage.write<std::string>("Server is full.");

		send(outMessage);
		close();
		return;
	}

	setUsedColor(color);

	server->updateClientList();

	// Let's reply
	NetworkMessage outMessage;
	outMessage.write<uint8_t>(PACKET_HELLO_FROM_SERVER);

	const Map& map = server->getEditor()->getMap();
	outMessage.write<std::string>(map.getName());
	outMessage.write<uint16_t>(map.getWidth());
	outMessage.write<uint16_t>(map.getHeight());
	outMessage.write<uint32_t>(clientId);
	outMessage.write<uint8_t>(color.Red());
	outMessage.write<uint8_t>(color.Green());
	outMessage.write<uint8_t>(color.Blue());
	outMessage.write<uint8_t>(color.Alpha());

	server->writeSessionBounds(outMessage);

	send(outMessage);
	server->sendCommentList(this);
	server->sendBlockList(this);
}

void LivePeer::parseNodeRequest(NetworkMessage& message) {
	Map& map = server->getEditor()->getMap();
	const LiveSessionBounds& bounds = server->getSessionBounds();

	// Flush the response in bounded chunks instead of one giant packet. A whole
	// viewport's nodes can be several MB; sent as a single write it monopolizes the
	// (now serialized) socket for seconds -- blocking cursors/pings and forcing the
	// client to wait for the entire blob before rendering anything. Smaller packets
	// let other traffic interleave and let the client render nodes progressively.
	constexpr size_t kNodeResponseFlushBytes = 32 * 1024;

	NetworkMessage response;
	for (uint32_t nodes = message.read<uint32_t>(); nodes != 0; --nodes) {
		const uint32_t ind = message.read<uint32_t>();
		if (message.overflow) {
			break; // truncated/corrupt request; stop rather than spin on garbage
		}

		const int32_t ndx = ind >> 18;
		const int32_t ndy = (ind >> 4) & 0x3FFF;
		const bool underground = ind & 1;
		const uint32_t floorMask = underground ? 0xFF00u : 0x00FFu;

		if (bounds.enabled && !bounds.intersectsLeaf(ndx * 4, ndy * 4)) {
			appendNode(response, clientId, nullptr, ndx, ndy, floorMask);
		} else {
			QTreeNode* node = map.createLeaf(ndx * 4, ndy * 4);
			appendNode(response, clientId, node, ndx, ndy, floorMask);
		}

		if (response.size >= kNodeResponseFlushBytes) {
			send(response);
			response.clear();
		}
	}

	if (response.size > 0) {
		send(response);
	}
}

void LivePeer::parseReceiveChanges(NetworkMessage& message) {
	Editor& editor = *server->getEditor();

	const std::string data = message.read<std::string>();
	if (data.empty()) {
		livePeerLog(log, "Received empty or invalid change packet.");
		return;
	}

	changeParseBuffer.resize(data.size() + 1);
	changeParseBuffer[0] = 0;
	memcpy(&changeParseBuffer[1], data.data(), data.size());
	mapReader.assign(changeParseBuffer.data(), changeParseBuffer.size());

	BinaryNode* rootNode = mapReader.getRootNode();
	if (!rootNode) {
		livePeerLog(log, "Could not parse change packet.");
		mapReader.close();
		return;
	}

	BinaryNode* tileNode = rootNode->getChild();

	NetworkedAction* action = static_cast<NetworkedAction*>(editor.createAction(ACTION_REMOTE));
	action->owner = clientId;

	if (tileNode) {
		const LiveSessionBounds& bounds = server->getSessionBounds();
		do {
			Tile* tile = readTile(tileNode, editor, nullptr);
			if (tile) {
				if (!bounds.enabled || bounds.contains(tile->getX(), tile->getY())) {
					action->addChange(newd Change(tile));
				} else {
					delete tile;
				}
			}
		} while (tileNode->advance());
	}
	mapReader.close();

	DirtyList dirty_list;
	dirty_list.owner = clientId;
	if (action->size() > 0) {
		action->commit(&dirty_list);
		editor.getMap().doChange();
	}
	delete action;

	if (!dirty_list.Empty()) {
		server->broadcastNodes(dirty_list);
	}

	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
	}
}

void LivePeer::parseCursorUpdate(NetworkMessage& message) {
	LiveCursor cursor = readCursor(message);
	cursor.id = clientId;
	cursor.color = color;

	server->broadcastCursor(cursor);
	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
	}
}

void LivePeer::parseColorUpdate(NetworkMessage& message) {
	const uint8_t r = message.read<uint8_t>();
	const uint8_t g = message.read<uint8_t>();
	const uint8_t b = message.read<uint8_t>();
	const uint8_t a = message.read<uint8_t>();
	setUsedColor(wxColor(r, g, b, a));

	server->updateClientList();
	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
	}
}

void LivePeer::parseClientPing(NetworkMessage& message) {
	const uint16_t x = message.read<uint16_t>();
	const uint16_t y = message.read<uint16_t>();
	const uint8_t z = message.read<uint8_t>();

	const LiveSessionBounds& bounds = server->getSessionBounds();
	if (bounds.enabled && !bounds.contains(x, y)) {
		return;
	}

	LivePing ping;
	ping.id = clientId;
	ping.color = color;
	ping.pos = Position(x, y, z);
	server->broadcastPing(ping);
}

void LivePeer::parseCommentAdd(NetworkMessage& message) {
	const uint16_t x = message.read<uint16_t>();
	const uint16_t y = message.read<uint16_t>();
	const uint8_t z = message.read<uint8_t>();
	const std::string text = message.read<std::string>();
	if (text.empty()) {
		return;
	}

	const LiveSessionBounds& bounds = server->getSessionBounds();
	if (bounds.enabled && !bounds.contains(x, y)) {
		return;
	}

	Editor& editor = *server->getEditor();
	MapComment comment = editor.getMap().getComments().createComment(
		Position(x, y, z),
		nstr(name),
		text
	);
	editor.getMap().doChange();
	server->broadcastComment(comment);

	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
		g_gui.RefreshCommentsWindow();
	}
}

void LivePeer::parseCommentRemove(NetworkMessage& message) {
	const uint32_t commentId = message.read<uint32_t>();

	Editor& editor = *server->getEditor();
	if (!editor.getMap().getComments().removeById(commentId)) {
		return;
	}

	editor.getMap().doChange();
	server->broadcastCommentRemoved(commentId);

	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
		g_gui.RefreshCommentsWindow();
	}
}

void LivePeer::parseCommentEdit(NetworkMessage& message) {
	const uint32_t commentId = message.read<uint32_t>();
	const std::string text = message.read<std::string>();
	if (text.empty()) {
		return;
	}

	Editor& editor = *server->getEditor();
	const MapComment* existing = editor.getMap().getComments().findById(commentId);
	if (!existing) {
		return;
	}

	MapComment updated = *existing;
	updated.text = text;
	editor.getMap().getComments().upsert(updated);
	editor.getMap().doChange();
	server->broadcastComment(updated);

	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
		g_gui.RefreshCommentsWindow();
	}
}
