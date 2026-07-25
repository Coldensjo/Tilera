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

#include "carpet_brush.h"

#include "basemap.h"
#include "brush.h"
#include "ground_brush.h"
#include "items.h"

//=============================================================================
// Carpet brush

uint32_t CarpetBrush::carpet_types[256];

CarpetBrush::CarpetBrush() :
	look_id(0),
	hate_friends(false) {
	////
}

CarpetBrush::~CarpetBrush() {
	////
}

bool CarpetBrush::load(pugi::xml_node node, wxArrayString& warnings) {
	pugi::xml_attribute attribute;
	if ((attribute = node.attribute("lookid"))) {
		look_id = attribute.as_ushort();
	}

	if ((attribute = node.attribute("server_lookid"))) {
		look_id = g_items[attribute.as_ushort()].clientID;
	}

	for (pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
		const std::string& childName = as_lower_str(childNode.name());
		if (childName == "friend") {
			const std::string& name = childNode.attribute("name").as_string();
			if (!name.empty()) {
				if (name == "all") {
					friends.push_back(0xFFFFFFFF);
				} else {
					Brush* brush = g_brushes.getBrush(name);
					if (brush && brush->isGround()) {
						friends.push_back(brush->getID());
					} else {
						warnings.push_back("Brush '" + wxstr(name) + "' is not a ground brush.");
					}
				}
			}
			hate_friends = false;
			continue;
		} else if (childName == "enemy") {
			const std::string& name = childNode.attribute("name").as_string();
			if (!name.empty()) {
				if (name == "all") {
					friends.push_back(0xFFFFFFFF);
				} else {
					Brush* brush = g_brushes.getBrush(name);
					if (brush && brush->isGround()) {
						friends.push_back(brush->getID());
					} else {
						warnings.push_back("Brush '" + wxstr(name) + "' is not a ground brush.");
					}
				}
			}
			hate_friends = true;
			continue;
		} else if (childName != "carpet") {
			continue;
		}

		uint32_t alignment;
		if ((attribute = childNode.attribute("align"))) {
			const std::string& alignString = attribute.as_string();
			alignment = AutoBorder::edgeNameToID(alignString);
			if (alignment == BORDER_NONE) {
				if (alignString == "center") {
					alignment = CARPET_CENTER;
				} else {
					warnings.push_back("Invalid alignment of carpet node\n");
					continue;
				}
			}
		} else {
			warnings.push_back("Could not read alignment tag of carpet node\n");
			continue;
		}

		bool use_local_id = true;
		for (pugi::xml_node subChildNode = childNode.first_child(); subChildNode; subChildNode = subChildNode.next_sibling()) {
			if (as_lower_str(subChildNode.name()) != "item") {
				continue;
			}

			use_local_id = false;
			if (!(attribute = subChildNode.attribute("id"))) {
				warnings.push_back("Could not read id tag of item node\n");
				continue;
			}

			int32_t id = attribute.as_int();
			if (!(attribute = subChildNode.attribute("chance"))) {
				warnings.push_back("Could not read chance tag of item node\n");
				continue;
			}

			int32_t chance = attribute.as_int();

			ItemType& it = g_items[id];
			if (it.id == 0) {
				warnings.push_back("There is no itemtype with id " + std::to_string(id));
				continue;
			} else if (it.brush && it.brush != this) {
				warnings.push_back("Itemtype id " + std::to_string(id) + " already has a brush");
				continue;
			}

			it.isCarpet = true;
			it.brush = this;

			auto& alignItem = carpet_items[alignment];
			alignItem.total_chance += chance;

			CarpetType t;
			t.id = id;
			t.chance = chance;

			alignItem.items.push_back(t);
		}

		if (use_local_id) {
			if (!(attribute = childNode.attribute("id"))) {
				warnings.push_back("Could not read id tag of carpet node\n");
				continue;
			}

			uint16_t id = attribute.as_ushort();

			ItemType& it = g_items[id];
			if (it.id == 0) {
				warnings.push_back("There is no itemtype with id " + std::to_string(id));
				return false;
			} else if (it.brush && it.brush != this) {
				warnings.push_back("Itemtype id " + std::to_string(id) + " already has a brush");
				return false;
			}

			it.isCarpet = true;
			it.brush = this;

			auto& alignItem = carpet_items[alignment];
			alignItem.total_chance = 1;

			CarpetType carpetType;
			carpetType.id = id;
			carpetType.chance = 1;

			alignItem.items.push_back(carpetType);
		}
	}
	return true;
}

bool CarpetBrush::canDraw(BaseMap* map, const Position& position) const {
	return true;
}

bool CarpetBrush::hasFriendGround(const Tile* tile) const {
	if (!tile || friends.empty()) {
		return false;
	}

	GroundBrush* ground = tile->getGroundBrush();
	if (!ground) {
		return hate_friends;
	}

	for (uint32_t friendId : friends) {
		if (friendId == ground->getID() || friendId == 0xFFFFFFFF) {
			return !hate_friends;
		}
	}
	return hate_friends;
}

bool CarpetBrush::hasMatchingNeighbour(BaseMap* map, uint32_t x, uint32_t y, uint32_t z) const {
	Tile* tile = map->getTile(x, y, z);
	if (!tile) {
		return false;
	}

	for (Item* item : tile->items) {
		if (item->getCarpetBrush() == this) {
			return true;
		}
	}

	return hasFriendGround(tile);
}

void CarpetBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	undraw(map, tile); // Remove old
	if (hasFriendGround(tile)) {
		// Matching ground already carries these borders via automagic.
		return;
	}
	tile->addItem(Item::Create(getRandomCarpet(CARPET_CENTER)));
}

void CarpetBrush::undraw(BaseMap* map, Tile* tile) {
	auto& items = tile->items;
	for (auto it = items.begin(); it != items.end();) {
		Item* item = *it;
		if (item->isCarpet()) {
			CarpetBrush* carpetBrush = item->getCarpetBrush();
			if (carpetBrush) {
				delete item;
				it = items.erase(it);
			} else {
				++it;
			}
		} else {
			++it;
		}
	}
}

void CarpetBrush::doCarpets(BaseMap* map, Tile* tile) {
	static const auto hasMatchingCarpetBrushAtTile = [](BaseMap* map, CarpetBrush* carpetBrush, uint32_t x, uint32_t y, uint32_t z) -> bool {
		return carpetBrush->hasMatchingNeighbour(map, x, y, z);
	};

	ASSERT(tile);
	if (!tile->hasCarpet()) {
		return;
	}

	const Position& position = tile->getPosition();
	uint32_t x = position.x;
	uint32_t y = position.y;
	uint32_t z = position.z;

	for (Item* item : tile->items) {
		ASSERT(item);

		CarpetBrush* carpetBrush = item->getCarpetBrush();
		if (!carpetBrush) {
			continue;
		}

		bool neighbours[8] = { false };
		if (x == 0) {
			if (y == 0) {
				neighbours[0] = false;
				neighbours[1] = false;
				neighbours[2] = false;
				neighbours[3] = false;
				neighbours[4] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y, z);
				neighbours[5] = false;
				neighbours[6] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x, y + 1, z);
				neighbours[7] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y + 1, z);
			} else {
				neighbours[0] = false;
				neighbours[1] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x, y - 1, z);
				neighbours[2] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y - 1, z);
				neighbours[3] = false;
				neighbours[4] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y, z);
				neighbours[5] = false;
				neighbours[6] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x, y + 1, z);
				neighbours[7] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y + 1, z);
			}
		} else if (y == 0) {
			neighbours[0] = false;
			neighbours[1] = false;
			neighbours[2] = false;
			neighbours[3] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x - 1, y, z);
			neighbours[4] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y, z);
			neighbours[5] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x - 1, y + 1, z);
			neighbours[6] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x, y + 1, z);
			neighbours[7] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y + 1, z);
		} else {
			neighbours[0] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x - 1, y - 1, z);
			neighbours[1] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x, y - 1, z);
			neighbours[2] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y - 1, z);
			neighbours[3] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x - 1, y, z);
			neighbours[4] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y, z);
			neighbours[5] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x - 1, y + 1, z);
			neighbours[6] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x, y + 1, z);
			neighbours[7] = hasMatchingCarpetBrushAtTile(map, carpetBrush, x + 1, y + 1, z);
		}

		uint32_t tileData = 0;
		for (uint32_t i = 0; i < 8; ++i) {
			if (neighbours[i]) {
				tileData |= static_cast<uint32_t>(1) << i;
			}
		}

		uint16_t id = carpetBrush->getRandomCarpet(static_cast<BorderType>(carpet_types[tileData]));
		if (id != 0) {
			item->setID(id);
		}
	}
}

BorderType CarpetBrush::getCarpetAlignment(uint16_t id) const {
	for (int32_t alignment = 0; alignment < 14; ++alignment) {
		for (const CarpetType& carpetType : carpet_items[alignment].items) {
			if (carpetType.id == id) {
				return static_cast<BorderType>(alignment);
			}
		}
	}
	return BORDER_NONE;
}

uint16_t CarpetBrush::getCarpetItemId(BorderType alignment) const {
	const int32_t index = static_cast<int32_t>(alignment);
	if (index < 0 || index >= 14) {
		return 0;
	}

	const CarpetNode& node = carpet_items[index];
	if (node.items.empty()) {
		return 0;
	}
	return node.items.front().id;
}

uint16_t CarpetBrush::getRandomCarpet(BorderType alignment) {
	static const auto findRandomCarpet = [](const CarpetNode& node) -> uint16_t {
		int32_t chance = random(1, node.total_chance);
		for (const CarpetType& carpetType : node.items) {
			if (chance <= carpetType.chance) {
				return carpetType.id;
			}
			chance -= carpetType.chance;
		}
		return 0;
	};

	CarpetNode node = carpet_items[alignment];
	if (node.total_chance > 0) {
		return findRandomCarpet(node);
	}

	node = carpet_items[CARPET_CENTER];
	if (alignment != CARPET_CENTER && node.total_chance > 0) {
		uint16_t id = findRandomCarpet(node);
		if (id != 0) {
			return id;
		}
	}

	for (int32_t i = 0; i < 12; ++i) {
		node = carpet_items[i];
		if (node.total_chance > 0) {
			uint16_t id = findRandomCarpet(node);
			if (id != 0) {
				return id;
			}
		}
	}
	return 0;
}
