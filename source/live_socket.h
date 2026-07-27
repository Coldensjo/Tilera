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

#ifndef RME_LIVE_SOCKET_H_
#define RME_LIVE_SOCKET_H_

#include "position.h"
#include "net_connection.h"
#include "live_packets.h"
#include "live_session_bounds.h"
#include "map_comment.h"
#include "filehandle.h"
#include "iomap.h"

#include <memory>
#include <unordered_map>
#include <chrono>
#include <vector>

class Editor;
class LiveLogTab;
class LiveServer;
class LivePeer;
class Action;
class QTreeNode;
class Floor;
class Tile;

// True when a live marker on markerFloor should be visible to a viewer on viewerFloor.
// Above-ground and underground are isolated; any floor within the same realm is visible.
bool isLiveMarkerVisibleOnFloor(int viewerFloor, int markerFloor);

// Parse a user-supplied editor version into the ID sent in the live hello packet.
// Accepts "major.minor.subversion" (missing trailing parts default to 0) or a raw
// numeric ID as produced by MAKE_VERSION_ID. Returns false if the text cannot be
// parsed; an empty/whitespace-only string is not a valid version.
bool parseEditorVersionId(const wxString& text, uint32_t& versionId);

// Render a version ID back as "major.minor.subversion" for logs and dialogs.
wxString formatEditorVersionId(uint32_t versionId);

// The next lower version ID below the given one, or 0 when there is none.
// Steps down one subversion at a time, rolling into the previous minor at
// maxSubversion once subversion 0 is passed. The subversion field can encode up to
// 99, but releases never climb anywhere near that before the minor is bumped, so
// the ceiling keeps a downward search from burning its budget on versions that
// cannot exist.
uint32_t previousEditorVersionId(uint32_t versionId, uint32_t maxSubversion);

struct LiveCursor {
	uint32_t id;
	wxColor color;
	Position pos;
};

struct LivePing {
	uint32_t id;
	wxColor color;
	Position pos;
};

struct ActiveLivePing {
	LivePing ping;
	std::chrono::steady_clock::time_point startTime;
};

struct LiveParticipant {
	uint32_t id;
	wxString name;
	wxColor color;
};

class LiveSocket {
public:
	LiveSocket();
	virtual ~LiveSocket();

	//
	wxString getName() const;
	bool setName(const wxString& newName);

	wxString getPassword() const;
	bool setPassword(const wxString& newPassword);

	wxString getLastError() const;
	void setLastError(const wxString& error);

	virtual std::string getHostName() const;
	std::vector<LiveCursor> getCursorList() const;
	bool getCursorPosition(uint32_t clientId, Position& position) const;
	const std::vector<LiveParticipant>& getParticipantList() const {
		return participants;
	}
	uint32_t getOwnClientId() const {
		return ownClientId;
	}

	const LiveSessionBounds& getSessionBounds() const {
		return sessionBounds;
	}
	void setSessionBounds(const LiveSessionBounds& bounds) {
		sessionBounds = bounds;
	}

	//
	void logMessage(const wxString& message);

	//
	virtual void receiveHeader() = 0;
	virtual void receive(uint32_t packetSize) = 0;
	virtual void send(NetworkMessage& message) = 0;

	//
	virtual void updateCursor(const Position& position) = 0;

	bool shouldSendCursorUpdate(const Position& position);

	void addPing(const LivePing& ping);
	bool hasActivePings();
	const std::vector<ActiveLivePing>& getActivePings() const {
		return activePings;
	}

	void writeSessionBounds(NetworkMessage& message) const;
	void readSessionBounds(NetworkMessage& message);
	void writeMapComment(NetworkMessage& message, const MapComment& comment) const;
	MapComment readMapComment(NetworkMessage& message);

protected:
	// receive / send methods
	void receiveNode(NetworkMessage& message, Editor& editor, Action* action, int32_t ndx, int32_t ndy, bool underground);
	void appendNode(NetworkMessage& message, uint32_t clientId, QTreeNode* node, int32_t ndx, int32_t ndy, uint32_t floorMask);
	void sendNode(uint32_t clientId, QTreeNode* node, int32_t ndx, int32_t ndy, uint32_t floorMask);

	void receiveFloor(NetworkMessage& message, Editor& editor, Action* action, int32_t ndx, int32_t ndy, int32_t z, QTreeNode* node, Floor* floor);
	void sendFloor(NetworkMessage& message, Floor* floor);

	void receiveTile(BinaryNode* node, Editor& editor, Action* action, const Position* position);
	void sendTile(MemoryNodeFileWriteHandle& writer, Tile* tile, const Position* position);

	// read / write types
	Tile* readTile(BinaryNode* node, Editor& editor, const Position* position);

	LiveCursor readCursor(NetworkMessage& message);
	void writeCursor(NetworkMessage& message, const LiveCursor& cursor);
	LivePing readPing(NetworkMessage& message);
	void writePing(NetworkMessage& message, const LivePing& ping);
	void writeParticipantList(NetworkMessage& message) const;
	void readParticipantList(NetworkMessage& message);

	static wxColor colorForClientId(uint32_t clientId);

	void pruneExpiredPings();

	//
	std::unordered_map<uint32_t, LiveCursor> cursors;
	std::vector<ActiveLivePing> activePings;
	std::vector<LiveParticipant> participants;
	uint32_t ownClientId;

	MemoryNodeFileReadHandle mapReader;
	MemoryNodeFileWriteHandle mapWriter;
	VirtualIOMap mapVersion;

	LiveLogTab* log;

	std::vector<wxString> pendingLogMessages;

	wxString name;
	wxString password;
	wxString lastError;

	LiveSessionBounds sessionBounds;

	Position lastSentCursorPos;
	std::chrono::steady_clock::time_point lastSentCursorTime {};
	bool hasLastSentCursor = false;

	friend class LiveLogTab;
	friend class LiveServer;
	friend class LivePeer;
};

#endif
