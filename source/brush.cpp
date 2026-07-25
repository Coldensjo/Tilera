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

#include "brush.h"
#include "carpet_brush.h"
#include "creature_brush.h"
#include "doodad_brush.h"
#include "ground_brush.h"
#include "house_brush.h"
#include "house_exit_brush.h"
#include "raw_brush.h"
#include "spawn_brush.h"
#include "table_brush.h"
#include "wall_brush.h"

#include "settings.h"

#include "sprites.h"

#include "item.h"
#include "complexitem.h"
#include "creatures.h"
#include "creature.h"
#include "map.h"

#include "gui.h"

Brushes g_brushes;

Brushes::Brushes() {
	////
}

Brushes::~Brushes() {
	////
}

void Brushes::clear() {
	for (auto brushEntry : brushes) {
		delete brushEntry.second;
	}
	brushes.clear();

	for (auto borderEntry : borders) {
		delete borderEntry.second;
	}
	borders.clear();

	// The cached item -> autoborder lookup points into the borders we just deleted
	resetBorderItemLookup();
}

void Brushes::init() {
	addBrush(g_gui.optional_brush = newd OptionalBorderBrush());
	addBrush(g_gui.eraser = newd EraserBrush());
	addBrush(g_gui.spawn_brush = newd SpawnBrush());
	addBrush(g_gui.normal_door_brush = newd DoorBrush(WALL_DOOR_NORMAL));
	addBrush(g_gui.locked_door_brush = newd DoorBrush(WALL_DOOR_LOCKED));
	addBrush(g_gui.magic_door_brush = newd DoorBrush(WALL_DOOR_MAGIC));
	addBrush(g_gui.quest_door_brush = newd DoorBrush(WALL_DOOR_QUEST));
	addBrush(g_gui.hatch_door_brush = newd DoorBrush(WALL_HATCH_WINDOW));
	addBrush(g_gui.archway_door_brush = newd DoorBrush(WALL_ARCHWAY));
	addBrush(g_gui.normal_door_alt_brush = newd DoorBrush(WALL_DOOR_NORMAL_ALT));
	addBrush(g_gui.window_door_brush = newd DoorBrush(WALL_WINDOW));
	addBrush(g_gui.house_brush = newd HouseBrush());
	addBrush(g_gui.house_exit_brush = newd HouseExitBrush());

	addBrush(g_gui.pz_brush = newd FlagBrush(TILESTATE_PROTECTIONZONE));
	addBrush(g_gui.rook_brush = newd FlagBrush(TILESTATE_NOPVP));
	addBrush(g_gui.nolog_brush = newd FlagBrush(TILESTATE_NOLOGOUT));
	addBrush(g_gui.pvp_brush = newd FlagBrush(TILESTATE_PVPZONE));
	addBrush(g_gui.refresh_brush = newd FlagBrush(TILESTATE_REFRESH));

	GroundBrush::init();
	WallBrush::init();
	TableBrush::init();
	CarpetBrush::init();
}

Brush* Brushes::createBrush(pugi::xml_node node, wxArrayString& warnings) {
	pugi::xml_attribute attribute;
	if (!(attribute = node.attribute("name"))) {
		warnings.push_back("Brush node without name.");
		return nullptr;
	}

	const std::string& brushName = attribute.as_string();
	if (brushName == "all" || brushName == "none") {
		warnings.push_back(wxString("Using reserved brushname \"") << wxstr(brushName) << "\".");
		return nullptr;
	}

	Brush* brush = getBrush(brushName);
	if (brush) {
		return brush;
	}

	if (!(attribute = node.attribute("type"))) {
		warnings.push_back("Couldn't read brush type");
		return nullptr;
	}

	const std::string brushType = attribute.as_string();
	if (brushType == "border" || brushType == "ground") {
		brush = newd GroundBrush();
	} else if (brushType == "wall") {
		brush = newd WallBrush();
	} else if (brushType == "wall decoration") {
		brush = newd WallDecorationBrush();
	} else if (brushType == "carpet") {
		brush = newd CarpetBrush();
	} else if (brushType == "table") {
		brush = newd TableBrush();
	} else if (brushType == "doodad") {
		brush = newd DoodadBrush();
	} else {
		warnings.push_back(wxString("Unknown brush type ") << wxstr(brushType));
		return nullptr;
	}

	ASSERT(brush);
	brush->setName(brushName);
	brushes.insert(std::make_pair(brush->getName(), brush));
	return brush;
}

bool Brushes::unserializeBrush(pugi::xml_node node, wxArrayString& warnings) {
	Brush* brush = createBrush(node, warnings);
	if (!brush) {
		return false;
	}

	if (!node.first_child()) {
		if (!current_load_file.empty()) {
			brush->setSourceFile(current_load_file);
		}
		return true;
	}

	wxArrayString subWarnings;
	brush->load(node, subWarnings);

	if (!subWarnings.empty()) {
		warnings.push_back(wxString("Errors while loading brush \"") << wxstr(brush->getName()) << "\"");
		warnings.insert(warnings.end(), subWarnings.begin(), subWarnings.end());
	}

	if (brush->getName() == "all" || brush->getName() == "none") {
		warnings.push_back(wxString("Using reserved brushname '") << wxstr(brush->getName()) << "'.");
		delete brush;
		return false;
	}

	Brush* otherBrush = getBrush(brush->getName());
	if (otherBrush) {
		if (otherBrush != brush) {
			warnings.push_back(wxString("Duplicate brush name ") << wxstr(brush->getName()) << ". Undefined behaviour may ensue.");
		} else {
			if (!current_load_file.empty()) {
				brush->setSourceFile(current_load_file);
			}
			// Don't insert
			return true;
		}
	}

	if (!current_load_file.empty()) {
		brush->setSourceFile(current_load_file);
	}

	brushes.insert(std::make_pair(brush->getName(), brush));
	return true;
}

bool Brushes::unserializeBorder(pugi::xml_node node, wxArrayString& warnings) {
	pugi::xml_attribute attribute = node.attribute("id");
	if (!attribute) {
		warnings.push_back("Couldn't read border id node");
		return false;
	}

	uint32_t id = attribute.as_uint();
	if (borders[id]) {
		warnings.push_back("Border ID " + std::to_string(id) + " already exists");
		return false;
	}

	AutoBorder* border = newd AutoBorder(id);
	border->load(node, warnings);
	borders[id] = border;
	return true;
}

void Brushes::addBrush(Brush* brush) {
	brushes.insert(std::make_pair(brush->getName(), brush));
}

Brush* Brushes::getBrush(const std::string& name) const {
	auto it = brushes.find(name);
	if (it != brushes.end()) {
		return it->second;
	}
	return nullptr;
}

const AutoBorder* Brushes::getAutoBorder(uint32_t id) const {
	auto it = borders.find(id);
	if (it != borders.end()) {
		return it->second;
	}
	return nullptr;
}

std::vector<uint32_t> Brushes::getAutoBorderIds() const {
	std::vector<uint32_t> ids;
	ids.reserve(borders.size());
	for (const auto& pair : borders) {
		ids.push_back(pair.first);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

// Brush
uint32_t Brush::id_counter = 0;
Brush::Brush() :
	id(++id_counter), visible(false), usesCollection(false) {
	////
}

Brush::~Brush() {
	////
}

// TerrainBrush
TerrainBrush::TerrainBrush() :
	look_id(0), hate_friends(false) {
	////
}

TerrainBrush::~TerrainBrush() {
	////
}

bool TerrainBrush::friendOf(TerrainBrush* other) {
	uint32_t borderID = other->getID();
	for (uint32_t friendId : friends) {
		if (friendId == borderID) {
			// printf("%s is friend of %s\n", getName().c_str(), other->getName().c_str());
			return !hate_friends;
		} else if (friendId == 0xFFFFFFFF) {
			// printf("%s is generic friend of %s\n", getName().c_str(), other->getName().c_str());
			return !hate_friends;
		}
	}
	// printf("%s is enemy of %s\n", getName().c_str(), other->getName().c_str());
	return hate_friends;
}

//=============================================================================
// Flag brush
// draws pz etc.

FlagBrush::FlagBrush(uint32_t _flag) : flag(_flag) {
	////
}

FlagBrush::~FlagBrush() {
	////
}

std::string FlagBrush::getName() const {
	switch (flag) {
		case TILESTATE_PROTECTIONZONE:
			return "PZ brush (0x01)";
		case TILESTATE_NOPVP:
			return "No combat zone brush (0x04)";
		case TILESTATE_NOLOGOUT:
			return "No logout zone brush (0x08)";
		case TILESTATE_PVPZONE:
			return "PVP Zone brush (0x10)";
		case TILESTATE_REFRESH:
			return "Refresh Zone brush (0x20)";
	}
	return "Unknown flag brush";
}

int FlagBrush::getLookID() const {
	switch (flag) {
		case TILESTATE_PROTECTIONZONE:
			return EDITOR_SPRITE_PZ_TOOL;
		case TILESTATE_NOPVP:
			return EDITOR_SPRITE_NOPVP_TOOL;
		case TILESTATE_NOLOGOUT:
			return EDITOR_SPRITE_NOLOG_TOOL;
		case TILESTATE_PVPZONE:
			return EDITOR_SPRITE_PVPZ_TOOL;
		case TILESTATE_REFRESH:
			return EDITOR_SPRITE_REFRESH_TOOL;
	}
	return 0;
}

bool FlagBrush::canDraw(BaseMap* map, const Position& position) const {
	Tile* tile = map->getTile(position);
	return tile && tile->hasGround();
}

void FlagBrush::undraw(BaseMap* map, Tile* tile) {
	tile->unsetMapFlags(flag);
}

void FlagBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	if (tile->hasGround()) {
		tile->setMapFlags(flag);
	}
}

//=============================================================================
// Door brush

DoorBrush::DoorBrush(DoorType _doortype) : doortype(_doortype) {
	////
}

DoorBrush::~DoorBrush() {
	////
}

std::string DoorBrush::getName() const {
	switch (doortype) {
		case WALL_DOOR_NORMAL:
			return "Normal door brush";
		case WALL_DOOR_LOCKED:
			return "Locked door brush";
		case WALL_DOOR_MAGIC:
			return "Magic door brush";
		case WALL_DOOR_QUEST:
			return "Quest door brush";
		case WALL_DOOR_NORMAL_ALT:
			return "Normal door (alt) brush";
		case WALL_ARCHWAY:
			return "Archway brush";
		case WALL_WINDOW:
			return "Window brush";
		case WALL_HATCH_WINDOW:
			return "Hatch window brush";
		default:
			return "Unknown door brush";
	}
}

int DoorBrush::getLookID() const {
	switch (doortype) {
		case WALL_DOOR_NORMAL:
			return EDITOR_SPRITE_DOOR_NORMAL;
		case WALL_DOOR_LOCKED:
			return EDITOR_SPRITE_DOOR_LOCKED;
		case WALL_DOOR_MAGIC:
			return EDITOR_SPRITE_DOOR_MAGIC;
		case WALL_DOOR_QUEST:
			return EDITOR_SPRITE_DOOR_QUEST;
		case WALL_DOOR_NORMAL_ALT:
			return EDITOR_SPRITE_DOOR_NORMAL_ALT;
		case WALL_ARCHWAY:
			return EDITOR_SPRITE_DOOR_ARCHWAY;
		case WALL_WINDOW:
			return EDITOR_SPRITE_WINDOW_NORMAL;
		case WALL_HATCH_WINDOW:
			return EDITOR_SPRITE_WINDOW_HATCH;
		default:
			return EDITOR_SPRITE_DOOR_NORMAL;
	}
}

void DoorBrush::switchDoor(Item* item) {
	ASSERT(item);
	ASSERT(item->isBrushDoor());

	WallBrush* wb = item->getWallBrush();
	if (!wb) {
		return;
	}

	bool new_open = !item->isOpen();
	BorderType wall_alignment = item->getWallAlignment();

	DoorType doortype = wb->getDoorTypeFromID(item->getID());
	if (doortype == WALL_UNDEFINED) {
		return;
	}

	uint16_t oppositeVariant = 0; // give locked/unlocked variant if preferred is unavailable
	bool prefLocked = g_gui.HasDoorLocked();

	for (std::vector<WallBrush::DoorType>::iterator iter = wb->door_items[wall_alignment].begin(); iter != wb->door_items[wall_alignment].end(); ++iter) {
		WallBrush::DoorType& dt = *iter;
		if (dt.type == doortype) {
			ASSERT(dt.id);
			ItemType& it = g_items[dt.id];
			ASSERT(it.id != 0);

			if (it.isOpen == new_open) {
				if (!new_open || dt.locked == prefLocked) {
					item->setID(dt.id);
					return;
				} else {
					oppositeVariant = dt.id;
				}
			}
		}
	}

	if (oppositeVariant != 0) {
		item->setID(oppositeVariant);
	}
}

bool DoorBrush::canDraw(BaseMap* map, const Position& position) const {
	Tile* tile = map->getTile(position);
	if (!tile) {
		return false;
	}

	Item* item = tile->getWall();
	if (!item) {
		return false;
	}

	WallBrush* wb = item->getWallBrush();
	if (!wb) {
		return false;
	}

	BorderType wall_alignment = item->getWallAlignment();

	uint16_t discarded_id = 0; // The id of a discarded match
	bool close_match = false;

	bool open = false;
	if (item->isBrushDoor()) {
		open = item->isOpen();
	}

	bool prefLocked = g_gui.HasDoorLocked();

	WallBrush* test_brush = wb;
	do {
		for (std::vector<WallBrush::DoorType>::iterator iter = test_brush->door_items[wall_alignment].begin();
			 iter != test_brush->door_items[wall_alignment].end();
			 ++iter) {
			WallBrush::DoorType& dt = *iter;
			if (dt.type == doortype) {
				ASSERT(dt.id);
				ItemType& it = g_items[dt.id];
				ASSERT(it.id != 0);

				if (it.isOpen == open) {
					if (open || dt.locked == prefLocked) {
						return true;
					} else {
						discarded_id = dt.id;
						close_match = true;
					}
				} else if (!close_match) {
					discarded_id = dt.id;
					close_match = true;
				}
				if (!close_match && discarded_id == 0) {
					discarded_id = dt.id;
				}
			}
		}
		test_brush = test_brush->redirect_to;
	} while (test_brush != wb && test_brush != nullptr);
	// If we've found no perfect match, use a close-to perfect
	if (discarded_id) {
		return true;
	}
	return false;
}

void DoorBrush::undraw(BaseMap* map, Tile* tile) {
	for (ItemVector::iterator it = tile->items.begin(); it != tile->items.end(); ++it) {
		Item* item = *it;
		if (item->isBrushDoor()) {
			item->getWallBrush()->draw(map, tile, nullptr);
			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				tile->wallize(map);
			}
			return;
		}
	}
}

void DoorBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	for (ItemVector::iterator item_iter = tile->items.begin(); item_iter != tile->items.end();) {
		Item* item = *item_iter;
		if (!item->isWall()) {
			++item_iter;
			continue;
		}
		WallBrush* wb = item->getWallBrush();
		if (!wb) {
			++item_iter;
			continue;
		}

		BorderType wall_alignment = item->getWallAlignment();

		uint16_t discarded_id = 0; // The id of a discarded match
		bool close_match = false;
		bool perfect_match = false;

		bool open = false;
		if (parameter) {
			open = *reinterpret_cast<bool*>(parameter);
		}

		if (item->isBrushDoor()) {
			open = item->isOpen();
		}

		bool prefLocked = g_gui.HasDoorLocked();

		WallBrush* test_brush = wb;
		do {
			for (std::vector<WallBrush::DoorType>::iterator iter = test_brush->door_items[wall_alignment].begin();
				 iter != test_brush->door_items[wall_alignment].end();
				 ++iter) {
				WallBrush::DoorType& dt = *iter;
				if (dt.type == doortype) {
					ASSERT(dt.id);
					ItemType& it = g_items[dt.id];
					ASSERT(it.id != 0);

					if (it.isOpen == open) {
						if (open || dt.locked == prefLocked) {
							item = transformItem(item, dt.id, tile);
							perfect_match = true;
							break;
						} else {
							discarded_id = dt.id;
							close_match = true;
						}
					} else if (!close_match) {
						discarded_id = dt.id;
						close_match = true;
					}
					if (!close_match && discarded_id == 0) {
						discarded_id = dt.id;
					}
				}
			}
			test_brush = test_brush->redirect_to;
			if (perfect_match) {
				break;
			}
		} while (test_brush != wb && test_brush != nullptr);

		// If we've found no perfect match, use a close-to perfect
		if (!perfect_match && discarded_id) {
			item = transformItem(item, discarded_id, tile);
		}

		if (g_settings.getInteger(Config::AUTO_ASSIGN_DOORID) && tile->isHouseTile()) {
			Map* mmap = dynamic_cast<Map*>(map);
			Door* door = dynamic_cast<Door*>(item);
			if (mmap && door) {
				House* house = mmap->houses.getHouse(tile->getHouseID());
				ASSERT(house);
				Map* real_map = dynamic_cast<Map*>(map);
				if (real_map) {
					door->setDoorID(house->getEmptyDoorID());
				}
			}
		}

		// We need to consider decorations!
		while (true) {
			// Vector has been modified, before we can use the iterator again we need to find the wall item again
			item_iter = tile->items.begin();
			while (true) {
				if (item_iter == tile->items.end()) {
					return;
				}
				if (*item_iter == item) {
					++item_iter;
					if (item_iter == tile->items.end()) {
						return;
					}
					break;
				}
				++item_iter;
			}
			// Now it points to the correct item!

			item = *item_iter;
			if (item->isWall()) {
				WallBrush* brush = item->getWallBrush();
				if (brush && brush->isWallDecoration()) {
					// We got a decoration!
					for (std::vector<WallBrush::DoorType>::iterator it = brush->door_items[wall_alignment].begin(); it != brush->door_items[wall_alignment].end(); ++it) {
						WallBrush::DoorType& dt = (*it);
						if (dt.type == doortype) {
							ASSERT(dt.id);
							ItemType& it = g_items[dt.id];
							ASSERT(it.id != 0);

							if (it.isOpen == open) {
								if (open || dt.locked == prefLocked) {
									item = transformItem(item, dt.id, tile);
									perfect_match = true;
									break;
								} else {
									discarded_id = dt.id;
									close_match = true;
								}
							} else if (!close_match) {
								discarded_id = dt.id;
								close_match = true;
							}
							if (!close_match && discarded_id == 0) {
								discarded_id = dt.id;
							}
						}
					}
					// If we've found no perfect match, use a close-to perfect
					if (!perfect_match && discarded_id) {
						item = transformItem(item, discarded_id, tile);
					}
					continue;
				}
			}
			break;
		}
		// If we get this far in the loop we should return
		return;
	}
}

//=============================================================================
// Gravel brush

OptionalBorderBrush::OptionalBorderBrush() {
	////
}

OptionalBorderBrush::~OptionalBorderBrush() {
	////
}

std::string OptionalBorderBrush::getName() const {
	return "Optional Border Tool";
}

int OptionalBorderBrush::getLookID() const {
	return EDITOR_SPRITE_OPTIONAL_BORDER_TOOL;
}

bool OptionalBorderBrush::canDraw(BaseMap* map, const Position& position) const {
	Tile* tile = map->getTile(position);

	// You can't do gravel on a mountain tile
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return false;
			}
		}
	}

	uint32_t x = position.x;
	uint32_t y = position.y;
	uint32_t z = position.z;

	tile = map->getTile(x - 1, y - 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x, y - 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x + 1, y - 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x - 1, y, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x + 1, y, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x - 1, y + 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x, y + 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}
	tile = map->getTile(x + 1, y + 1, z);
	if (tile) {
		if (GroundBrush* bb = tile->getGroundBrush()) {
			if (bb->hasOptionalBorder()) {
				return true;
			}
		}
	}

	return false;
}

void OptionalBorderBrush::undraw(BaseMap* map, Tile* tile) {
	tile->setOptionalBorder(false); // The bordering algorithm will handle this automagicaly
}

void OptionalBorderBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	tile->setOptionalBorder(true); // The bordering algorithm will handle this automagicaly
}

//=============================================================================
// Palette separator

PaletteSeparatorBrush::PaletteSeparatorBrush() {
	////
}

PaletteSeparatorBrush::~PaletteSeparatorBrush() {
	////
}

std::string PaletteSeparatorBrush::getName() const {
	return "Separator";
}

//=============================================================================
// Orientation remapping for rotated/mirrored areas

namespace {

// Which BorderType an edge becomes after a clockwise quarter turn
const BorderType border_clockwise[14] = {
	BORDER_NONE, // BORDER_NONE
	EAST_HORIZONTAL, // NORTH_HORIZONTAL
	SOUTH_HORIZONTAL, // EAST_HORIZONTAL
	WEST_HORIZONTAL, // SOUTH_HORIZONTAL
	NORTH_HORIZONTAL, // WEST_HORIZONTAL
	NORTHEAST_CORNER, // NORTHWEST_CORNER
	SOUTHEAST_CORNER, // NORTHEAST_CORNER
	NORTHWEST_CORNER, // SOUTHWEST_CORNER
	SOUTHWEST_CORNER, // SOUTHEAST_CORNER
	NORTHEAST_DIAGONAL, // NORTHWEST_DIAGONAL
	SOUTHEAST_DIAGONAL, // NORTHEAST_DIAGONAL
	SOUTHWEST_DIAGONAL, // SOUTHEAST_DIAGONAL
	NORTHWEST_DIAGONAL, // SOUTHWEST_DIAGONAL
	CARPET_CENTER, // CARPET_CENTER
};

// ...and after mirroring along the vertical axis (west <-> east)
const BorderType border_flip_horizontal[14] = {
	BORDER_NONE,
	NORTH_HORIZONTAL,
	WEST_HORIZONTAL,
	SOUTH_HORIZONTAL,
	EAST_HORIZONTAL,
	NORTHEAST_CORNER,
	NORTHWEST_CORNER,
	SOUTHEAST_CORNER,
	SOUTHWEST_CORNER,
	NORTHEAST_DIAGONAL,
	NORTHWEST_DIAGONAL,
	SOUTHWEST_DIAGONAL,
	SOUTHEAST_DIAGONAL,
	CARPET_CENTER,
};

// ...and after mirroring along the horizontal axis (north <-> south)
const BorderType border_flip_vertical[14] = {
	BORDER_NONE,
	SOUTH_HORIZONTAL,
	EAST_HORIZONTAL,
	NORTH_HORIZONTAL,
	WEST_HORIZONTAL,
	SOUTHWEST_CORNER,
	SOUTHEAST_CORNER,
	NORTHWEST_CORNER,
	NORTHEAST_CORNER,
	SOUTHWEST_DIAGONAL,
	SOUTHEAST_DIAGONAL,
	NORTHEAST_DIAGONAL,
	NORTHWEST_DIAGONAL,
	CARPET_CENTER,
};

// Table alignments after a clockwise quarter turn
const BorderType table_clockwise[7] = {
	BorderType(TABLE_EAST_END), // TABLE_NORTH_END
	BorderType(TABLE_WEST_END), // TABLE_SOUTH_END
	BorderType(TABLE_SOUTH_END), // TABLE_EAST_END
	BorderType(TABLE_NORTH_END), // TABLE_WEST_END
	BorderType(TABLE_VERTICAL), // TABLE_HORIZONTAL
	BorderType(TABLE_HORIZONTAL), // TABLE_VERTICAL
	BorderType(TABLE_ALONE), // TABLE_ALONE
};

// The wall alignment is the mask of the sides a wall piece connects to - the same
// bits WallBrush::full_border_types is indexed by (see brush_tables.cpp). The names
// in BorderType read the other way round: connecting north only is WALL_SOUTH_END,
// because that is where the wall ends.
enum WallConnection {
	WALL_CONNECT_NORTH = 1,
	WALL_CONNECT_WEST = 2,
	WALL_CONNECT_EAST = 4,
	WALL_CONNECT_SOUTH = 8,
};

int rotateWallMaskClockwise(int mask) {
	int rotated = 0;
	if (mask & WALL_CONNECT_NORTH) {
		rotated |= WALL_CONNECT_EAST;
	}
	if (mask & WALL_CONNECT_EAST) {
		rotated |= WALL_CONNECT_SOUTH;
	}
	if (mask & WALL_CONNECT_SOUTH) {
		rotated |= WALL_CONNECT_WEST;
	}
	if (mask & WALL_CONNECT_WEST) {
		rotated |= WALL_CONNECT_NORTH;
	}
	return rotated;
}

int mirrorWallMask(int mask, bool horizontal) {
	const int keep = horizontal ? (WALL_CONNECT_NORTH | WALL_CONNECT_SOUTH) : (WALL_CONNECT_EAST | WALL_CONNECT_WEST);
	const int swap_a = horizontal ? WALL_CONNECT_EAST : WALL_CONNECT_NORTH;
	const int swap_b = horizontal ? WALL_CONNECT_WEST : WALL_CONNECT_SOUTH;

	int mirrored = mask & keep;
	if (mask & swap_a) {
		mirrored |= swap_b;
	}
	if (mask & swap_b) {
		mirrored |= swap_a;
	}
	return mirrored;
}

// item id -> the autoborder it belongs to, built on first use
std::map<uint16_t, const AutoBorder*> border_item_lookup;
bool border_item_lookup_built = false;

const AutoBorder* findAutoBorderForItem(uint16_t item_id) {
	if (!border_item_lookup_built) {
		for (uint32_t border_id : g_brushes.getAutoBorderIds()) {
			const AutoBorder* border = g_brushes.getAutoBorder(border_id);
			if (!border) {
				continue;
			}
			for (int edge = NORTH_HORIZONTAL; edge < 13; ++edge) {
				if (border->tiles[edge] != 0) {
					border_item_lookup.emplace(static_cast<uint16_t>(border->tiles[edge]), border);
				}
			}
		}
		border_item_lookup_built = true;
	}

	auto it = border_item_lookup.find(item_id);
	return it == border_item_lookup.end() ? nullptr : it->second;
}

bool transformBorderItem(Item* item, MapTransform transform) {
	const uint16_t item_id = item->getID();
	const AutoBorder* border = findAutoBorderForItem(item_id);
	if (!border) {
		return false;
	}

	// The edge this item sits on inside its border. An item can be listed under
	// several edges, the first match is the one we can map back from.
	int edge = BORDER_NONE;
	for (int i = NORTH_HORIZONTAL; i < 13; ++i) {
		if (border->tiles[i] == item_id) {
			edge = i;
			break;
		}
	}

	if (edge == BORDER_NONE) {
		return false;
	}

	const BorderType new_edge = transformBorderAlignment(BorderType(edge), transform);
	const uint32_t new_id = border->tiles[new_edge];
	if (new_id == 0 || new_id == item_id) {
		return false;
	}

	item->setID(static_cast<uint16_t>(new_id));
	return true;
}

bool transformWallItem(Item* item, MapTransform transform) {
	WallBrush* wall = item->getWallBrush();
	if (!wall) {
		return false;
	}

	const BorderType alignment = item->getBorderAlignment();
	const BorderType new_alignment = transformWallAlignment(alignment, transform);
	if (new_alignment == alignment) {
		return false;
	}

	uint16_t new_id = 0;
	if (item->isBrushDoor()) {
		// Doors and windows stay doors and windows, they just turn with the wall
		const ::DoorType door_type = wall->getDoorTypeFromID(item->getID());
		new_id = wall->findMatchingDoorItem(new_alignment, door_type, item->isOpen(), g_items[item->getID()].isLocked);
	} else {
		new_id = wall->getWallItemId(new_alignment);
	}

	if (new_id == 0 || new_id == item->getID()) {
		return false;
	}

	item->setID(new_id);
	return true;
}

bool transformCarpetItem(Item* item, MapTransform transform) {
	CarpetBrush* carpet = item->getCarpetBrush();
	if (!carpet) {
		return false;
	}

	const BorderType alignment = carpet->getCarpetAlignment(item->getID());
	const BorderType new_alignment = transformBorderAlignment(alignment, transform);
	if (new_alignment == alignment) {
		return false;
	}

	const uint16_t new_id = carpet->getCarpetItemId(new_alignment);
	if (new_id == 0 || new_id == item->getID()) {
		return false;
	}

	item->setID(new_id);
	return true;
}

bool transformTableItem(Item* item, MapTransform transform) {
	TableBrush* table = item->getTableBrush();
	if (!table) {
		return false;
	}

	const BorderType alignment = table->getTableAlignment(item->getID());
	const BorderType new_alignment = transformTableAlignment(alignment, transform);
	if (new_alignment == alignment) {
		return false;
	}

	const uint16_t new_id = table->getTableItemId(new_alignment);
	if (new_id == 0 || new_id == item->getID()) {
		return false;
	}

	item->setID(new_id);
	return true;
}

} // namespace

BorderType transformBorderAlignment(BorderType alignment, MapTransform transform) {
	if (alignment <= BORDER_NONE || alignment > CARPET_CENTER) {
		return alignment;
	}

	switch (transform) {
		case MapTransform::RotateClockwise:
			return border_clockwise[alignment];
		case MapTransform::RotateCounterClockwise:
			// Three quarter turns clockwise
			return border_clockwise[border_clockwise[border_clockwise[alignment]]];
		case MapTransform::FlipHorizontal:
			return border_flip_horizontal[alignment];
		case MapTransform::FlipVertical:
			return border_flip_vertical[alignment];
	}
	return alignment;
}

BorderType transformWallAlignment(BorderType alignment, MapTransform transform) {
	const int mask = static_cast<int>(alignment);
	if (mask < 0 || mask > WALL_INTERSECTION) {
		// WALL_UNTOUCHABLE and anything unknown is left alone
		return alignment;
	}

	switch (transform) {
		case MapTransform::RotateClockwise:
			return BorderType(rotateWallMaskClockwise(mask));
		case MapTransform::RotateCounterClockwise:
			return BorderType(rotateWallMaskClockwise(rotateWallMaskClockwise(rotateWallMaskClockwise(mask))));
		case MapTransform::FlipHorizontal:
			return BorderType(mirrorWallMask(mask, true));
		case MapTransform::FlipVertical:
			return BorderType(mirrorWallMask(mask, false));
	}
	return alignment;
}

BorderType transformTableAlignment(BorderType alignment, MapTransform transform) {
	const int index = static_cast<int>(alignment);
	if (index < TABLE_NORTH_END || index > TABLE_ALONE) {
		return alignment;
	}

	switch (transform) {
		case MapTransform::RotateClockwise:
			return table_clockwise[index];
		case MapTransform::RotateCounterClockwise:
			return table_clockwise[table_clockwise[table_clockwise[index]]];
		case MapTransform::FlipHorizontal:
			if (index == TABLE_EAST_END) {
				return BorderType(TABLE_WEST_END);
			}
			if (index == TABLE_WEST_END) {
				return BorderType(TABLE_EAST_END);
			}
			return alignment;
		case MapTransform::FlipVertical:
			if (index == TABLE_NORTH_END) {
				return BorderType(TABLE_SOUTH_END);
			}
			if (index == TABLE_SOUTH_END) {
				return BorderType(TABLE_NORTH_END);
			}
			return alignment;
	}
	return alignment;
}

bool transformItemOrientation(Item* item, MapTransform transform) {
	if (!item) {
		return false;
	}

	// Border pieces, walls, carpets and tables have a counterpart for every
	// orientation - swap to the one matching the transformed layout
	if (item->isBorder() && transformBorderItem(item, transform)) {
		return true;
	}

	if (item->isWall() && transformWallItem(item, transform)) {
		return true;
	}

	if (item->isCarpet() && transformCarpetItem(item, transform)) {
		return true;
	}

	if (item->isTable() && transformTableItem(item, transform)) {
		return true;
	}

	// Everything else can only follow a rotation if the item type says it can
	const int steps = itemRotationSteps(transform);
	if (steps == 0 || !item->isRoteable()) {
		return false;
	}

	for (int i = 0; i < steps; ++i) {
		item->doRotate();
	}
	return true;
}

bool reconnectTileWalls(BaseMap* map, Tile* tile) {
	if (!map || !tile) {
		return false;
	}

	const Position pos = tile->getPosition();
	bool changed = false;
	BorderType last_alignment = BORDER_NONE;
	bool has_last_alignment = false;

	for (Item* item : tile->items) {
		if (!item->isWall()) {
			continue;
		}

		WallBrush* wall = item->getWallBrush();
		if (!wall || item->getWallAlignment() == WALL_UNTOUCHABLE) {
			continue;
		}

		BorderType alignment;
		if (wall->isWallDecoration()) {
			// Decorations sit on top of the wall below them and follow its alignment
			if (!has_last_alignment) {
				continue;
			}
			alignment = last_alignment;
		} else {
			// Which of the four neighbours carry a wall this one connects to
			int mask = 0;
			if (WallBrush::tileHasConnectableWall(map->getTile(pos.x, pos.y - 1, pos.z), wall)) {
				mask |= WALL_CONNECT_NORTH;
			}
			if (WallBrush::tileHasConnectableWall(map->getTile(pos.x - 1, pos.y, pos.z), wall)) {
				mask |= WALL_CONNECT_WEST;
			}
			if (WallBrush::tileHasConnectableWall(map->getTile(pos.x + 1, pos.y, pos.z), wall)) {
				mask |= WALL_CONNECT_EAST;
			}
			if (WallBrush::tileHasConnectableWall(map->getTile(pos.x, pos.y + 1, pos.z), wall)) {
				mask |= WALL_CONNECT_SOUTH;
			}

			alignment = BorderType(WallBrush::full_border_types[mask]);
			last_alignment = alignment;
			has_last_alignment = true;
		}

		if (item->getWallAlignment() == alignment) {
			continue;
		}

		uint16_t new_id = 0;
		if (item->isBrushDoor()) {
			const ::DoorType door_type = wall->getDoorTypeFromID(item->getID());
			new_id = wall->findMatchingDoorItem(alignment, door_type, item->isOpen(), g_items[item->getID()].isLocked);
		} else {
			new_id = wall->getWallItemId(alignment);
		}

		// No piece for this shape (many wall sets only define one corner) - the piece
		// that is already there is the closest thing the brush has, so keep it
		if (new_id == 0 || new_id == item->getID()) {
			continue;
		}

		item->setID(new_id);
		changed = true;
	}

	return changed;
}

void resetBorderItemLookup() {
	border_item_lookup.clear();
	border_item_lookup_built = false;
}
