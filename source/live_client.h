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

#ifndef RME_LIVE_CLIENT_H_
#define RME_LIVE_CLIENT_H_

#include "live_socket.h"
#include "net_connection.h"
#include "live_assets.h"
#include "live_update.h"

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class DirtyList;
class MapTab;
class LiveLogTab;
class wxTimer;

class LiveClient : public LiveSocket {
public:
	LiveClient();
	~LiveClient();

	//
	bool connect(const std::string& address, uint16_t port);
	void tryConnect(asio::ip::tcp::resolver::results_type endpoints);

	void close();
	bool handleError(const net_error_code& error);
	// Logs the reason then tears the session down on the GUI thread (closes the
	// socket and the live editor tab). Safe to call from the network thread.
	void disconnectFromServer(const wxString& reason);
	// Closes the socket, then deletes this client and its live editor tab once
	// any network-thread completion handlers for the just-cancelled reads/writes
	// have drained. Every teardown path (menu disconnect, tab close, connection
	// error) must go through this instead of calling close() + CloseLiveEditors()
	// directly, or a cancelled read's completion handler can race the delete.
	// Must be called from the GUI thread. Guards against being called twice (e.g.
	// a network error racing the user closing the tab). If given, onTornDown runs
	// after this client has been deleted, so it must not capture `this`.
	void closeAndTeardown(std::function<void()> onTornDown = nullptr);

	//
	std::string getHostName() const;
	const std::string& getConnectionAddress() const {
		return connectionAddress;
	}
	uint16_t getConnectionPort() const {
		return connectionPort;
	}

	//
	void receiveHeader();
	void receive(uint32_t packetSize);
	void send(NetworkMessage& message);
	void send(NetworkMessage& message, std::function<void()> onSent);

	//
	void updateCursor(const Position& position);

	void setCursorColor(const wxColor& color);

	// Records that the user has already accepted a version mismatch for this session
	// (asked up front when they pinned a version), so the probe does not ask twice.
	void setVersionMismatchAccepted() {
		versionMismatchPrompted = true;
	}
	wxColor getOwnCursorColor() const {
		return ownClientColor;
	}
	void sendCursorColor();

	LiveLogTab* createLogWindow(wxWindow* parent);
	MapTab* createEditorWindow();

	// send packets
	void sendHello();
	void sendNodeRequests();
	void sendChanges(DirtyList& dirtyList);
	void sendReady();
	void sendCommentAdd(const Position& pos, const std::string& text);
	void sendCommentRemove(uint32_t commentId);
	void sendCommentEdit(uint32_t commentId, const std::string& text);
	void sendPing(const Position& pos);
	// Tiny no-op packet sent on a fixed interval so the connection keeps carrying
	// traffic even when the user is idle or tabbed out (see startKeepAliveTimer).
	void sendKeepAlive();

	void warnIfBlockedBrushUse(const Brush* brush);
	void setBlockedItemIds(std::set<uint16_t> ids);

	// Flags a node as queried and stores it, need to call SendNodeRequest to send it to server
	void queryNode(int32_t ndx, int32_t ndy, bool underground);
	void tickNodeRequests();
	void tickNodeRequestsIfDue();
	// Re-requests un-answered nodes on a fixed interval, independent of the paint
	// loop, so a lost node batch still recovers while the user is idle.
	void onNodeRetryTick();
	void startNodeRetryTimer();
	// Keeps the socket carrying traffic while the user is idle or the editor is
	// tabbed out, so NAT/firewall idle-connection timeouts don't silently drop
	// the session (see startKeepAliveTimer).
	void startKeepAliveTimer();
	void requestViewportRefresh();
	bool consumeViewportRefresh();
	void invalidateViewport(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y);

	Editor* getEditor() const {
		return editor;
	}

protected:
	void parsePacket(NetworkMessage message);
	void doWrite();

	// parse packets
	void parseHello(NetworkMessage& message);
	void parseKick(NetworkMessage& message);
	void parseClientAccepted(NetworkMessage& message);
	void parseChangeClientVersion(NetworkMessage& message);
	void parseAssetFileBegin(NetworkMessage& message);
	void parseAssetFileChunk(NetworkMessage& message);
	void parseAssetFileEnd(NetworkMessage& message);
	void parseAssetFilesDone(NetworkMessage& message);
	void finishLiveVersionLoad();
	void parseUpdateAvailable(NetworkMessage& message);
	void parseUpdateFileBegin(NetworkMessage& message);
	void parseUpdateFileChunk(NetworkMessage& message);
	void parseUpdateFileEnd(NetworkMessage& message);
	void parseUpdateDone(NetworkMessage& message);
	void failUpdate(const wxString& reason);
	void parseNode(NetworkMessage& message);
	void parseCursorUpdate(NetworkMessage& message);
	void parsePing(NetworkMessage& message);
	void parseClientList(NetworkMessage& message);
	void parseCommentList(NetworkMessage& message);
	void parseComment(NetworkMessage& message);
	void parseCommentRemoved(NetworkMessage& message);
	void parseStartOperation(NetworkMessage& message);
	void parseUpdateOperation(NetworkMessage& message);
	void parseItemBlockList(NetworkMessage& message);
	void showBlockedItemWarning(uint16_t itemId);

	// --- editor version probing ---
	// A server refuses any client whose editor version is not byte-for-byte its own,
	// and its kick packet does not say which version it wants. When the user allows
	// it we walk versions downwards from ours, reconnecting once per candidate, until
	// one is accepted. Only the announced editor version changes between attempts --
	// __LIVE_NET_VERSION__, which is what actually governs packet layout, is always
	// reported truthfully, so a genuinely incompatible server still refuses us.
	void initVersionProbe();
	// Records that this attempt was refused and starts the next one. Reports from a
	// stale generation (the kick packet and the socket EOF both arrive) are ignored.
	void onProbeFailure(uint32_t generation, const wxString& reason);
	// Steps to the next candidate, or gives up if the search is exhausted.
	void advanceProbe(const wxString& reason);
	// Tears the session down reporting a refusal the probe could not get past.
	void abortAfterRefusal(const wxString& reason);
	// Asks the user to accept a build mismatch before we connect to a server that is
	// demonstrably not our version. GUI thread only; asked at most once per session.
	bool confirmVersionMismatch();
	void scheduleNextProbe();
	// Stops probing because the server answered -- either it accepted us, or it
	// refused us for a reason another version cannot fix.
	void settleVersionProbe(bool accepted);

	bool versionProbeEnabled = false;
	bool versionProbeSettled = false;
	bool versionMismatchPrompted = false;
	// True once a hello reached the wire for the current attempt. Failures before
	// that (DNS, refused connection) are not version problems and must not consume
	// a candidate.
	bool helloSent = false;
	uint32_t probeVersionId = 0;
	uint32_t probeStartVersionId = 0;
	uint32_t probeAttempt = 0;
	uint32_t probeLimit = 0;
	uint32_t probeMaxSubversion = 0;
	uint32_t probeGeneration = 0;
	std::shared_ptr<asio::steady_timer> probeTimer;

	//
	NetworkMessage readMessage;

	// asio forbids overlapping async_writes on one socket; queue sends so only one
	// write is in flight at a time, otherwise the bytes interleave and corrupt the
	// stream. The completion handler drains the queue in order.
	struct OutboundPacket {
		std::shared_ptr<NetworkMessage> message;
		std::function<void()> onSent;
	};
	std::deque<OutboundPacket> writeQueue;
	bool writing = false;

	std::set<uint32_t> queryNodeList;
	std::map<uint32_t, std::chrono::steady_clock::time_point> pendingNodeRequests;
	std::chrono::steady_clock::time_point lastNodeRequestTick {};
	std::unique_ptr<wxTimer> nodeRetryTimer;
	std::unique_ptr<wxTimer> keepAliveTimer;
	// Throttles the "malformed packet" log so a persistently bad stream can't spam.
	bool warnedMalformedStream = false;
	bool viewportRefreshPending;
	bool hasPendingCommentList;
	std::vector<MapComment> pendingCommentList;
	wxString currentOperation;

	std::shared_ptr<asio::ip::tcp::resolver> resolver;
	std::shared_ptr<asio::ip::tcp::socket> socket;

	std::string connectionAddress;
	uint16_t connectionPort;

	Editor* editor;

	std::set<uint16_t> blockedItemIds;
	std::set<uint16_t> dismissedBlockedWarnings;

	wxColor ownClientColor;
	bool stopped;
	// Guards closeAndTeardown() against running twice (e.g. the user closing
	// the tab while a network error is also tearing the session down).
	bool teardownInitiated = false;

	ClientVersionID pendingVersionId;
	std::vector<LiveAssetFile> pendingAssetManifest;
	LiveAssetReceiveState assetReceiveState;
	bool ignoreIncomingAssets;
	bool waitingForServerAssets;
	uint64_t assetBytesExpected;
	uint64_t assetBytesReceived;
	int assetProgressReported;

	LiveUpdateReceiveState updateReceiveState;
	uint64_t updateBytesExpected;
	uint64_t updateBytesReceived;
	int updateProgressReported;
	bool receivingUpdate;
};

#endif
