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

#include "gui.h" // loadbar
#include "settings.h"

#include "map.h"

#include <sstream>
#include <algorithm>

Map::Map() : BaseMap(),
			 width(512),
			 height(512),
			 houses(*this),
			 has_changed(false),
			 unnamed(false) {
	// Earliest version possible
	// Caller is responsible for converting us to proper version
	mapVersion.otbm = MAP_OTBM_1;
	mapVersion.client = CLIENT_VERSION_NONE;
}

Map::~Map() {
	////
}

namespace {
	// Plain recursion (no std::function/dynamic_cast) since this runs over
	// every item on the map when the Unique ID cache is first built.
	void markUsedUniqueIds(std::bitset<0x10000>& used, Item* item) {
		if (!item) {
			return;
		}

		uint16_t uid = item->getUniqueID();
		if (uid > 0) {
			used.set(uid);
		}

		// Cheap type-flag check avoids paying for dynamic_cast's RTTI lookup on
		// every non-container item, which is the overwhelming majority.
		if (g_items[item->getID()].isContainer()) {
			if (Container* container = dynamic_cast<Container*>(item)) {
				for (Item* containedItem : container->getVector()) {
					markUsedUniqueIds(used, containedItem);
				}
			}
		}
	}
}

void Map::markUniqueIdUsed(uint16_t uid) {
	if (uid > 0) {
		usedUniqueIdsCache.set(uid);
	}
}

uint16_t Map::findFreeUniqueId() {
	if (!uniqueIdCacheBuilt) {
		for (MapIterator miter = begin(); miter != end(); ++miter) {
			Tile* tile = (*miter)->get();
			if (!tile) {
				continue;
			}

			if (tile->ground) {
				markUsedUniqueIds(usedUniqueIdsCache, tile->ground);
			}
			for (Item* item : tile->items) {
				markUsedUniqueIds(usedUniqueIdsCache, item);
			}
		}
		uniqueIdCacheBuilt = true;
	}

	for (uint32_t id = 1000; id <= 0xFFFF; ++id) {
		if (!usedUniqueIdsCache.test(id)) {
			return static_cast<uint16_t>(id);
		}
	}
	return 0;
}

bool Map::open(const std::string file) {
	if (file == filename) {
		return true; // Do not reopen ourselves!
	}

	tilecount = 0;

	IOMapOTBM maploader(getVersion());

	bool success = maploader.loadMap(*this, wxstr(file));

	mapVersion = maploader.version;

	warnings = maploader.getWarnings();

	if (!success) {
		error = maploader.getError();
		return false;
	}

	has_changed = false;

	wxFileName fn = wxstr(file);
	filename = fn.GetFullPath().mb_str(wxConvUTF8);
	name = fn.GetFullName().mb_str(wxConvUTF8);

	return true;
}

bool Map::convert(MapVersion to, bool showdialog) {
	if (mapVersion.client == to.client) {
		// Only OTBM version differs
		// No changes necessary
		mapVersion = to;
		return true;
	}

	mapVersion = to;

	return true;
}

void Map::cleanInvalidTiles(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Removing invalid tiles...");
	}

	uint64_t tiles_done = 0;

	for (MapIterator miter = begin(); miter != end(); ++miter) {
		Tile* tile = (*miter)->get();
		ASSERT(tile);

		if (tile->size() == 0) {
			continue;
		}

		for (ItemVector::iterator item_iter = tile->items.begin(); item_iter != tile->items.end();) {
			if (g_items.typeExists((*item_iter)->getID())) {
				++item_iter;
			} else {
				delete *item_iter;
				item_iter = tile->items.erase(item_iter);
			}
		}

		++tiles_done;
		if (showdialog && tiles_done % 0x10000 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(getTileCount()) * 100.0));
		}
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Map::convertHouseTiles(uint32_t fromId, uint32_t toId) {
	g_gui.CreateLoadBar("Converting house tiles...");
	uint64_t tiles_done = 0;

	for (MapIterator miter = begin(); miter != end(); ++miter) {
		Tile* tile = (*miter)->get();
		ASSERT(tile);

		uint32_t houseId = tile->getHouseID();
		if (houseId == 0 || houseId != fromId) {
			continue;
		}

		tile->setHouseID(toId);
		++tiles_done;
		if (tiles_done % 0x10000 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(getTileCount()) * 100.0));
		}
	}

	g_gui.DestroyLoadBar();
}

MapVersion Map::getVersion() const {
	return mapVersion;
}

bool Map::hasChanged() const {
	return has_changed;
}

bool Map::doChange() {
	bool doupdate = !has_changed;
	has_changed = true;
	return doupdate;
}

bool Map::clearChanges() {
	bool doupdate = has_changed;
	has_changed = false;
	return doupdate;
}

bool Map::hasFile() const {
	return filename != "";
}

void Map::setWidth(int new_width) {
	if (new_width > 65000) {
		width = 65000;
	} else if (new_width < 64) {
		width = 64;
	} else {
		width = new_width;
	}
}

void Map::setHeight(int new_height) {
	if (new_height > 65000) {
		height = 65000;
	} else if (new_height < 64) {
		height = 64;
	} else {
		height = new_height;
	}
}
void Map::setMapDescription(const std::string& new_description) {
	description = new_description;
}

void Map::setHouseFilename(const std::string& new_housefile) {
	housefile = new_housefile;
	unnamed = false;
}

void Map::setSpawnFilename(const std::string& new_spawnfile) {
	spawnfile = new_spawnfile;
	unnamed = false;
}

void Map::setCommentsFilename(const std::string& new_commentsfile) {
	commentsfile = new_commentsfile;
	unnamed = false;
}

bool Map::addSpawn(Tile* tile) {
	Spawn* spawn = tile->spawn;
	if (spawn) {
		int z = tile->getZ();
		int start_x = tile->getX() - spawn->getSize();
		int start_y = tile->getY() - spawn->getSize();
		int end_x = tile->getX() + spawn->getSize();
		int end_y = tile->getY() + spawn->getSize();

		for (int y = start_y; y <= end_y; ++y) {
			for (int x = start_x; x <= end_x; ++x) {
				TileLocation* ctile_loc = createTileL(x, y, z);
				ctile_loc->increaseSpawnCount();
			}
		}
		spawns.addSpawn(tile);
		return true;
	}
	return false;
}

void Map::removeSpawnInternal(Tile* tile) {
	Spawn* spawn = tile->spawn;
	ASSERT(spawn);

	int z = tile->getZ();
	int start_x = tile->getX() - spawn->getSize();
	int start_y = tile->getY() - spawn->getSize();
	int end_x = tile->getX() + spawn->getSize();
	int end_y = tile->getY() + spawn->getSize();

	for (int y = start_y; y <= end_y; ++y) {
		for (int x = start_x; x <= end_x; ++x) {
			TileLocation* ctile_loc = getTileL(x, y, z);
			if (ctile_loc != nullptr && ctile_loc->getSpawnCount() > 0) {
				ctile_loc->decreaseSpawnCount();
			}
		}
	}
}

void Map::removeSpawn(Tile* tile) {
	if (tile->spawn) {
		removeSpawnInternal(tile);
		spawns.removeSpawn(tile);
	}
}

SpawnList Map::getSpawnList(Tile* where) {
	SpawnList list;
	TileLocation* tile_loc = where->getLocation();
	if (tile_loc) {
		if (tile_loc->getSpawnCount() > 0) {
			uint32_t found = 0;
			if (where->spawn) {
				++found;
				list.push_back(where->spawn);
			}

			// Scans the border tiles in an expanding square around the original spawn
			int z = where->getZ();
			int start_x = where->getX() - 1, end_x = where->getX() + 1;
			int start_y = where->getY() - 1, end_y = where->getY() + 1;
			const int maxSpawnRadius = g_settings.getInteger(Config::MAX_SPAWN_RADIUS);
			int max_search_radius = maxSpawnRadius * 2; // Safety limit to prevent infinite loops
			
			while (found != tile_loc->getSpawnCount()) {
				// Safety check: prevent infinite loop if spawn_count is inconsistent
				int current_radius = (end_x - start_x) / 2;
				if (current_radius > max_search_radius) {
					// Spawn count inconsistency detected, break to prevent infinite loop
					break;
				}
				
				for (int x = start_x; x <= end_x; ++x) {
					Tile* tile = getTile(x, start_y, z);
					if (tile && tile->spawn) {
						// Check if spawn is already in list to avoid duplicates
						if (std::find(list.begin(), list.end(), tile->spawn) == list.end()) {
							list.push_back(tile->spawn);
							++found;
						}
					}
					tile = getTile(x, end_y, z);
					if (tile && tile->spawn) {
						if (std::find(list.begin(), list.end(), tile->spawn) == list.end()) {
							list.push_back(tile->spawn);
							++found;
						}
					}
				}

				for (int y = start_y + 1; y < end_y; ++y) {
					Tile* tile = getTile(start_x, y, z);
					if (tile && tile->spawn) {
						if (std::find(list.begin(), list.end(), tile->spawn) == list.end()) {
							list.push_back(tile->spawn);
							++found;
						}
					}
					tile = getTile(end_x, y, z);
					if (tile && tile->spawn) {
						if (std::find(list.begin(), list.end(), tile->spawn) == list.end()) {
							list.push_back(tile->spawn);
							++found;
						}
					}
				}
				--start_x, --start_y;
				++end_x, ++end_y;
			}
		}
	}
	return list;
}

bool Map::exportMinimap(FileName filename, int floor /*= GROUND_LAYER*/, bool displaydialog) {
	uint8_t* pic = nullptr;

	try {
		int min_x = 0x10000, min_y = 0x10000;
		int max_x = 0x00000, max_y = 0x00000;

		if (size() == 0) {
			return true;
		}

		for (MapIterator mit = begin(); mit != end(); ++mit) {
			if ((*mit)->get() == nullptr || (*mit)->empty()) {
				continue;
			}

			Position pos = (*mit)->getPosition();

			if (pos.x < min_x) {
				min_x = pos.x;
			}

			if (pos.y < min_y) {
				min_y = pos.y;
			}

			if (pos.x > max_x) {
				max_x = pos.x;
			}

			if (pos.y > max_y) {
				max_y = pos.y;
			}
		}

		int minimap_width = max_x - min_x + 1;
		int minimap_height = max_y - min_y + 1;

		pic = newd uint8_t[minimap_width * minimap_height]; // 1 byte per pixel

		memset(pic, 0, minimap_width * minimap_height);

		int tiles_iterated = 0;
		for (MapIterator mit = begin(); mit != end(); ++mit) {
			Tile* tile = (*mit)->get();
			++tiles_iterated;
			if (tiles_iterated % 8192 == 0 && displaydialog) {
				g_gui.SetLoadDone(int(tiles_iterated / double(tilecount) * 90.0));
			}

			if (tile->empty() || tile->getZ() != floor) {
				continue;
			}

			// std::cout << "Pixel : " << (tile->getY() - min_y) * width + (tile->getX() - min_x) << std::endl;
			uint32_t pixelpos = (tile->getY() - min_y) * minimap_width + (tile->getX() - min_x);
			uint8_t& pixel = pic[pixelpos];

			for (ItemVector::const_reverse_iterator item_iter = tile->items.rbegin(); item_iter != tile->items.rend(); ++item_iter) {
				if ((*item_iter)->getMiniMapColor()) {
					pixel = (*item_iter)->getMiniMapColor();
					break;
				}
			}
			if (pixel == 0) {
				// check ground too
				if (tile->hasGround()) {
					pixel = tile->ground->getMiniMapColor();
				}
			}
		}

		// Convert palette indices to RGB and save as PNG
		uint8_t* rgb = newd uint8_t[minimap_width * minimap_height * 3]; // 3 bytes per pixel
		for (int y = 0; y < minimap_height; ++y) {
			for (int x = 0; x < minimap_width; ++x) {
				const RGBQuad& color = minimap_color[pic[y * minimap_width + x]];
				uint32_t index = (y * minimap_width + x) * 3;
				rgb[index] = color.red;
				rgb[index + 1] = color.green;
				rgb[index + 2] = color.blue;
			}
			if (y % 100 == 0 && displaydialog) {
				g_gui.SetLoadDone(90 + int(y / double(minimap_height) * 10.0));
			}
		}

		delete[] pic;
		pic = nullptr;

		wxImage image(minimap_width, minimap_height, rgb, true);
		bool saved = image.SaveFile(filename.GetFullPath(), wxBITMAP_TYPE_PNG);
		image.Destroy();
		delete[] rgb;

		if (!saved) {
			return false;
		}
	} catch (...) {
		delete[] pic;
	}

	return true;
}
