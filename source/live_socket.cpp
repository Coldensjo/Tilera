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
#include "live_socket.h"
#include "live_tab.h"
#include "map_region.h"
#include "iomap_otbm.h"
#include "editor.h"
#include "map.h"
#include "tile.h"
#include "item.h"
#include "creature.h"
#include "creatures.h"
#include "spawn.h"
#include "settings.h"

#include <wx/tokenzr.h>

#include <iostream>
#include <algorithm>

bool isLiveMarkerVisibleOnFloor(int viewerFloor, int markerFloor) {
	const bool viewerUnderground = viewerFloor > GROUND_LAYER;
	const bool markerUnderground = markerFloor > GROUND_LAYER;
	return viewerUnderground == markerUnderground;
}

bool parseEditorVersionId(const wxString& text, uint32_t& versionId) {
	const wxString trimmed = wxString(text).Trim(true).Trim(false);
	if (trimmed.empty()) {
		return false;
	}

	// A bare number large enough to be an ID is taken verbatim, so a server
	// operator can hand out the exact value their build reports.
	unsigned long single = 0;
	if (trimmed.ToULong(&single) && single >= 100000) {
		versionId = static_cast<uint32_t>(single);
		return true;
	}

	unsigned long parts[3] = { 0, 0, 0 };
	size_t partIndex = 0;
	wxStringTokenizer tokenizer(trimmed, ".");
	while (tokenizer.HasMoreTokens()) {
		if (partIndex >= 3) {
			return false; // more components than major.minor.subversion
		}

		const wxString token = tokenizer.GetNextToken().Trim(true).Trim(false);
		if (token.empty() || !token.ToULong(&parts[partIndex]) || parts[partIndex] > 99) {
			return false;
		}
		++partIndex;
	}

	if (partIndex == 0) {
		return false;
	}

	versionId = MAKE_VERSION_ID(
		static_cast<uint32_t>(parts[0]),
		static_cast<uint32_t>(parts[1]),
		static_cast<uint32_t>(parts[2])
	);
	return true;
}

wxString formatEditorVersionId(uint32_t versionId) {
	const uint32_t major = versionId / 10000000;
	const uint32_t minor = (versionId / 100000) % 100;
	const uint32_t subversion = (versionId / 1000) % 100;
	return wxString() << major << "." << minor << "." << subversion;
}

uint32_t previousEditorVersionId(uint32_t versionId, uint32_t maxSubversion) {
	if (versionId < 1000) {
		return 0;
	}

	const uint32_t subversion = (versionId / 1000) % 100;
	if (subversion > 0) {
		// Coming down from a subversion above the ceiling (the user pinned one, or
		// it is our own build's) lands on the ceiling rather than stepping by one.
		const uint32_t next = std::min(subversion - 1, std::min(maxSubversion, 99u));
		return versionId - (subversion - next) * 1000;
	}

	// Subversion 0 reached -- drop into the previous minor at the ceiling.
	if (versionId < 100000) {
		return 0;
	}
	return versionId - 100000 + std::min(maxSubversion, 99u) * 1000;
}

namespace {
constexpr std::chrono::milliseconds LIVE_CURSOR_SEND_INTERVAL{50};
}

LiveSocket::LiveSocket() :
	cursors(), participants(), ownClientId(0), mapReader(nullptr, 0), mapWriter(),
	mapVersion(MapVersion(MAP_OTBM_4, CLIENT_VERSION_NONE)), log(nullptr),
	name("User"), password(""), lastSentCursorPos(), lastSentCursorTime {},
	hasLastSentCursor(false) {
	//
}

bool LiveSocket::shouldSendCursorUpdate(const Position& position) {
	const auto now = std::chrono::steady_clock::now();
	if (hasLastSentCursor && lastSentCursorPos == position &&
	    now - lastSentCursorTime < LIVE_CURSOR_SEND_INTERVAL) {
		return false;
	}
	if (hasLastSentCursor && now - lastSentCursorTime < LIVE_CURSOR_SEND_INTERVAL) {
		return false;
	}
	lastSentCursorPos = position;
	lastSentCursorTime = now;
	hasLastSentCursor = true;
	return true;
}

LiveSocket::~LiveSocket() {
	//
}

wxString LiveSocket::getName() const {
	return name;
}

bool LiveSocket::setName(const wxString& newName) {
	if (newName.empty()) {
		setLastError("Must provide a name.");
		return false;
	} else if (newName.length() > 32) {
		setLastError("Name is too long.");
		return false;
	}
	name = newName;
	return true;
}

wxString LiveSocket::getPassword() const {
	return password;
}

bool LiveSocket::setPassword(const wxString& newPassword) {
	if (newPassword.length() > 32) {
		setLastError("Password is too long.");
		return false;
	}
	password = newPassword;
	return true;
}

wxString LiveSocket::getLastError() const {
	return lastError;
}

void LiveSocket::setLastError(const wxString& error) {
	lastError = error;
}

std::string LiveSocket::getHostName() const {
	return "?";
}

std::vector<LiveCursor> LiveSocket::getCursorList() const {
	std::vector<LiveCursor> cursorList;
	for (auto& cursorEntry : cursors) {
		cursorList.push_back(cursorEntry.second);
	}
	return cursorList;
}

bool LiveSocket::getCursorPosition(uint32_t clientId, Position& position) const {
	const auto cursorEntry = cursors.find(clientId);
	if (cursorEntry == cursors.end()) {
		return false;
	}
	position = cursorEntry->second.pos;
	return true;
}

void LiveSocket::logMessage(const wxString& message) {
	std::cout << "[live] " << message.ToStdString() << std::endl;
	std::cout.flush();

	if (wxThread::IsMain()) {
		if (log) {
			log->Message(message);
		} else {
			pendingLogMessages.push_back(message);
		}
		return;
	}

	wxTheApp->CallAfter([this, message]() {
		if (log) {
			log->Message(message);
		} else {
			pendingLogMessages.push_back(message);
		}
	});
}

void LiveSocket::receiveNode(NetworkMessage& message, Editor& editor, Action* action, int32_t ndx, int32_t ndy, bool underground) {
	Map& map = editor.getMap();
	QTreeNode* node = map.getLeaf(ndx * 4, ndy * 4);
	if (!node) {
		node = map.createLeaf(ndx * 4, ndy * 4);
	}
	if (!node) {
		if (log) {
			log->Message("Warning: Received update for unknown tile (" + std::to_string(ndx * 4) + "/" + std::to_string(ndy * 4) + "/" + (underground ? "true" : "false") + ")");
		}
		return;
	}

	node->setRequested(underground, false);
	node->setVisible(underground, true);

	uint16_t floorBits = message.read<uint16_t>();
	if (floorBits == 0) {
		return;
	}

	for (uint_fast8_t z = 0; z < 16; ++z) {
		if (testFlags(floorBits, static_cast<uint64_t>(1) << z)) {
			receiveFloor(message, editor, action, ndx, ndy, z, node, node->getFloor(z));
		}
	}
}

void LiveSocket::appendNode(NetworkMessage& message, uint32_t clientId, QTreeNode* node, int32_t ndx, int32_t ndy, uint32_t floorMask) {
	bool underground;
	if (floorMask & 0xFF00) {
		if (floorMask & 0x00FF) {
			underground = false;
		} else {
			underground = true;
		}
	} else {
		underground = false;
	}

	if (node) {
		node->setVisible(clientId, underground, true);
	}

	message.write<uint8_t>(PACKET_NODE);
	message.write<uint32_t>((ndx << 18) | (ndy << 4) | ((floorMask & 0xFF00) ? 1 : 0));

	if (!node) {
		message.write<uint16_t>(0);
		return;
	}

	Floor** floors = node->getFloors();

	uint16_t sendMask = 0;
	for (uint32_t z = 0; z < 16; ++z) {
		const uint32_t bit = 1u << z;
		if (floors[z] && testFlags(floorMask, bit)) {
			sendMask |= bit;
		}
	}

	message.write<uint16_t>(sendMask);
	for (uint32_t z = 0; z < 16; ++z) {
		if (testFlags(sendMask, static_cast<uint64_t>(1) << z)) {
			sendFloor(message, floors[z]);
		}
	}
}

void LiveSocket::sendNode(uint32_t clientId, QTreeNode* node, int32_t ndx, int32_t ndy, uint32_t floorMask) {
	NetworkMessage message;
	appendNode(message, clientId, node, ndx, ndy, floorMask);
	send(message);
}

void LiveSocket::receiveFloor(NetworkMessage& message, Editor& editor, Action* action, int32_t ndx, int32_t ndy, int32_t z, QTreeNode* node, Floor* floor) {
	Map& map = editor.getMap();

	uint16_t tileBits = message.read<uint16_t>();
	if (tileBits == 0) {
		for (uint_fast8_t x = 0; x < 4; ++x) {
			for (uint_fast8_t y = 0; y < 4; ++y) {
				action->addChange(newd Change(map.allocator(node->createTile(ndx * 4 + x, ndy * 4 + y, z))));
			}
		}
		return;
	}

	// -1 on address since we skip the first START_NODE when sending
	const std::string& data = message.read<std::string>();
	mapReader.assign(reinterpret_cast<const uint8_t*>(data.c_str() - 1), data.size());

	BinaryNode* rootNode = mapReader.getRootNode();
	BinaryNode* tileNode = rootNode->getChild();

	Position position(0, 0, z);
	for (uint_fast8_t x = 0; x < 4; ++x) {
		for (uint_fast8_t y = 0; y < 4; ++y) {
			position.x = (ndx * 4) + x;
			position.y = (ndy * 4) + y;

			if (testFlags(tileBits, static_cast<uint64_t>(1) << ((x * 4) + y))) {
				receiveTile(tileNode, editor, action, &position);
				tileNode->advance();
			} else {
				action->addChange(newd Change(map.allocator(node->createTile(position.x, position.y, z))));
			}
		}
	}
	mapReader.close();
}

void LiveSocket::sendFloor(NetworkMessage& message, Floor* floor) {
	uint16_t tileBits = 0;
	for (uint_fast8_t x = 0; x < 4; ++x) {
		for (uint_fast8_t y = 0; y < 4; ++y) {
			uint_fast8_t index = (x * 4) + y;

			Tile* tile = floor->locs[index].get();
			if (tile && tile->size() > 0) {
				tileBits |= (1 << index);
			}
		}
	}

	message.write<uint16_t>(tileBits);
	if (tileBits == 0) {
		return;
	}

	mapWriter.reset();
	for (uint_fast8_t x = 0; x < 4; ++x) {
		for (uint_fast8_t y = 0; y < 4; ++y) {
			uint_fast8_t index = (x * 4) + y;
			if (testFlags(tileBits, static_cast<uint64_t>(1) << index)) {
				sendTile(mapWriter, floor->locs[index].get(), nullptr);
			}
		}
	}
	mapWriter.endNode();

	std::string stream(
		reinterpret_cast<char*>(mapWriter.getMemory()),
		mapWriter.getSize()
	);
	message.write<std::string>(stream);
}

void LiveSocket::receiveTile(BinaryNode* node, Editor& editor, Action* action, const Position* position) {
	ASSERT(node != nullptr);

	Tile* tile = readTile(node, editor, position);
	if (tile) {
		action->addChange(newd Change(tile));
	}
}

void LiveSocket::sendTile(MemoryNodeFileWriteHandle& writer, Tile* tile, const Position* position) {
	writer.addNode(tile->isHouseTile() ? OTBM_HOUSETILE : OTBM_TILE);
	if (position) {
		writer.addU16(position->x);
		writer.addU16(position->y);
		writer.addU8(position->z);
	}

	if (tile->isHouseTile()) {
		writer.addU32(tile->getHouseID());
	}

	if (tile->getMapFlags()) {
		writer.addByte(OTBM_ATTR_TILE_FLAGS);
		writer.addU32(tile->getMapFlags());
	}

	Item* ground = tile->ground;
	if (ground) {
		if (ground->isComplex()) {
			ground->serializeItemNode_OTBM(mapVersion, writer);
		} else {
			writer.addByte(OTBM_ATTR_ITEM);
			ground->serializeItemCompact_OTBM(mapVersion, writer);
		}
	}

	for (Item* item : tile->items) {
		item->serializeItemNode_OTBM(mapVersion, writer);
	}

	if (tile->creature) {
		Creature* creature = tile->creature;
		writer.addNode(OTBM_MONSTER);
		writer.addString(creature->getName());
		writer.addU32(creature->getSpawnTime());
		writer.addU8(static_cast<uint8_t>(creature->getDirection()));
		writer.addU8(creature->isNpc() ? 1 : 0);
		writer.endNode();
	}

	if (tile->spawn) {
		writer.addNode(OTBM_SPAWN_AREA);
		writer.addU32(tile->spawn->getSize());
		writer.endNode();
	}

	writer.endNode();
}

Tile* LiveSocket::readTile(BinaryNode* node, Editor& editor, const Position* position) {
	ASSERT(node != nullptr);

	Map& map = editor.getMap();

	uint8_t tileType;
	node->getByte(tileType);

	if (tileType != OTBM_TILE && tileType != OTBM_HOUSETILE) {
		return nullptr;
	}

	Position pos;
	if (position) {
		pos = *position;
	} else {
		uint16_t x;
		node->getU16(x);
		pos.x = x;
		uint16_t y;
		node->getU16(y);
		pos.y = y;
		uint8_t z;
		node->getU8(z);
		pos.z = z;
	}

	Tile* tile = map.allocator(
		map.createTileL(pos)
	);

	if (tileType == OTBM_HOUSETILE) {
		uint32_t houseId;
		if (!node->getU32(houseId)) {
			delete tile;
			return nullptr;
		}

		if (houseId) {
			House* house = map.houses.getHouse(houseId);
			if (house) {
				tile->setHouse(house);
			}
		}
	}

	uint8_t attribute;
	while (node->getU8(attribute)) {
		switch (attribute) {
			case OTBM_ATTR_TILE_FLAGS: {
				uint32_t flags = 0;
				node->getU32(flags);
				tile->setMapFlags(flags);
				break;
			}
			case OTBM_ATTR_ITEM: {
				Item* item = Item::Create_OTBM(mapVersion, node);
				if (item) {
					tile->addItem(item);
				}
				break;
			}
			default:
				break;
		}
	}

	BinaryNode* itemNode = node->getChild();
	if (itemNode) {
		do {
			uint8_t itemType;
			if (!itemNode->getByte(itemType)) {
				delete tile;
				return nullptr;
			}

			if (itemType == OTBM_ITEM) {
				Item* item = Item::Create_OTBM(mapVersion, itemNode);
				if (item) {
					item->unserializeItemNode_OTBM(mapVersion, itemNode);
					tile->addItem(item);
				}
			} else if (itemType == OTBM_MONSTER) {
				std::string name;
				if (!itemNode->getString(name)) {
					delete tile;
					return nullptr;
				}

				uint32_t spawntime = g_settings.getInteger(Config::DEFAULT_SPAWNTIME);
				itemNode->getU32(spawntime);

				uint8_t direction = SOUTH;
				itemNode->getU8(direction);

				uint8_t isNpc = 0;
				itemNode->getU8(isNpc);

				CreatureType* type = g_creatures[name];
				if (!type) {
					type = g_creatures.addMissingCreatureType(name, isNpc != 0);
				}

				if (tile->creature) {
					delete tile->creature;
				}
				tile->creature = newd Creature(type);
				tile->creature->setSpawnTime(spawntime);
				tile->creature->setDirection(static_cast<Direction>(direction));
			} else if (itemType == OTBM_SPAWN_AREA) {
				uint32_t radius = 3;
				if (!itemNode->getU32(radius)) {
					delete tile;
					return nullptr;
				}

				if (tile->spawn) {
					delete tile->spawn;
				}
				tile->spawn = newd Spawn(radius);
			}
		} while (itemNode->advance());
	}

	return tile;
}

LiveCursor LiveSocket::readCursor(NetworkMessage& message) {
	LiveCursor cursor;
	cursor.id = message.read<uint32_t>();

	uint8_t r = message.read<uint8_t>();
	uint8_t g = message.read<uint8_t>();
	uint8_t b = message.read<uint8_t>();
	uint8_t a = message.read<uint8_t>();
	cursor.color = wxColor(r, g, b, a);

	cursor.pos = message.read<Position>();
	return cursor;
}

void LiveSocket::writeCursor(NetworkMessage& message, const LiveCursor& cursor) {
	message.write<uint32_t>(cursor.id);
	message.write<uint8_t>(cursor.color.Red());
	message.write<uint8_t>(cursor.color.Green());
	message.write<uint8_t>(cursor.color.Blue());
	message.write<uint8_t>(cursor.color.Alpha());
	message.write<Position>(cursor.pos);
}

namespace {
constexpr std::chrono::milliseconds LIVE_PING_DURATION{4000};
}

void LiveSocket::addPing(const LivePing& ping) {
	ActiveLivePing active;
	active.ping = ping;
	active.startTime = std::chrono::steady_clock::now();
	activePings.push_back(active);
}

void LiveSocket::pruneExpiredPings() {
	const auto now = std::chrono::steady_clock::now();
	activePings.erase(
		std::remove_if(activePings.begin(), activePings.end(), [&](const ActiveLivePing& active) {
			return now - active.startTime >= LIVE_PING_DURATION;
		}),
		activePings.end()
	);
}

bool LiveSocket::hasActivePings() {
	pruneExpiredPings();
	return !activePings.empty();
}

LivePing LiveSocket::readPing(NetworkMessage& message) {
	LivePing ping;
	ping.id = message.read<uint32_t>();

	uint8_t r = message.read<uint8_t>();
	uint8_t g = message.read<uint8_t>();
	uint8_t b = message.read<uint8_t>();
	uint8_t a = message.read<uint8_t>();
	ping.color = wxColor(r, g, b, a);

	ping.pos = message.read<Position>();
	return ping;
}

void LiveSocket::writePing(NetworkMessage& message, const LivePing& ping) {
	message.write<uint32_t>(ping.id);
	message.write<uint8_t>(ping.color.Red());
	message.write<uint8_t>(ping.color.Green());
	message.write<uint8_t>(ping.color.Blue());
	message.write<uint8_t>(ping.color.Alpha());
	message.write<Position>(ping.pos);
}

wxColor LiveSocket::colorForClientId(uint32_t clientId) {
	static const wxColor palette[] = {
		wxColor(235, 70, 70),
		wxColor(70, 140, 255),
		wxColor(245, 190, 45),
		wxColor(170, 80, 245),
		wxColor(65, 195, 115),
		wxColor(245, 110, 195),
		wxColor(110, 210, 210),
		wxColor(215, 115, 65),
		wxColor(190, 190, 75),
		wxColor(130, 130, 220),
		wxColor(255, 130, 130),
		wxColor(130, 255, 170),
		wxColor(255, 170, 90),
		wxColor(150, 90, 255),
		wxColor(90, 220, 255),
		wxColor(220, 90, 160),
	};

	int index = 0;
	for (uint32_t id = clientId; id > 1; id >>= 1) {
		++index;
	}
	return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

void LiveSocket::writeParticipantList(NetworkMessage& message) const {
	message.write<uint32_t>(static_cast<uint32_t>(participants.size()));
	for (const LiveParticipant& participant : participants) {
		message.write<uint32_t>(participant.id);
		message.write<std::string>(nstr(participant.name));
		message.write<uint8_t>(participant.color.Red());
		message.write<uint8_t>(participant.color.Green());
		message.write<uint8_t>(participant.color.Blue());
		message.write<uint8_t>(participant.color.Alpha());
	}
}

void LiveSocket::readParticipantList(NetworkMessage& message) {
	participants.clear();
	const uint32_t count = message.read<uint32_t>();
	participants.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		LiveParticipant participant;
		participant.id = message.read<uint32_t>();
		participant.name = wxstr(message.read<std::string>());
		const uint8_t r = message.read<uint8_t>();
		const uint8_t g = message.read<uint8_t>();
		const uint8_t b = message.read<uint8_t>();
		const uint8_t a = message.read<uint8_t>();
		participant.color = wxColor(r, g, b, a);
		participants.push_back(participant);
	}
}

void LiveSocket::writeSessionBounds(NetworkMessage& message) const {
	message.write<uint8_t>(sessionBounds.enabled ? 1 : 0);
	if (sessionBounds.enabled) {
		message.write<uint16_t>(sessionBounds.centerX);
		message.write<uint16_t>(sessionBounds.centerY);
		message.write<uint8_t>(sessionBounds.centerZ);
		message.write<uint32_t>(sessionBounds.radius);
	}
}

void LiveSocket::readSessionBounds(NetworkMessage& message) {
	sessionBounds.enabled = message.read<uint8_t>() != 0;
	if (sessionBounds.enabled) {
		sessionBounds.centerX = message.read<uint16_t>();
		sessionBounds.centerY = message.read<uint16_t>();
		sessionBounds.centerZ = message.read<uint8_t>();
		sessionBounds.radius = message.read<uint32_t>();
	}
}

void LiveSocket::writeMapComment(NetworkMessage& message, const MapComment& comment) const {
	message.write<uint32_t>(comment.id);
	message.write<uint16_t>(static_cast<uint16_t>(comment.pos.x));
	message.write<uint16_t>(static_cast<uint16_t>(comment.pos.y));
	message.write<uint8_t>(static_cast<uint8_t>(comment.pos.z));
	message.write<std::string>(comment.author);
	message.write<int64_t>(comment.createdUnix);
	message.write<std::string>(comment.text);
}

MapComment LiveSocket::readMapComment(NetworkMessage& message) {
	MapComment comment;
	comment.id = message.read<uint32_t>();
	comment.pos.x = message.read<uint16_t>();
	comment.pos.y = message.read<uint16_t>();
	comment.pos.z = message.read<uint8_t>();
	comment.author = message.read<std::string>();
	comment.createdUnix = message.read<int64_t>();
	comment.text = message.read<std::string>();
	return comment;
}
