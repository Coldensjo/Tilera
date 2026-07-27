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

#ifndef RME_SETTINGS_H_
#define RME_SETTINGS_H_

#include "main.h"

namespace Config {
	enum Key {
		NONE,
		VERSION_ID,

		USE_CUSTOM_DATA_DIRECTORY,
		DATA_DIRECTORY,
		EXTENSIONS_DIRECTORY,

		MERGE_MOVE,
		TEXTURE_MANAGEMENT,
		TEXTURE_CLEAN_PULSE,
		TEXTURE_CLEAN_THRESHOLD,
		TEXTURE_LONGEVITY,
		HARD_REFRESH_RATE,
		USE_MEMCACHED_SPRITES,
		USE_MEMCACHED_SPRITES_TO_SAVE,
		SOFTWARE_CLEAN_THRESHOLD,
		SOFTWARE_CLEAN_SIZE,
		TRANSPARENT_FLOORS,
		TRANSPARENT_ITEMS,
		TRANSPARENT_ITEMS_ON_VOID,
		SHOW_INGAME_BOX,
		SHOW_GRID,
		SHOW_EXTRA,
		SHOW_ALL_FLOORS,
		SHOW_ALL_FLOORS_UNDERGROUND,
		SHOW_CREATURES,
		SHOW_SPAWNS,
		SHOW_HOUSES,
		SHOW_SHADE,
		SHOW_SPECIAL_TILES,
		HIGHLIGHT_ITEMS,
		SHOW_ITEMS,
		SHOW_BLOCKING,
		SHOW_TOOLTIPS,
		SHOW_PREVIEW,
		SHOW_WALL_HOOKS,
		SHOW_FISHABLE_WATER,
		SHOW_BORDERS,
		SHOW_WALLS,
		SHOW_GROUND,
		SHOW_AS_MINIMAP,
		SHOW_ONLY_TILEFLAGS,
		SHOW_ONLY_MODIFIED_TILES,
		HIDE_ITEMS_WHEN_ZOOMED,
		GROUP_ACTIONS,
		SCROLL_SPEED,
		ZOOM_SPEED,
		UNDO_SIZE,
		UNDO_MEM_SIZE,
		MERGE_PASTE,
		SELECTION_TYPE,
		COMPENSATED_SELECT,
		LASSO_SELECT,
		BORDER_IS_GROUND,
		BORDERIZE_PASTE,
		BORDERIZE_DRAG,
		BORDERIZE_DRAG_THRESHOLD,
		BORDERIZE_PASTE_THRESHOLD,
		CONTENT_AWARE_FILL_IGNORE_BORDERS,
		CONTENT_AWARE_FILL_IGNORE_GROUND,
		ICON_BACKGROUND,
		ALWAYS_MAKE_BACKUP,
		USE_AUTOMAGIC,
		HOUSE_BRUSH_REMOVE_ITEMS,
		AUTO_ASSIGN_DOORID,
		ERASER_LEAVE_UNIQUE,
		DOODAD_BRUSH_ERASE_LIKE,
		WARN_FOR_DUPLICATE_ID,
		USE_OTBM_4_FOR_ALL_MAPS,
		USE_OTGZ,
		SAVE_WITH_OTB_MAGIC_NUMBER,
		REPLACE_SIZE,

		USE_LARGE_CONTAINER_ICONS,
		USE_LARGE_CHOOSE_ITEM_ICONS,
		USE_LARGE_TERRAIN_TOOLBAR,
		USE_LARGE_DOODAD_SIZEBAR,
		USE_LARGE_ITEM_SIZEBAR,
		USE_LARGE_HOUSE_SIZEBAR,
		USE_LARGE_RAW_SIZEBAR,
		USE_GUI_SELECTION_SHADOW,
		PALETTE_COL_COUNT,
		PALETTE_TERRAIN_STYLE,
		PALETTE_DOODAD_STYLE,
		PALETTE_ITEM_STYLE,
		PALETTE_RAW_STYLE,

		ASSETS_DATA_DIRS,
		DEFAULT_CLIENT_VERSION,
		CHECK_SIGNATURES,

		CURSOR_RED,
		CURSOR_GREEN,
		CURSOR_BLUE,
		CURSOR_ALPHA,

		CURSOR_ALT_RED,
		CURSOR_ALT_GREEN,
		CURSOR_ALT_BLUE,
		CURSOR_ALT_ALPHA,

		SCREENSHOT_DIRECTORY,
		SCREENSHOT_FORMAT,
		MAX_SPAWN_RADIUS,
		CURRENT_SPAWN_RADIUS,
		AUTO_CREATE_SPAWN,
		DEFAULT_SPAWNTIME,
		SWITCH_MOUSEBUTTONS,
		DOUBLECLICK_PROPERTIES,
		LISTBOX_EATS_ALL_EVENTS,
		RAW_LIKE_SIMONE,
		WORKER_THREADS,
		COPY_POSITION_FORMAT,

		GOTO_WEBSITE_ON_BOOT,
		INDIRECTORY_INSTALLATION,
		ONLY_ONE_INSTANCE,
		SHOW_TILESET_EDITOR,

		PALETTE_LAYOUT,
		MINIMAP_VISIBLE,
		MINIMAP_LAYOUT,
		MINIMAP_UPDATE_DELAY,
		MINIMAP_VIEW_BOX,
		MINIMAP_SCROLL_ZOOM,
		MINIMAP_EXPORT_DIR,
		TILESET_EXPORT_DIR,
		WINDOW_HEIGHT,
		WINDOW_WIDTH,
		WINDOW_MAXIMIZED,
		WELCOME_DIALOG,

		NUMERICAL_HOTKEYS,
		RECENT_FILES,

		RECENT_EDITED_MAP_PATH,
		RECENT_EDITED_MAP_POSITION,

		FIND_ITEM_MODE,
		JUMP_TO_ITEM_MODE,

		SHOW_TOOLBAR_STANDARD,
		SHOW_TOOLBAR_BRUSHES,
		SHOW_TOOLBAR_POSITION,
		SHOW_TOOLBAR_SIZES,
		TOOLBAR_STANDARD_LAYOUT,
		TOOLBAR_BRUSHES_LAYOUT,
		TOOLBAR_POSITION_LAYOUT,
		TOOLBAR_SIZES_LAYOUT,

		// add new settings at the end to make sure nothing gets misread
		DRAW_LOCKED_DOOR,
		HIGHLIGHT_LOCKED_DOORS,
		SHOW_LIGHTS,
		SHOW_LIGHT_STR,
		SHOW_TECHNICAL_ITEMS,

		SHOW_TOWNS,
		ALWAYS_SHOW_ZONES,
		EXT_HOUSE_SHADER,
		SHOW_INVISIBLE_ITEMS,

		SHOW_CREATURE_SPAWN_TIME,

		SHOW_TOOLTIP_KEY_NUMBER,
		SHOW_TOOLTIP_DOOR_PROPERTIES,

		CONTAINER_FIND_DEFAULT_NAMES,
		CONTAINER_FAVORITE_ITEMS,

		IGNORE_SAVE_PROMPT,
		AUTO_SAVE_ON_CLOSE,

		OPEN_MAP_ON_STARTUP,
		OPEN_MAP_ON_STARTUP_PATH,

		SHOW_CHUNK_BOUNDARIES,
		SHOW_TILE_COORDINATES,
		SHOW_HUD,
		SHOW_AUTOBORDER_INDICATOR,
		SHOW_MOUSE_CROSSHAIR,
		CURSOR_CROSSHAIR_RED,
		CURSOR_CROSSHAIR_GREEN,
		CURSOR_CROSSHAIR_BLUE,
		VIEWPORT_BACKGROUND_RED,
		VIEWPORT_BACKGROUND_GREEN,
		VIEWPORT_BACKGROUND_BLUE,

		LIVE_HOST,
		LIVE_PORT,
		LIVE_USERNAME,
		LIVE_PASSWORD,
		LIVE_SPOOF_VERSION,
		LIVE_AUTO_VERSION_PROBE,
		LIVE_VERSION_PROBE_LIMIT,
		LIVE_VERSION_PROBE_MAX_SUBVERSION,
		LIVE_ALLOW_CLIPBOARD,
		LIVE_CURSOR_RED,
		LIVE_CURSOR_GREEN,
		LIVE_CURSOR_BLUE,
		LIVE_CURSOR_ALPHA,

		MAP_VIEW_POSITIONS,
		UI_THEME,
		SHOW_PALETTE_TOOLS,
		SHOW_PALETTE_BRUSH_SIZE,

		SHOW_MAKE_SCRIPT_MENU,

		LAST,
	};
}

class wxConfigBase;

class Settings {
public:
	Settings();
	~Settings();

	bool getBoolean(uint32_t key) const;
	int getInteger(uint32_t key) const;
	float getFloat(uint32_t key) const;
	std::string getString(uint32_t key) const;
	uint32_t getRevision() const;

	void setInteger(uint32_t key, int newval);
	void setFloat(uint32_t key, float newval);
	void setString(uint32_t key, std::string newval);

	wxConfigBase& getConfigObject();
	void setDefaults() {
		IO(DEFAULT);
	}
	void load();
	void save(bool endoftheworld = false);

public:
	enum DynamicType {
		TYPE_NONE,
		TYPE_STR,
		TYPE_INT,
		TYPE_FLOAT,
	};
	class DynamicValue {
	public:
		DynamicValue() : type(TYPE_NONE) {
			intval = 0;
		};
		DynamicValue(DynamicType t) : type(t) {
			if (t == TYPE_STR) {
				strval = nullptr;
			} else if (t == TYPE_INT) {
				intval = 0;
			} else if (t == TYPE_FLOAT) {
				floatval = 0.0;
			} else {
				intval = 0;
			}
		};
		~DynamicValue() {
			if (type == TYPE_STR) {
				delete strval;
			}
		}
		DynamicValue(const DynamicValue& dv) : type(dv.type) {
			if (dv.type == TYPE_STR) {
				strval = newd std::string(*dv.strval);
			} else if (dv.type == TYPE_INT) {
				intval = dv.intval;
			} else if (dv.type == TYPE_FLOAT) {
				floatval = dv.floatval;
			} else {
				intval = 0;
			}
		};

		std::string str();

	private:
		DynamicType type;
		union {
			int intval;
			std::string* strval;
			float floatval;
		};

		friend class Settings;
	};

private:
	enum IOMode {
		DEFAULT,
		LOAD,
		SAVE,
	};
	void IO(IOMode mode);
	std::vector<DynamicValue> store;
#ifdef __WINDOWS__
	bool use_file_cfg;
#endif
	uint32_t revision;
};

extern Settings g_settings;

#endif
