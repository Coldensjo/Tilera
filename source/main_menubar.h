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

#ifndef RME_MAIN_BAR_H_
#define RME_MAIN_BAR_H_

#include <wx/docview.h>

namespace MenuBar {
	struct Action;

	enum ActionID {
		NEW,
		OPEN,
		SAVE,
		SAVE_AS,
		GENERATE_MAP,
		CLOSE,
		IMPORT_MAP,
		IMPORT_MONSTERS,
		IMPORT_MINIMAP,
		EXPORT_MINIMAP,
		EXPORT_TILESETS,
		RELOAD_DATA,
		RECENT_FILES,
		PREFERENCES,
		EXIT,
		UNDO,
		REDO,
		FIND_ITEM,
		FIND_CREATURE,
		REPLACE_ITEMS,
		SEARCH_ON_MAP_EVERYTHING,
		SEARCH_ON_MAP_UNIQUE,
		SEARCH_ON_MAP_DUPLICATE,
		SEARCH_ON_MAP_ACTION,
		SEARCH_ON_MAP_CONTAINER,
		SEARCH_ON_MAP_WRITEABLE,
		SEARCH_ON_MAP_KEYHOLE,
		SEARCH_ON_MAP_DOOR_LEVEL,
		SEARCH_ON_MAP_DOOR_QUEST_NUMBER,
		SEARCH_ON_MAP_DOOR_QUEST_VALUE,
		SEARCH_ON_SELECTION_EVERYTHING,
		SEARCH_ON_SELECTION_UNIQUE,
		SEARCH_ON_SELECTION_DUPLICATE,
		SEARCH_ON_SELECTION_ACTION,
		SEARCH_ON_SELECTION_CONTAINER,
		SEARCH_ON_SELECTION_WRITEABLE,
		SEARCH_ON_SELECTION_KEYHOLE,
		SEARCH_ON_SELECTION_DOOR_LEVEL,
		SEARCH_ON_SELECTION_DOOR_QUEST_NUMBER,
		SEARCH_ON_SELECTION_DOOR_QUEST_VALUE,
		SEARCH_ON_SELECTION_ITEM,
		REPLACE_ON_SELECTION_ITEMS,
		REMOVE_ON_SELECTION_ITEM,
		EXPORT_CREATURES_IN_SELECTION,
		SELECT_ITEM_FROM_ID,
		SELECT_MODE_COMPENSATE,
		SELECT_MODE_LASSO,
		SELECT_MODE_CURRENT,
		SELECT_MODE_LOWER,
		SELECT_MODE_VISIBLE,
		AUTOMAGIC,
		BORDERIZE_SELECTION,
		BORDERIZE_MAP,
		RANDOMIZE_SELECTION,
		RANDOMIZE_MAP,
		CONTENT_AWARE_FILL_SELECTION,
		MOVE_SELECTION_UP,
		MOVE_SELECTION_DOWN,
		ROTATE_SELECTION_CW,
		ROTATE_SELECTION_CCW,
		FLIP_SELECTION_HORIZONTAL,
		FLIP_SELECTION_VERTICAL,
		GOTO_PREVIOUS_POSITION,
		GOTO_POSITION,
		JUMP_TO_BRUSH,
		JUMP_TO_ITEM_BRUSH,
		FIND_BRUSH_WINDOW,
		CLEAR_INVALID_HOUSES,
		CLEAR_MODIFIED_STATE,
		CUT,
		COPY,
		PASTE,
		EDIT_TOWNS,
		EDIT_ITEMS,
		EDIT_MONSTERS,
		MAP_CLEANUP,
		MAP_REMOVE_ITEMS,
		MAP_REMOVE_CORPSES,
		MAP_REMOVE_UNREACHABLE_TILES,
		MAP_CLEAN_HOUSE_ITEMS,
		MAP_PROPERTIES,
		MAP_STATISTICS,
		VIEW_TOOLBARS_BRUSHES,
		VIEW_TOOLBARS_POSITION,
		VIEW_TOOLBARS_SIZES,
		VIEW_TOOLBARS_STANDARD,
		VIEW_PALETTE_TOOLS,
		VIEW_PALETTE_BRUSH_SIZE,
		NEW_VIEW,
		TOGGLE_FULLSCREEN,
		ZOOM_IN,
		ZOOM_OUT,
		ZOOM_NORMAL,
		SHOW_SHADE,
		SHOW_ALL_FLOORS,
		SHOW_ALL_FLOORS_UNDERGROUND,
		GHOST_ITEMS,
		GHOST_ITEMS_ON_VOID,
		GHOST_HIGHER_FLOORS,
		HIGHLIGHT_ITEMS,
		HIGHLIGHT_LOCKED_DOORS,
		SHOW_INGAME_BOX,
		SHOW_LIGHTS,
		SHOW_LIGHT_STR,
		SHOW_TECHNICAL_ITEMS,
		SHOW_GRID,
		SHOW_TILE_COORDINATES,
		SHOW_HUD,
		SHOW_AUTOBORDER_INDICATOR,
		SHOW_EXTRA,
		SHOW_CREATURES,
		SHOW_CREATURE_SPAWN_TIME,
		SHOW_SPAWNS,
		SHOW_SPECIAL,
		SHOW_AS_MINIMAP,
		SHOW_ONLY_COLORS,
		SHOW_ONLY_MODIFIED,
		SHOW_HOUSES,
		SHOW_PATHING,
		SHOW_TOOLTIPS,
		SHOW_TOOLTIP_KEY_NUMBER,
		SHOW_TOOLTIP_DOOR_PROPERTIES,
		SHOW_PREVIEW,
		SHOW_WALL_HOOKS,
		SHOW_FISHABLE_WATER,
		SHOW_BORDERS,
		SHOW_WALLS,
		SHOW_GROUND,
		SHOW_TOWNS,
		ALWAYS_SHOW_ZONES,
		EXT_HOUSE_SHADER,
		SHOW_INVISIBLE_ITEMS,
		WIN_MINIMAP,
		NEW_PALETTE,
		TAKE_SCREENSHOT,
		SELECT_TERRAIN,
		SELECT_DOODAD,
		SELECT_ITEM,
		SELECT_CREATURE,
		SELECT_HOUSE,
		SELECT_RAW,
		FLOOR_0,
		FLOOR_1,
		FLOOR_2,
		FLOOR_3,
		FLOOR_4,
		FLOOR_5,
		FLOOR_6,
		FLOOR_7,
		FLOOR_8,
		FLOOR_9,
		FLOOR_10,
		FLOOR_11,
		FLOOR_12,
		FLOOR_13,
		FLOOR_14,
		FLOOR_15,
		DEBUG_VIEW_DAT,
		DEBUG_BENCHMARK,
		EXTENSIONS,
		GOTO_WEBSITE,
		ABOUT,

		LIVE_CONNECT,
		LIVE_DISCONNECT,

		SELECT_EXIT_BUTTON,

		HOTKEY_MANAGER,
	};
}

class MainFrame;

class MainMenuBar : public wxEvtHandler {
public:
	MainMenuBar(MainFrame* frame);
	virtual ~MainMenuBar();

	bool Load(const FileName&, wxArrayString& warnings, wxString& error);

	// Update
	// Turn on/off all buttons according to current editor state
	void Update();
	void UpdateFloorMenu(); // Only concerns the floor menu

	void AddRecentFile(FileName file);
	void LoadRecentFiles();
	void SaveRecentFiles();
	std::vector<wxString> GetRecentFiles();

	// Interface
	void EnableItem(MenuBar::ActionID id, bool enable);
	void CheckItem(MenuBar::ActionID id, bool enable);
	bool IsItemChecked(MenuBar::ActionID id) const;

	// Re-applies the (possibly user-overridden) hotkey of every menu item
	// to its label/accelerator. Called by the Hotkey Manager dialog after
	// a binding changes.
	void ApplyHotkeyOverrides();

#ifdef __WXGTK__
	// Rebuilds the frame-level accelerator table from the accelerators
	// currently attached to the menu items.
	void RebuildGtkAcceleratorTable();
#endif

#ifdef __WXGTK__
	// wxGTK fires accelerator-table hotkeys as a synthetic wxEVT_MENU sent
	// straight to the handler, without going through the real wxMenuItem
	// the way a genuine click (or a native Windows accelerator) does - so
	// checkable items never get their checked state flipped and handlers
	// that trust IsItemChecked() see stale state. Called from
	// MainFrame::OnCharHook, before the accelerator table processes the
	// same key, to flip the checkbox first.
	void PreToggleGtkCheckHotkey(const wxKeyEvent& event);
#endif

	// Event handlers for all menu buttons
	// File Menu
	void OnNew(wxCommandEvent& event);
	void OnOpen(wxCommandEvent& event);
	void OnGenerateMap(wxCommandEvent& event);
	void OnOpenRecent(wxCommandEvent& event);
	void OnSave(wxCommandEvent& event);
	void OnSaveAs(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);
	void OnPreferences(wxCommandEvent& event);
	void OnHotkeyManager(wxCommandEvent& event);
	void OnQuit(wxCommandEvent& event);

	// Import Menu
	// Export Menu
	void OnImportMap(wxCommandEvent& event);
	void OnImportMonsterData(wxCommandEvent& event);
	void OnImportMinimap(wxCommandEvent& event);
	void OnExportMinimap(wxCommandEvent& event);
	void OnExportTilesets(wxCommandEvent& event);
	void OnReloadDataFiles(wxCommandEvent& event);

	// Edit Menu
	void OnUndo(wxCommandEvent& event);
	void OnRedo(wxCommandEvent& event);
	void OnBorderizeSelection(wxCommandEvent& event);
	void OnBorderizeMap(wxCommandEvent& event);
	void OnRandomizeSelection(wxCommandEvent& event);
	void OnRandomizeMap(wxCommandEvent& event);
	void OnContentAwareFillSelection(wxCommandEvent& event);
	void OnMoveSelectionUp(wxCommandEvent& event);
	void OnMoveSelectionDown(wxCommandEvent& event);
	void OnRotateSelectionClockwise(wxCommandEvent& event);
	void OnRotateSelectionCounterClockwise(wxCommandEvent& event);
	void OnFlipSelectionHorizontal(wxCommandEvent& event);
	void OnFlipSelectionVertical(wxCommandEvent& event);
	void OnJumpToBrush(wxCommandEvent& event);
	void OnJumpToItemBrush(wxCommandEvent& event);
	void OnFindBrushWindow(wxCommandEvent& event);
	void OnGotoPreviousPosition(wxCommandEvent& event);
	void OnGotoPosition(wxCommandEvent& event);
	void OnMapRemoveItems(wxCommandEvent& event);
	void OnMapRemoveCorpses(wxCommandEvent& event);
	void OnMapRemoveUnreachable(wxCommandEvent& event);
	void OnClearHouseTiles(wxCommandEvent& event);
	void OnClearModifiedState(wxCommandEvent& event);
	void OnToggleAutomagic(wxCommandEvent& event);
	void OnSelectionTypeChange(wxCommandEvent& event);
	void OnCut(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);
	void OnPaste(wxCommandEvent& event);
	void OnSearchForItem(wxCommandEvent& event);
	void OnSearchForCreature(wxCommandEvent& event);
	void OnReplaceItems(wxCommandEvent& event);
	void OnSearchForStuffOnMap(wxCommandEvent& event);
	void OnSearchForUniqueOnMap(wxCommandEvent& event);
	void OnSearchForDuplicateOnMap(wxCommandEvent& event);
	void OnSearchForActionOnMap(wxCommandEvent& event);
	void OnSearchForContainerOnMap(wxCommandEvent& event);
	void OnSearchForWriteableOnMap(wxCommandEvent& event);
	void OnSearchForKeyHoleOnMap(wxCommandEvent& event);
	void OnSearchForDoorLevelOnMap(wxCommandEvent& event);
	void OnSearchForDoorQuestNumberOnMap(wxCommandEvent& event);
	void OnSearchForDoorQuestValueOnMap(wxCommandEvent& event);

	// Select menu
	void OnSearchForStuffOnSelection(wxCommandEvent& event);
	void OnSearchForUniqueOnSelection(wxCommandEvent& event);
	void OnSearchForDuplicateOnSelection(wxCommandEvent& event);
	void OnSearchForActionOnSelection(wxCommandEvent& event);
	void OnSearchForContainerOnSelection(wxCommandEvent& event);
	void OnSearchForWriteableOnSelection(wxCommandEvent& event);
	void OnSearchForKeyHoleOnSelection(wxCommandEvent& event);
	void OnSearchForDoorLevelOnSelection(wxCommandEvent& event);
	void OnSearchForDoorQuestNumberOnSelection(wxCommandEvent& event);
	void OnSearchForDoorQuestValueOnSelection(wxCommandEvent& event);
	void OnSearchForItemOnSelection(wxCommandEvent& event);
	void OnReplaceItemsOnSelection(wxCommandEvent& event);
	void OnRemoveItemOnSelection(wxCommandEvent& event);
	void OnExportCreaturesInSelection(wxCommandEvent& event);
	void OnSelectItemFromId(wxCommandEvent& event);
	void OnSelectExitButton(wxCommandEvent& event);

	// Map menu
	void OnMapEditTowns(wxCommandEvent& event);
	void OnMapEditItems(wxCommandEvent& event);
	void OnMapEditMonsters(wxCommandEvent& event);
	void OnMapCleanHouseItems(wxCommandEvent& event);
	void OnMapCleanup(wxCommandEvent& event);
	void OnMapProperties(wxCommandEvent& event);
	void OnMapStatistics(wxCommandEvent& event);

	// View Menu
	void OnToolbars(wxCommandEvent& event);
	void OnPalettePanels(wxCommandEvent& event);
	void OnNewView(wxCommandEvent& event);
	void OnToggleFullscreen(wxCommandEvent& event);
	void OnZoomIn(wxCommandEvent& event);
	void OnZoomOut(wxCommandEvent& event);
	void OnZoomNormal(wxCommandEvent& event);
	void OnChangeViewSettings(wxCommandEvent& event);

	// Network menu
	void OnLiveConnect(wxCommandEvent& event);
	void OnLiveDisconnect(wxCommandEvent& event);

	// Window Menu
	void OnMinimapWindow(wxCommandEvent& event);
	void OnNewPalette(wxCommandEvent& event);
	void OnTakeScreenshot(wxCommandEvent& event);
	void OnSelectTerrainPalette(wxCommandEvent& event);
	void OnSelectDoodadPalette(wxCommandEvent& event);
	void OnSelectItemPalette(wxCommandEvent& event);
	void OnSelectHousePalette(wxCommandEvent& event);
	void OnSelectCreaturePalette(wxCommandEvent& event);
	void OnSelectRawPalette(wxCommandEvent& event);

	// Floor menu
	void OnChangeFloor(wxCommandEvent& event);

	// About Menu
	void OnDebugViewDat(wxCommandEvent& event);
	void OnDebugBenchmark(wxCommandEvent& event);
	void OnListExtensions(wxCommandEvent& event);
	void OnGotoWebsite(wxCommandEvent& event);
	void OnAbout(wxCommandEvent& event);

protected:
	// Load and returns a menu item, also sets accelerator; category is the
	// top-level menu name, used to group actions in the Hotkey Manager
	wxObject* LoadItem(pugi::xml_node node, wxMenu* parent, wxArrayString& warnings, wxString& error, const wxString& category = wxString());
	// Checks the items in the menus according to the settings (in config)
	void LoadValues();
	void SearchItems(bool unique, bool action, bool container, bool writable, bool duplicate, bool keyhole, bool doorLevel, bool doorQuestNumber, bool doorQuestValue, bool onSelection = false);

protected:
	MainFrame* frame;
	wxMenuBar* menubar;

	// Used so that calling Check on menu items don't trigger events (avoids infinite recursion)
	bool checking_programmaticly;

	std::map<MenuBar::ActionID, std::list<wxMenuItem*>> items;

#ifdef __WXGTK__
	// Source of truth for the frame-level accelerator table built in Load();
	// also searched by PreToggleGtkCheckHotkey() to find which action (if
	// any) a given keypress maps to.
	std::vector<wxAcceleratorEntry> gtk_hotkey_entries;
#endif

	// Hardcoded recent files
	wxFileHistory recentFiles;

	std::map<std::string, MenuBar::Action*> actions;

	DECLARE_EVENT_TABLE();
};

namespace MenuBar {
	struct Action {
		Action() : id(0), kind(wxITEM_NORMAL) { }
		Action(std::string s, int id, wxItemKind kind, wxCommandEventFunction handler) : id(id), setting(0), name(s), kind(kind), handler(handler) { }

		int id;
		int setting;
		std::string name;
		wxItemKind kind;
		wxCommandEventFunction handler;
	};
}

#endif
