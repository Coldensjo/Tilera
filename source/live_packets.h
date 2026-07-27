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

#ifndef RME_LIVE_PACKETS_H
#define RME_LIVE_PACKETS_H

enum LivePacketType {
	PACKET_HELLO_FROM_CLIENT = 0x10,
	PACKET_READY_CLIENT = 0x11,

	PACKET_REQUEST_NODES = 0x20,
	PACKET_CHANGE_LIST = 0x21,
	PACKET_COMMENT_ADD = 0x22,
	PACKET_COMMENT_REMOVE = 0x23,
	PACKET_CLIENT_PING = 0x24,
	PACKET_COMMENT_EDIT = 0x25,
	PACKET_CLIENT_KEEPALIVE = 0x26,

	PACKET_CLIENT_UPDATE_CURSOR = 0x31,
	PACKET_CLIENT_UPDATE_COLOR = 0x32,

	PACKET_HELLO_FROM_SERVER = 0x80,
	PACKET_KICK = 0x81,
	PACKET_ACCEPTED_CLIENT = 0x82,
	PACKET_CHANGE_CLIENT_VERSION = 0x83,
	PACKET_ASSET_FILE_BEGIN = 0x84,
	PACKET_ASSET_FILE_CHUNK = 0x85,
	PACKET_ASSET_FILE_END = 0x86,
	PACKET_ASSET_FILES_DONE = 0x87,

	// Editor auto-update (server pushes a newer editor build to outdated clients).
	PACKET_UPDATE_AVAILABLE = 0x88,
	PACKET_UPDATE_FILE_BEGIN = 0x89,
	PACKET_UPDATE_FILE_CHUNK = 0x8A,
	PACKET_UPDATE_FILE_END = 0x8B,
	PACKET_UPDATE_DONE = 0x8C,

	PACKET_NODE = 0x90,
	PACKET_CURSOR_UPDATE = 0x91,
	PACKET_START_OPERATION = 0x92,
	PACKET_UPDATE_OPERATION = 0x93,
	PACKET_CLIENT_LIST = 0x94,
	PACKET_COMMENT_LIST = 0x95,
	PACKET_COMMENT = 0x96,
	PACKET_COMMENT_REMOVED = 0x97,
	PACKET_PING = 0x98,
	PACKET_ITEM_BLOCK_LIST = 0x99,
};

#endif
