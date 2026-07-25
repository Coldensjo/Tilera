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

#include "main_menubar.h"
#include "application.h"
#include "preferences.h"
#include "about_window.h"
#include "minimap_window.h"
#include "dat_debug_view.h"
#include "result_window.h"
#include "extension_window.h"
#include "find_item_window.h"
#include "settings.h"

#include "gui.h"

#include <wx/chartype.h>

#include <chrono>
#include <map>

#include "editor.h"
#include "map_tab.h"
#include "materials.h"
#include "live_client.h"
#include "live_tab.h"
#include "common_windows.h"

BEGIN_EVENT_TABLE(MainMenuBar, wxEvtHandler)
END_EVENT_TABLE()

MainMenuBar::MainMenuBar(MainFrame* frame) : frame(frame) {
	using namespace MenuBar;
	checking_programmaticly = false;

#define MAKE_ACTION(id, kind, handler) actions[#id] = new MenuBar::Action(#id, id, kind, wxCommandEventFunction(&MainMenuBar::handler))
#define MAKE_SET_ACTION(id, kind, setting_, handler)                                                  \
	actions[#id] = new MenuBar::Action(#id, id, kind, wxCommandEventFunction(&MainMenuBar::handler)); \
	actions[#id].setting = setting_

	MAKE_ACTION(NEW, wxITEM_NORMAL, OnNew);
	MAKE_ACTION(OPEN, wxITEM_NORMAL, OnOpen);
	MAKE_ACTION(SAVE, wxITEM_NORMAL, OnSave);
	MAKE_ACTION(SAVE_AS, wxITEM_NORMAL, OnSaveAs);
	MAKE_ACTION(GENERATE_MAP, wxITEM_NORMAL, OnGenerateMap);
	MAKE_ACTION(CLOSE, wxITEM_NORMAL, OnClose);

	MAKE_ACTION(IMPORT_MAP, wxITEM_NORMAL, OnImportMap);
	MAKE_ACTION(IMPORT_MONSTERS, wxITEM_NORMAL, OnImportMonsterData);
	MAKE_ACTION(IMPORT_MINIMAP, wxITEM_NORMAL, OnImportMinimap);
	MAKE_ACTION(EXPORT_MINIMAP, wxITEM_NORMAL, OnExportMinimap);
	MAKE_ACTION(EXPORT_TILESETS, wxITEM_NORMAL, OnExportTilesets);

	MAKE_ACTION(RELOAD_DATA, wxITEM_NORMAL, OnReloadDataFiles);
	// MAKE_ACTION(RECENT_FILES, wxITEM_NORMAL, OnRecent);
	MAKE_ACTION(PREFERENCES, wxITEM_NORMAL, OnPreferences);
	MAKE_ACTION(EXIT, wxITEM_NORMAL, OnQuit);

	MAKE_ACTION(UNDO, wxITEM_NORMAL, OnUndo);
	MAKE_ACTION(REDO, wxITEM_NORMAL, OnRedo);

	MAKE_ACTION(FIND_ITEM, wxITEM_NORMAL, OnSearchForItem);
	MAKE_ACTION(FIND_CREATURE, wxITEM_NORMAL, OnSearchForCreature);
	MAKE_ACTION(REPLACE_ITEMS, wxITEM_NORMAL, OnReplaceItems);
	MAKE_ACTION(SEARCH_ON_MAP_EVERYTHING, wxITEM_NORMAL, OnSearchForStuffOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_UNIQUE, wxITEM_NORMAL, OnSearchForUniqueOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_DUPLICATE, wxITEM_NORMAL, OnSearchForDuplicateOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_ACTION, wxITEM_NORMAL, OnSearchForActionOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_CONTAINER, wxITEM_NORMAL, OnSearchForContainerOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_WRITEABLE, wxITEM_NORMAL, OnSearchForWriteableOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_KEYHOLE, wxITEM_NORMAL, OnSearchForKeyHoleOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_DOOR_LEVEL, wxITEM_NORMAL, OnSearchForDoorLevelOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_DOOR_QUEST_NUMBER, wxITEM_NORMAL, OnSearchForDoorQuestNumberOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_DOOR_QUEST_VALUE, wxITEM_NORMAL, OnSearchForDoorQuestValueOnMap);
	MAKE_ACTION(SEARCH_ON_SELECTION_EVERYTHING, wxITEM_NORMAL, OnSearchForStuffOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_UNIQUE, wxITEM_NORMAL, OnSearchForUniqueOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_DUPLICATE, wxITEM_NORMAL, OnSearchForDuplicateOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_ACTION, wxITEM_NORMAL, OnSearchForActionOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_CONTAINER, wxITEM_NORMAL, OnSearchForContainerOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_WRITEABLE, wxITEM_NORMAL, OnSearchForWriteableOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_KEYHOLE, wxITEM_NORMAL, OnSearchForKeyHoleOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_DOOR_LEVEL, wxITEM_NORMAL, OnSearchForDoorLevelOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_DOOR_QUEST_NUMBER, wxITEM_NORMAL, OnSearchForDoorQuestNumberOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_DOOR_QUEST_VALUE, wxITEM_NORMAL, OnSearchForDoorQuestValueOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_ITEM, wxITEM_NORMAL, OnSearchForItemOnSelection);
	MAKE_ACTION(REPLACE_ON_SELECTION_ITEMS, wxITEM_NORMAL, OnReplaceItemsOnSelection);
	MAKE_ACTION(REMOVE_ON_SELECTION_ITEM, wxITEM_NORMAL, OnRemoveItemOnSelection);
	MAKE_ACTION(EXPORT_CREATURES_IN_SELECTION, wxITEM_NORMAL, OnExportCreaturesInSelection);
	MAKE_ACTION(SELECT_ITEM_FROM_ID, wxITEM_NORMAL, OnSelectItemFromId);
	MAKE_ACTION(SELECT_MODE_COMPENSATE, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_LASSO, wxITEM_CHECK, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_LOWER, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_CURRENT, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_VISIBLE, wxITEM_RADIO, OnSelectionTypeChange);

	MAKE_ACTION(AUTOMAGIC, wxITEM_CHECK, OnToggleAutomagic);
	MAKE_ACTION(BORDERIZE_SELECTION, wxITEM_NORMAL, OnBorderizeSelection);
	MAKE_ACTION(BORDERIZE_MAP, wxITEM_NORMAL, OnBorderizeMap);
	MAKE_ACTION(RANDOMIZE_SELECTION, wxITEM_NORMAL, OnRandomizeSelection);
	MAKE_ACTION(RANDOMIZE_MAP, wxITEM_NORMAL, OnRandomizeMap);
	MAKE_ACTION(CONTENT_AWARE_FILL_SELECTION, wxITEM_NORMAL, OnContentAwareFillSelection);
	MAKE_ACTION(MOVE_SELECTION_UP, wxITEM_NORMAL, OnMoveSelectionUp);
	MAKE_ACTION(MOVE_SELECTION_DOWN, wxITEM_NORMAL, OnMoveSelectionDown);
	MAKE_ACTION(ROTATE_SELECTION_CW, wxITEM_NORMAL, OnRotateSelectionClockwise);
	MAKE_ACTION(ROTATE_SELECTION_CCW, wxITEM_NORMAL, OnRotateSelectionCounterClockwise);
	MAKE_ACTION(FLIP_SELECTION_HORIZONTAL, wxITEM_NORMAL, OnFlipSelectionHorizontal);
	MAKE_ACTION(FLIP_SELECTION_VERTICAL, wxITEM_NORMAL, OnFlipSelectionVertical);
	MAKE_ACTION(GOTO_PREVIOUS_POSITION, wxITEM_NORMAL, OnGotoPreviousPosition);
	MAKE_ACTION(GOTO_POSITION, wxITEM_NORMAL, OnGotoPosition);
	MAKE_ACTION(JUMP_TO_BRUSH, wxITEM_NORMAL, OnJumpToBrush);
	MAKE_ACTION(JUMP_TO_ITEM_BRUSH, wxITEM_NORMAL, OnJumpToItemBrush);
	MAKE_ACTION(FIND_BRUSH_WINDOW, wxITEM_NORMAL, OnFindBrushWindow);

	MAKE_ACTION(CUT, wxITEM_NORMAL, OnCut);
	MAKE_ACTION(COPY, wxITEM_NORMAL, OnCopy);
	MAKE_ACTION(PASTE, wxITEM_NORMAL, OnPaste);

	MAKE_ACTION(EDIT_TOWNS, wxITEM_NORMAL, OnMapEditTowns);
	MAKE_ACTION(EDIT_ITEMS, wxITEM_NORMAL, OnMapEditItems);
	MAKE_ACTION(EDIT_MONSTERS, wxITEM_NORMAL, OnMapEditMonsters);

	MAKE_ACTION(CLEAR_INVALID_HOUSES, wxITEM_NORMAL, OnClearHouseTiles);
	MAKE_ACTION(CLEAR_MODIFIED_STATE, wxITEM_NORMAL, OnClearModifiedState);
	MAKE_ACTION(MAP_REMOVE_ITEMS, wxITEM_NORMAL, OnMapRemoveItems);
	MAKE_ACTION(MAP_REMOVE_CORPSES, wxITEM_NORMAL, OnMapRemoveCorpses);
	MAKE_ACTION(MAP_REMOVE_UNREACHABLE_TILES, wxITEM_NORMAL, OnMapRemoveUnreachable);
	MAKE_ACTION(MAP_CLEANUP, wxITEM_NORMAL, OnMapCleanup);
	MAKE_ACTION(MAP_CLEAN_HOUSE_ITEMS, wxITEM_NORMAL, OnMapCleanHouseItems);
	MAKE_ACTION(MAP_PROPERTIES, wxITEM_NORMAL, OnMapProperties);
	MAKE_ACTION(MAP_STATISTICS, wxITEM_NORMAL, OnMapStatistics);

	MAKE_ACTION(VIEW_TOOLBARS_BRUSHES, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_POSITION, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_SIZES, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_STANDARD, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_PALETTE_TOOLS, wxITEM_CHECK, OnPalettePanels);
	MAKE_ACTION(VIEW_PALETTE_BRUSH_SIZE, wxITEM_CHECK, OnPalettePanels);
	MAKE_ACTION(NEW_VIEW, wxITEM_NORMAL, OnNewView);
	MAKE_ACTION(TOGGLE_FULLSCREEN, wxITEM_NORMAL, OnToggleFullscreen);

	MAKE_ACTION(ZOOM_IN, wxITEM_NORMAL, OnZoomIn);
	MAKE_ACTION(ZOOM_OUT, wxITEM_NORMAL, OnZoomOut);
	MAKE_ACTION(ZOOM_NORMAL, wxITEM_NORMAL, OnZoomNormal);

	MAKE_ACTION(SHOW_SHADE, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ALL_FLOORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ALL_FLOORS_UNDERGROUND, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_ITEMS_ON_VOID, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_HIGHER_FLOORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(HIGHLIGHT_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(HIGHLIGHT_LOCKED_DOORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_EXTRA, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_INGAME_BOX, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_LIGHTS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_LIGHT_STR, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TECHNICAL_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_GRID, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TILE_COORDINATES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_HUD, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_AUTOBORDER_INDICATOR, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_CREATURES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_CREATURE_SPAWN_TIME, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPAWNS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPECIAL, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_AS_MINIMAP, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ONLY_COLORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ONLY_MODIFIED, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_HOUSES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PATHING, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TOOLTIPS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TOOLTIP_KEY_NUMBER, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TOOLTIP_DOOR_PROPERTIES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PREVIEW, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_WALL_HOOKS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_FISHABLE_WATER, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_BORDERS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_WALLS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_GROUND, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TOWNS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(ALWAYS_SHOW_ZONES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(EXT_HOUSE_SHADER, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_INVISIBLE_ITEMS, wxITEM_CHECK, OnChangeViewSettings);

	MAKE_ACTION(WIN_MINIMAP, wxITEM_NORMAL, OnMinimapWindow);
	MAKE_ACTION(NEW_PALETTE, wxITEM_NORMAL, OnNewPalette);
	MAKE_ACTION(TAKE_SCREENSHOT, wxITEM_NORMAL, OnTakeScreenshot);


	MAKE_ACTION(SELECT_TERRAIN, wxITEM_NORMAL, OnSelectTerrainPalette);
	MAKE_ACTION(SELECT_DOODAD, wxITEM_NORMAL, OnSelectDoodadPalette);
	MAKE_ACTION(SELECT_ITEM, wxITEM_NORMAL, OnSelectItemPalette);
	MAKE_ACTION(SELECT_CREATURE, wxITEM_NORMAL, OnSelectCreaturePalette);
	MAKE_ACTION(SELECT_HOUSE, wxITEM_NORMAL, OnSelectHousePalette);
	MAKE_ACTION(SELECT_RAW, wxITEM_NORMAL, OnSelectRawPalette);

	MAKE_ACTION(FLOOR_0, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_1, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_2, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_3, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_4, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_5, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_6, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_7, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_8, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_9, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_10, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_11, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_12, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_13, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_14, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_15, wxITEM_RADIO, OnChangeFloor);

	MAKE_ACTION(DEBUG_VIEW_DAT, wxITEM_NORMAL, OnDebugViewDat);
	MAKE_ACTION(DEBUG_BENCHMARK, wxITEM_NORMAL, OnDebugBenchmark);
	MAKE_ACTION(EXTENSIONS, wxITEM_NORMAL, OnListExtensions);
	MAKE_ACTION(GOTO_WEBSITE, wxITEM_NORMAL, OnGotoWebsite);
	MAKE_ACTION(ABOUT, wxITEM_NORMAL, OnAbout);

	MAKE_ACTION(LIVE_CONNECT, wxITEM_NORMAL, OnLiveConnect);
	MAKE_ACTION(LIVE_DISCONNECT, wxITEM_NORMAL, OnLiveDisconnect);

	MAKE_ACTION(SELECT_EXIT_BUTTON, wxITEM_NORMAL, OnSelectExitButton);

	// A deleter, this way the frame does not need
	// to bother deleting us.
	class CustomMenuBar : public wxMenuBar {
	public:
		CustomMenuBar(MainMenuBar* mb) : mb(mb) { }
		~CustomMenuBar() {
			delete mb;
		}

	private:
		MainMenuBar* mb;
	};

	menubar = newd CustomMenuBar(this);
	frame->SetMenuBar(menubar);

	// Tie all events to this handler!

	for (std::map<std::string, MenuBar::Action*>::iterator ai = actions.begin(); ai != actions.end(); ++ai) {
		frame->Connect(MAIN_FRAME_MENU + ai->second->id, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction)(wxEventFunction)(ai->second->handler), nullptr, this);
	}
	for (size_t i = 0; i < 10; ++i) {
		frame->Connect(recentFiles.GetBaseId() + i, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(MainMenuBar::OnOpenRecent), nullptr, this);
	}
}

#include "palette_window.h"
#include "palette_house.h"

void MainMenuBar::OnSelectExitButton(wxCommandEvent&) {
	// Step 1: Switch to the House palette
	g_gui.SelectPalettePage(TILESET_HOUSE);

	// Step 2: Get the active panel from the palette window
	PaletteWindow* palette = g_gui.GetPalette();
	if (!palette) {
		return;
	}

	// Step 3: Get the house palette panel directly
	HousePalettePanel* housePanel = palette->GetHousePalette();
	if (!housePanel) {
		return;
	}

	// Step 4: Invoke the button action
	wxCommandEvent selectExitEvent;
	housePanel->OnClickSelectExitButton(selectExitEvent);
}

MainMenuBar::~MainMenuBar() {
	// Don't need to delete menubar, it's owned by the frame

	for (std::map<std::string, MenuBar::Action*>::iterator ai = actions.begin(); ai != actions.end(); ++ai) {
		delete ai->second;
	}
}

namespace OnMapRemoveItems {
	struct RemoveItemCondition {
		RemoveItemCondition(uint16_t itemId) :
			itemId(itemId) { }

		uint16_t itemId;

		bool operator()(Map& map, Item* item, int64_t removed, int64_t done) {
			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((uint32_t)(100 * done / map.getTileCount()));
			}
			return item->getID() == itemId && !item->isComplex();
		}
	};
}

void MainMenuBar::EnableItem(MenuBar::ActionID id, bool enable) {
	std::map<MenuBar::ActionID, std::list<wxMenuItem*>>::iterator fi = items.find(id);
	if (fi == items.end()) {
		return;
	}

	std::list<wxMenuItem*>& li = fi->second;

	for (std::list<wxMenuItem*>::iterator i = li.begin(); i != li.end(); ++i) {
		(*i)->Enable(enable);
	}
}

void MainMenuBar::CheckItem(MenuBar::ActionID id, bool enable) {
	std::map<MenuBar::ActionID, std::list<wxMenuItem*>>::iterator fi = items.find(id);
	if (fi == items.end()) {
		return;
	}

	std::list<wxMenuItem*>& li = fi->second;

	checking_programmaticly = true;
	for (std::list<wxMenuItem*>::iterator i = li.begin(); i != li.end(); ++i) {
		(*i)->Check(enable);
	}
	checking_programmaticly = false;
}

bool MainMenuBar::IsItemChecked(MenuBar::ActionID id) const {
	std::map<MenuBar::ActionID, std::list<wxMenuItem*>>::const_iterator fi = items.find(id);
	if (fi == items.end()) {
		return false;
	}

	const std::list<wxMenuItem*>& li = fi->second;

	for (std::list<wxMenuItem*>::const_iterator i = li.begin(); i != li.end(); ++i) {
		if ((*i)->IsChecked()) {
			return true;
		}
	}

	return false;
}

#ifdef __WXGTK__
void MainMenuBar::PreToggleGtkCheckHotkey(const wxKeyEvent& event) {
	int flags = wxACCEL_NORMAL;
	if (event.ControlDown()) {
		flags |= wxACCEL_CTRL;
	}
	if (event.ShiftDown()) {
		flags |= wxACCEL_SHIFT;
	}
	if (event.AltDown()) {
		flags |= wxACCEL_ALT;
	}

	for (const wxAcceleratorEntry& entry : gtk_hotkey_entries) {
		if (entry.GetFlags() != flags || entry.GetKeyCode() != event.GetKeyCode()) {
			continue;
		}

		MenuBar::ActionID id = static_cast<MenuBar::ActionID>(entry.GetCommand() - MAIN_FRAME_MENU);
		std::map<MenuBar::ActionID, std::list<wxMenuItem*>>::iterator fi = items.find(id);
		if (fi == items.end()) {
			return;
		}

		// Only checkable items suffer the desync; anything else (e.g.
		// GOTO_PREVIOUS_POSITION) reaches its handler correctly via the
		// accelerator table alone and must be left untouched here.
		bool isCheckable = !fi->second.empty() && fi->second.front()->IsCheckable();
		if (isCheckable) {
			CheckItem(id, !IsItemChecked(id));
		}
		return;
	}
}
#endif

void MainMenuBar::Update() {
	using namespace MenuBar;
	// This updates all buttons and sets them to proper enabled/disabled state

	bool enable = !g_gui.IsWelcomeDialogShown();
	menubar->Enable(enable);
	if (!enable) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	const bool is_live_client = editor && editor->IsLiveClient();

	if (editor) {
		EnableItem(UNDO, editor->actionQueue->canUndo());
		EnableItem(REDO, editor->actionQueue->canRedo());
		EnableItem(PASTE, editor->copybuffer.canPaste() && editor->IsClipboardAllowed());
	} else {
		EnableItem(UNDO, false);
		EnableItem(REDO, false);
		EnableItem(PASTE, false);
	}

	bool loaded = g_gui.IsVersionLoaded();
	bool has_map = editor != nullptr;
	bool has_selection = editor && editor->hasSelection();
	bool is_host = has_map && !is_live_client;
	bool is_local = has_map && !is_live_client;

	bool has_live_client = false;
	if (g_gui.tabbook) {
		for (int i = 0; i < g_gui.GetTabCount(); ++i) {
			auto* mapTab = dynamic_cast<MapTab*>(g_gui.GetTab(i));
			if (mapTab && mapTab->GetEditor() && mapTab->GetEditor()->IsLiveClient()) {
				has_live_client = true;
				break;
			}
		}
	}

	EnableItem(LIVE_CONNECT, loaded && !has_live_client);
	EnableItem(LIVE_DISCONNECT, has_live_client || is_live_client);

	EnableItem(CLOSE, is_local);
	EnableItem(SAVE, is_host);
	EnableItem(SAVE_AS, is_host);
	EnableItem(GENERATE_MAP, false);

	EnableItem(IMPORT_MAP, is_local);
	EnableItem(IMPORT_MONSTERS, is_local);
	EnableItem(IMPORT_MINIMAP, false);
	EnableItem(EXPORT_MINIMAP, is_local);
	EnableItem(EXPORT_TILESETS, loaded);

	EnableItem(FIND_ITEM, is_host);
	EnableItem(FIND_CREATURE, is_host);
	EnableItem(REPLACE_ITEMS, is_local);
	EnableItem(SEARCH_ON_MAP_EVERYTHING, is_host);
	EnableItem(SEARCH_ON_MAP_UNIQUE, is_host);
	EnableItem(SEARCH_ON_MAP_DUPLICATE, is_host);
	EnableItem(SEARCH_ON_MAP_ACTION, is_host);
	EnableItem(SEARCH_ON_MAP_CONTAINER, is_host);
	EnableItem(SEARCH_ON_MAP_WRITEABLE, is_host);
	EnableItem(SEARCH_ON_MAP_KEYHOLE, is_host);
	EnableItem(SEARCH_ON_MAP_DOOR_LEVEL, is_host);
	EnableItem(SEARCH_ON_MAP_DOOR_QUEST_NUMBER, is_host);
	EnableItem(SEARCH_ON_MAP_DOOR_QUEST_VALUE, is_host);
	EnableItem(SEARCH_ON_SELECTION_EVERYTHING, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_UNIQUE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_DUPLICATE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_ACTION, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_CONTAINER, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_WRITEABLE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_KEYHOLE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_DOOR_LEVEL, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_DOOR_QUEST_NUMBER, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_DOOR_QUEST_VALUE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_ITEM, has_selection && is_host);
	EnableItem(REPLACE_ON_SELECTION_ITEMS, has_selection && is_host);
	EnableItem(REMOVE_ON_SELECTION_ITEM, has_selection && is_host);
	EnableItem(EXPORT_CREATURES_IN_SELECTION, has_selection && is_host);
	EnableItem(SELECT_ITEM_FROM_ID, is_host);

	EnableItem(CUT, has_map && editor->IsClipboardAllowed());
	EnableItem(COPY, has_map && editor->IsClipboardAllowed());

	EnableItem(BORDERIZE_SELECTION, has_map && has_selection);
	EnableItem(BORDERIZE_MAP, is_local);
	EnableItem(RANDOMIZE_SELECTION, has_map && has_selection);
	EnableItem(RANDOMIZE_MAP, is_local);
	EnableItem(CONTENT_AWARE_FILL_SELECTION, has_map && has_selection);
	EnableItem(MOVE_SELECTION_UP, has_map && has_selection);
	EnableItem(MOVE_SELECTION_DOWN, has_map && has_selection);
	// Transformations work on the selection, or on a pending paste while one is held
	const bool can_transform = has_map && (has_selection || g_gui.IsPasting());
	EnableItem(ROTATE_SELECTION_CW, can_transform);
	EnableItem(ROTATE_SELECTION_CCW, can_transform);
	EnableItem(FLIP_SELECTION_HORIZONTAL, can_transform);
	EnableItem(FLIP_SELECTION_VERTICAL, can_transform);

	EnableItem(GOTO_PREVIOUS_POSITION, has_map);
	EnableItem(GOTO_POSITION, has_map);
	EnableItem(JUMP_TO_BRUSH, loaded);
	EnableItem(JUMP_TO_ITEM_BRUSH, loaded);
	EnableItem(FIND_BRUSH_WINDOW, loaded);

	EnableItem(MAP_REMOVE_ITEMS, is_host);
	EnableItem(MAP_REMOVE_CORPSES, is_local);
	EnableItem(MAP_REMOVE_UNREACHABLE_TILES, is_local);
	EnableItem(CLEAR_INVALID_HOUSES, is_local);
	EnableItem(CLEAR_MODIFIED_STATE, is_local);

	EnableItem(EDIT_TOWNS, is_local);
	EnableItem(EDIT_ITEMS, false);
	EnableItem(EDIT_MONSTERS, false);

	EnableItem(MAP_CLEANUP, is_local);
	EnableItem(MAP_PROPERTIES, is_local);
	EnableItem(MAP_STATISTICS, is_local);

	EnableItem(NEW_VIEW, has_map);
	EnableItem(ZOOM_IN, has_map);
	EnableItem(ZOOM_OUT, has_map);
	EnableItem(ZOOM_NORMAL, has_map);

	if (has_map) {
		CheckItem(SHOW_SPAWNS, g_settings.getBoolean(Config::SHOW_SPAWNS));
	}

	EnableItem(WIN_MINIMAP, loaded);
	EnableItem(NEW_PALETTE, loaded);
	EnableItem(SELECT_TERRAIN, loaded);
	EnableItem(SELECT_DOODAD, loaded);
	EnableItem(SELECT_ITEM, loaded);
	EnableItem(SELECT_HOUSE, loaded);
	EnableItem(SELECT_CREATURE, loaded);
	EnableItem(SELECT_RAW, loaded);


	EnableItem(DEBUG_VIEW_DAT, loaded);
	EnableItem(DEBUG_BENCHMARK, has_map);

	UpdateFloorMenu();
}

void MainMenuBar::LoadValues() {
	using namespace MenuBar;

	CheckItem(VIEW_TOOLBARS_BRUSHES, g_settings.getBoolean(Config::SHOW_TOOLBAR_BRUSHES));
	CheckItem(VIEW_TOOLBARS_POSITION, g_settings.getBoolean(Config::SHOW_TOOLBAR_POSITION));
	CheckItem(VIEW_TOOLBARS_SIZES, g_settings.getBoolean(Config::SHOW_TOOLBAR_SIZES));
	CheckItem(VIEW_TOOLBARS_STANDARD, g_settings.getBoolean(Config::SHOW_TOOLBAR_STANDARD));
	CheckItem(VIEW_PALETTE_TOOLS, g_settings.getBoolean(Config::SHOW_PALETTE_TOOLS));
	CheckItem(VIEW_PALETTE_BRUSH_SIZE, g_settings.getBoolean(Config::SHOW_PALETTE_BRUSH_SIZE));

	CheckItem(SELECT_MODE_COMPENSATE, g_settings.getBoolean(Config::COMPENSATED_SELECT));
	CheckItem(SELECT_MODE_LASSO, g_settings.getBoolean(Config::LASSO_SELECT));

	if (IsItemChecked(MenuBar::SELECT_MODE_CURRENT)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_LOWER)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_ALL_FLOORS);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_VISIBLE)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_VISIBLE_FLOORS);
	}

	switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
		case SELECT_CURRENT_FLOOR:
			CheckItem(SELECT_MODE_CURRENT, true);
			break;
		case SELECT_ALL_FLOORS:
			CheckItem(SELECT_MODE_LOWER, true);
			break;
		default:
		case SELECT_VISIBLE_FLOORS:
			CheckItem(SELECT_MODE_VISIBLE, true);
			break;
	}

	CheckItem(AUTOMAGIC, g_settings.getBoolean(Config::USE_AUTOMAGIC));

	CheckItem(SHOW_SHADE, g_settings.getBoolean(Config::SHOW_SHADE));
	CheckItem(SHOW_INGAME_BOX, g_settings.getBoolean(Config::SHOW_INGAME_BOX));
	CheckItem(SHOW_LIGHTS, g_settings.getBoolean(Config::SHOW_LIGHTS));
	CheckItem(SHOW_LIGHT_STR, g_settings.getBoolean(Config::SHOW_LIGHT_STR));
	CheckItem(SHOW_TECHNICAL_ITEMS, g_settings.getBoolean(Config::SHOW_TECHNICAL_ITEMS));
	CheckItem(SHOW_ALL_FLOORS, g_settings.getBoolean(Config::SHOW_ALL_FLOORS));
	CheckItem(SHOW_ALL_FLOORS_UNDERGROUND, g_settings.getBoolean(Config::SHOW_ALL_FLOORS_UNDERGROUND));
	CheckItem(GHOST_ITEMS, g_settings.getBoolean(Config::TRANSPARENT_ITEMS));
	CheckItem(GHOST_ITEMS_ON_VOID, g_settings.getBoolean(Config::TRANSPARENT_ITEMS_ON_VOID));
	CheckItem(GHOST_HIGHER_FLOORS, g_settings.getBoolean(Config::TRANSPARENT_FLOORS));
	CheckItem(SHOW_EXTRA, !g_settings.getBoolean(Config::SHOW_EXTRA));
	CheckItem(SHOW_GRID, g_settings.getBoolean(Config::SHOW_GRID));
	CheckItem(SHOW_TILE_COORDINATES, g_settings.getBoolean(Config::SHOW_TILE_COORDINATES));
	CheckItem(SHOW_HUD, g_settings.getBoolean(Config::SHOW_HUD));
	CheckItem(SHOW_AUTOBORDER_INDICATOR, g_settings.getBoolean(Config::SHOW_AUTOBORDER_INDICATOR));
	CheckItem(HIGHLIGHT_ITEMS, g_settings.getBoolean(Config::HIGHLIGHT_ITEMS));
	CheckItem(HIGHLIGHT_LOCKED_DOORS, g_settings.getBoolean(Config::HIGHLIGHT_LOCKED_DOORS));
	CheckItem(SHOW_CREATURES, g_settings.getBoolean(Config::SHOW_CREATURES));
	CheckItem(SHOW_CREATURE_SPAWN_TIME, g_settings.getBoolean(Config::SHOW_CREATURE_SPAWN_TIME));
	CheckItem(SHOW_SPAWNS, g_settings.getBoolean(Config::SHOW_SPAWNS));
	CheckItem(SHOW_SPECIAL, g_settings.getBoolean(Config::SHOW_SPECIAL_TILES));
	CheckItem(SHOW_AS_MINIMAP, g_settings.getBoolean(Config::SHOW_AS_MINIMAP));
	CheckItem(SHOW_ONLY_COLORS, g_settings.getBoolean(Config::SHOW_ONLY_TILEFLAGS));
	CheckItem(SHOW_ONLY_MODIFIED, g_settings.getBoolean(Config::SHOW_ONLY_MODIFIED_TILES));
	CheckItem(SHOW_HOUSES, g_settings.getBoolean(Config::SHOW_HOUSES));
	CheckItem(SHOW_PATHING, g_settings.getBoolean(Config::SHOW_BLOCKING));
	CheckItem(SHOW_TOOLTIPS, g_settings.getBoolean(Config::SHOW_TOOLTIPS));
	CheckItem(SHOW_TOOLTIP_KEY_NUMBER, g_settings.getBoolean(Config::SHOW_TOOLTIP_KEY_NUMBER));
	CheckItem(SHOW_TOOLTIP_DOOR_PROPERTIES, g_settings.getBoolean(Config::SHOW_TOOLTIP_DOOR_PROPERTIES));
	CheckItem(SHOW_PREVIEW, g_settings.getBoolean(Config::SHOW_PREVIEW));
	CheckItem(SHOW_WALL_HOOKS, g_settings.getBoolean(Config::SHOW_WALL_HOOKS));
	CheckItem(SHOW_FISHABLE_WATER, g_settings.getBoolean(Config::SHOW_FISHABLE_WATER));
	CheckItem(SHOW_BORDERS, g_settings.getBoolean(Config::SHOW_BORDERS));
	CheckItem(SHOW_WALLS, g_settings.getBoolean(Config::SHOW_WALLS));
	CheckItem(SHOW_GROUND, g_settings.getBoolean(Config::SHOW_GROUND));
	CheckItem(SHOW_TOWNS, g_settings.getBoolean(Config::SHOW_TOWNS));
	CheckItem(ALWAYS_SHOW_ZONES, g_settings.getBoolean(Config::ALWAYS_SHOW_ZONES));
	CheckItem(EXT_HOUSE_SHADER, g_settings.getBoolean(Config::EXT_HOUSE_SHADER));
	CheckItem(SHOW_INVISIBLE_ITEMS, g_settings.getBoolean(Config::SHOW_INVISIBLE_ITEMS));
}

void MainMenuBar::LoadRecentFiles() {
	recentFiles.Load(g_settings.getConfigObject());

	// Prune entries whose file no longer exists so the File menu / welcome
	// screen never offers dead links. Iterate backwards since removal shifts
	// the remaining indices.
	bool pruned = false;
	for (int i = int(recentFiles.GetCount()) - 1; i >= 0; --i) {
		if (!wxFileName(recentFiles.GetHistoryFile(i)).FileExists()) {
			recentFiles.RemoveFileFromHistory(i);
			pruned = true;
		}
	}
	if (pruned) {
		recentFiles.Save(g_settings.getConfigObject());
	}
}

void MainMenuBar::SaveRecentFiles() {
	recentFiles.Save(g_settings.getConfigObject());
}

void MainMenuBar::AddRecentFile(FileName file) {
	recentFiles.AddFileToHistory(file.GetFullPath());
}

std::vector<wxString> MainMenuBar::GetRecentFiles() {
	std::vector<wxString> files(recentFiles.GetCount());
	for (size_t i = 0; i < recentFiles.GetCount(); ++i) {
		files[i] = recentFiles.GetHistoryFile(i);
	}
	return files;
}

void MainMenuBar::UpdateFloorMenu() {
	// this will have to be changed if you want to have more floors
	// see MAKE_ACTION(FLOOR_0, wxITEM_RADIO, OnChangeFloor);
	if (MAP_MAX_LAYER < 16) {
		if (g_gui.IsEditorOpen()) {
			for (int i = 0; i < MAP_LAYERS; ++i) {
				CheckItem(MenuBar::ActionID(MenuBar::FLOOR_0 + i), false);
			}
			CheckItem(MenuBar::ActionID(MenuBar::FLOOR_0 + g_gui.GetCurrentFloor()), true);
		}
	}
}

bool MainMenuBar::Load(const FileName& path, wxArrayString& warnings, wxString& error) {
	// Open the XML file
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(path.GetFullPath().mb_str());
	if (!result) {
		error = "Could not open " + path.GetFullName() + " (file not found or syntax error)";
		return false;
	}

	pugi::xml_node node = doc.child("menubar");
	if (!node) {
		error = path.GetFullName() + ": Invalid rootheader.";
		return false;
	}

	// Clear the menu
	while (menubar->GetMenuCount() > 0) {
		menubar->Remove(0);
	}

	// Load succeded
	for (pugi::xml_node menuNode = node.first_child(); menuNode; menuNode = menuNode.next_sibling()) {
		// For each child node, load it
		wxObject* i = LoadItem(menuNode, nullptr, warnings, error);
		wxMenu* m = dynamic_cast<wxMenu*>(i);
		if (m) {
			menubar->Append(m, m->GetTitle());
#ifdef __APPLE__
			m->SetTitle(m->GetTitle());
#else
			m->SetTitle("");
#endif
		} else if (i) {
			delete i;
			warnings.push_back(path.GetFullName() + ": Only menus can be subitems of main menu");
		}
	}

#ifdef __WXGTK__
	gtk_hotkey_entries.clear();
	// Edit
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'Z', MAIN_FRAME_MENU + MenuBar::UNDO);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL | wxACCEL_SHIFT, (int)'Z', MAIN_FRAME_MENU + MenuBar::REDO);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'F', MAIN_FRAME_MENU + MenuBar::FIND_ITEM);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL | wxACCEL_SHIFT, (int)'C', MAIN_FRAME_MENU + MenuBar::FIND_CREATURE);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL | wxACCEL_SHIFT, (int)'F', MAIN_FRAME_MENU + MenuBar::REPLACE_ITEMS);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'A', MAIN_FRAME_MENU + MenuBar::AUTOMAGIC);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'B', MAIN_FRAME_MENU + MenuBar::BORDERIZE_SELECTION);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'P', MAIN_FRAME_MENU + MenuBar::GOTO_PREVIOUS_POSITION);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'G', MAIN_FRAME_MENU + MenuBar::GOTO_POSITION);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'J', MAIN_FRAME_MENU + MenuBar::JUMP_TO_BRUSH);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'X', MAIN_FRAME_MENU + MenuBar::CUT);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'C', MAIN_FRAME_MENU + MenuBar::COPY);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'V', MAIN_FRAME_MENU + MenuBar::PASTE);
	// View
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'=', MAIN_FRAME_MENU + MenuBar::ZOOM_IN);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'-', MAIN_FRAME_MENU + MenuBar::ZOOM_OUT);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'0', MAIN_FRAME_MENU + MenuBar::ZOOM_NORMAL);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'Q', MAIN_FRAME_MENU + MenuBar::SHOW_SHADE);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'W', MAIN_FRAME_MENU + MenuBar::SHOW_ALL_FLOORS);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'Q', MAIN_FRAME_MENU + MenuBar::GHOST_ITEMS);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'L', MAIN_FRAME_MENU + MenuBar::GHOST_HIGHER_FLOORS);
	gtk_hotkey_entries.emplace_back(wxACCEL_SHIFT, (int)'I', MAIN_FRAME_MENU + MenuBar::SHOW_INGAME_BOX);
	gtk_hotkey_entries.emplace_back(wxACCEL_SHIFT, (int)'L', MAIN_FRAME_MENU + MenuBar::SHOW_LIGHTS);
	gtk_hotkey_entries.emplace_back(wxACCEL_SHIFT, (int)'G', MAIN_FRAME_MENU + MenuBar::SHOW_GRID);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'V', MAIN_FRAME_MENU + MenuBar::HIGHLIGHT_ITEMS);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'X', MAIN_FRAME_MENU + MenuBar::HIGHLIGHT_LOCKED_DOORS);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'F', MAIN_FRAME_MENU + MenuBar::SHOW_CREATURES);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'S', MAIN_FRAME_MENU + MenuBar::SHOW_SPAWNS);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'E', MAIN_FRAME_MENU + MenuBar::SHOW_SPECIAL);
	gtk_hotkey_entries.emplace_back(wxACCEL_SHIFT, (int)'E', MAIN_FRAME_MENU + MenuBar::SHOW_AS_MINIMAP);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'E', MAIN_FRAME_MENU + MenuBar::SHOW_ONLY_COLORS);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'M', MAIN_FRAME_MENU + MenuBar::SHOW_ONLY_MODIFIED);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL, (int)'H', MAIN_FRAME_MENU + MenuBar::SHOW_HOUSES);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'O', MAIN_FRAME_MENU + MenuBar::SHOW_PATHING);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'Y', MAIN_FRAME_MENU + MenuBar::SHOW_TOOLTIPS);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'L', MAIN_FRAME_MENU + MenuBar::SHOW_PREVIEW);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'K', MAIN_FRAME_MENU + MenuBar::SHOW_WALL_HOOKS);

	// Window
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'M', MAIN_FRAME_MENU + MenuBar::WIN_MINIMAP);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'T', MAIN_FRAME_MENU + MenuBar::SELECT_TERRAIN);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'D', MAIN_FRAME_MENU + MenuBar::SELECT_DOODAD);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'I', MAIN_FRAME_MENU + MenuBar::SELECT_ITEM);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'H', MAIN_FRAME_MENU + MenuBar::SELECT_HOUSE);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'C', MAIN_FRAME_MENU + MenuBar::SELECT_CREATURE);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'R', MAIN_FRAME_MENU + MenuBar::SELECT_RAW);
	gtk_hotkey_entries.emplace_back(wxACCEL_NORMAL, (int)'Z', MAIN_FRAME_MENU + MenuBar::SELECT_EXIT_BUTTON);

	// Edit (selection floor movement)
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL | wxACCEL_SHIFT, WXK_UP, MAIN_FRAME_MENU + MenuBar::MOVE_SELECTION_UP);
	gtk_hotkey_entries.emplace_back(wxACCEL_CTRL | wxACCEL_SHIFT, WXK_DOWN, MAIN_FRAME_MENU + MenuBar::MOVE_SELECTION_DOWN);

	wxAcceleratorTable accelerator(static_cast<int>(gtk_hotkey_entries.size()), gtk_hotkey_entries.data());
	frame->SetAcceleratorTable(accelerator);
#endif

	/*
	// Create accelerator table
	accelerator_table = newd wxAcceleratorTable(accelerators.size(), &accelerators[0]);

	// Tell all clients of the renewed accelerators
	RenewClients();
	*/

	recentFiles.AddFilesToMenu();
	Update();
	LoadValues();
	return true;
}

wxObject* MainMenuBar::LoadItem(pugi::xml_node node, wxMenu* parent, wxArrayString& warnings, wxString& error) {
	pugi::xml_attribute attribute;

	const std::string& nodeName = as_lower_str(node.name());
	if (nodeName == "menu") {
		if (!(attribute = node.attribute("name"))) {
			return nullptr;
		}

		std::string name = attribute.as_string();
		std::replace(name.begin(), name.end(), '$', '&');

		wxMenu* menu = newd wxMenu;
		if ((attribute = node.attribute("special")) && std::string(attribute.as_string()) == "RECENT_FILES") {
			recentFiles.UseMenu(menu);
		} else {
			for (pugi::xml_node menuNode = node.first_child(); menuNode; menuNode = menuNode.next_sibling()) {
				// Load an add each item in order
				LoadItem(menuNode, menu, warnings, error);
			}
		}

		// If we have a parent, add ourselves.
		// If not, we just return the item and the parent function
		// is responsible for adding us to wherever
		if (parent) {
			parent->AppendSubMenu(menu, wxstr(name));
		} else {
			menu->SetTitle((name));
		}
		return menu;
	} else if (nodeName == "item") {
		// We must have a parent when loading items
		if (!parent) {
			return nullptr;
		} else if (!(attribute = node.attribute("name"))) {
			return nullptr;
		}

		std::string name = attribute.as_string();
		std::replace(name.begin(), name.end(), '$', '&');
		if (!(attribute = node.attribute("action"))) {
			return nullptr;
		}

		const std::string& action = attribute.as_string();
		std::string hotkey = node.attribute("hotkey").as_string();
		if (!hotkey.empty()) {
			hotkey = '\t' + hotkey;
		}

		const std::string& help = node.attribute("help").as_string();
		name += hotkey;

		auto it = actions.find(action);
		if (it == actions.end()) {
			warnings.push_back("Invalid action type '" + wxstr(action) + "'.");
			return nullptr;
		}

		const MenuBar::Action& act = *it->second;
		wxAcceleratorEntry* entry = wxAcceleratorEntry::Create(wxstr(hotkey));
		if (entry) {
			delete entry; // accelerators.push_back(entry);
		} else {
			warnings.push_back("Invalid hotkey.");
		}

		wxMenuItem* tmp = parent->Append(
			MAIN_FRAME_MENU + act.id, // ID
			wxstr(name), // Title of button
			wxstr(help), // Help text
			act.kind // Kind of item
		);
		items[MenuBar::ActionID(act.id)].push_back(tmp);
		return tmp;
	} else if (nodeName == "separator") {
		// We must have a parent when loading items
		if (!parent) {
			return nullptr;
		}
		return parent->AppendSeparator();
	}
	return nullptr;
}

void MainMenuBar::OnNew(wxCommandEvent& WXUNUSED(event)) {
	g_gui.NewMap();
}

void MainMenuBar::OnGenerateMap(wxCommandEvent& WXUNUSED(event)) {
	/*
	if(!DoQuerySave()) return;

	std::ostringstream os;
	os << "Untitled-" << untitled_counter << ".otbm";
	++untitled_counter;

	editor.generateMap(wxstr(os.str()));

	g_gui.SetStatusText("Generated newd map");

	g_gui.UpdateTitle();
	g_gui.RefreshPalettes();
	g_gui.UpdateMinimap();
	g_gui.FitViewToMap();
	UpdateMenubar();
	Refresh();
	*/
}

void MainMenuBar::OnOpenRecent(wxCommandEvent& event) {
	FileName fn(recentFiles.GetHistoryFile(event.GetId() - recentFiles.GetBaseId()));
	frame->LoadMap(fn);
}

void MainMenuBar::OnOpen(wxCommandEvent& WXUNUSED(event)) {
	g_gui.OpenMap();
}

void MainMenuBar::OnClose(wxCommandEvent& WXUNUSED(event)) {
	frame->DoQuerySave(true); // It closes the editor too
}

void MainMenuBar::OnSave(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SaveMap();
}

void MainMenuBar::OnSaveAs(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SaveMapAs();
}

void MainMenuBar::OnPreferences(wxCommandEvent& WXUNUSED(event)) {
	PreferencesWindow dialog(frame);
	dialog.ShowModal();
	dialog.Destroy();
}

void MainMenuBar::OnQuit(wxCommandEvent& WXUNUSED(event)) {
	/*
	while(g_gui.IsEditorOpen())
		if(!frame->DoQuerySave(true))
			return;
			*/
	//((Application*)wxTheApp)->Unload();
	g_gui.root->Close();
}

void MainMenuBar::OnImportMap(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(g_gui.GetCurrentEditor());
	wxDialog* importmap = newd ImportMapWindow(frame, *g_gui.GetCurrentEditor());
	importmap->ShowModal();
}

void MainMenuBar::OnImportMonsterData(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog dlg(g_gui.root, "Import monster/npc file", "", "", "*.xml", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		for (uint32_t i = 0; i < paths.GetCount(); ++i) {
			wxString error;
			wxArrayString warnings;
			bool ok = g_creatures.importXMLFromOT(FileName(paths[i]), error, warnings);
			if (ok) {
				g_gui.ListDialog("Monster loader errors", warnings);
			} else {
				wxMessageBox("Error OT data file \"" + paths[i] + "\".\n" + error, "Error", wxOK | wxICON_INFORMATION, g_gui.root);
			}
		}
	}
}

void MainMenuBar::OnImportMinimap(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(g_gui.IsEditorOpen());
	// wxDialog* importmap = newd ImportMapWindow();
	// importmap->ShowModal();
}

void MainMenuBar::OnExportMinimap(wxCommandEvent& WXUNUSED(event)) {
	if (g_gui.GetCurrentEditor()) {
		ExportMiniMapWindow dlg(frame, *g_gui.GetCurrentEditor());
		dlg.ShowModal();
		dlg.Destroy();
	}
}

void MainMenuBar::OnExportTilesets(wxCommandEvent& WXUNUSED(event)) {
	if (g_gui.GetCurrentEditor()) {
		ExportTilesetsWindow dlg(frame, *g_gui.GetCurrentEditor());
		dlg.ShowModal();
		dlg.Destroy();
	}
}

void MainMenuBar::OnDebugViewDat(wxCommandEvent& WXUNUSED(event)) {
	wxDialog dlg(frame, wxID_ANY, "Debug .dat file", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	new DatDebugView(&dlg);
	dlg.ShowModal();
}

void MainMenuBar::OnDebugBenchmark(wxCommandEvent& WXUNUSED(event)) {
	// M3-T1 baseline: read-only micro-benchmark of the getTile hot path that the
	// spatial hash grid (M3-T2) accelerates. Run before/after to compare.
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		g_gui.PopupDialog("Benchmark", "Open a map first.", wxOK);
		return;
	}

	Map& map = editor->getMap();

	// Snapshot the positions of all existing tiles (does not mutate the map).
	std::vector<Position> positions;
	positions.reserve(static_cast<size_t>(map.getTileCount()));
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		if (Tile* tile = (*it)->get()) {
			positions.push_back(tile->getPosition());
		}
	}

	if (positions.empty()) {
		g_gui.PopupDialog("Benchmark", "The map has no tiles to benchmark.", wxOK);
		return;
	}

	const int rounds = 20;
	volatile uint64_t checksum = 0; // volatile so the lookups can't be optimized away
	const auto start = std::chrono::steady_clock::now();
	for (int r = 0; r < rounds; ++r) {
		for (const Position& pos : positions) {
			checksum += reinterpret_cast<uintptr_t>(map.getTile(pos));
		}
	}
	const auto finish = std::chrono::steady_clock::now();

	const double total_ms = std::chrono::duration<double, std::milli>(finish - start).count();
	const uint64_t lookups = static_cast<uint64_t>(rounds) * positions.size();
	const double ns_each = (total_ms * 1.0e6) / static_cast<double>(lookups);

	const wxString message = wxString::Format(
		"getTile() benchmark\n\n"
		"Tiles:          %llu\n"
		"Rounds:         %d\n"
		"Total lookups:  %llu\n"
		"Total time:     %.2f ms\n"
		"Per lookup:     %.1f ns\n",
		static_cast<unsigned long long>(positions.size()),
		rounds,
		static_cast<unsigned long long>(lookups),
		total_ms,
		ns_each
	);
	g_gui.PopupDialog("Benchmark", message, wxOK);
}

void MainMenuBar::OnReloadDataFiles(wxCommandEvent& WXUNUSED(event)) {
	wxString error;
	wxArrayString warnings;
	g_gui.LoadVersion(g_gui.GetCurrentVersionID(), error, warnings, true);
	g_gui.PopupDialog("Error", error, wxOK);
	g_gui.ListDialog("Warnings", warnings);
}

void MainMenuBar::OnListExtensions(wxCommandEvent& WXUNUSED(event)) {
	ExtensionsDialog exts(frame);
	exts.ShowModal();
}

void MainMenuBar::OnGotoWebsite(wxCommandEvent& WXUNUSED(event)) {
	::wxLaunchDefaultBrowser(__SITE_URL__, wxBROWSER_NEW_WINDOW);
}

void MainMenuBar::OnAbout(wxCommandEvent& WXUNUSED(event)) {
	AboutWindow about(frame);
	about.ShowModal();
}

void MainMenuBar::OnLiveConnect(wxCommandEvent& WXUNUSED(event)) {
	g_gui.ConnectToLiveServer();
}

void MainMenuBar::OnLiveDisconnect(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (editor && editor->IsLiveClient()) {
		static_cast<LiveClient&>(editor->GetLive()).closeAndTeardown();
		return;
	}

	if (!g_gui.tabbook) {
		return;
	}

	for (int i = 0; i < g_gui.GetTabCount(); ++i) {
		auto* mapTab = dynamic_cast<MapTab*>(g_gui.GetTab(i));
		if (mapTab) {
			Editor* tabEditor = mapTab->GetEditor();
			if (tabEditor && tabEditor->IsLiveClient()) {
				static_cast<LiveClient&>(tabEditor->GetLive()).closeAndTeardown();
				return;
			}
		}
	}
}

void MainMenuBar::OnUndo(wxCommandEvent& WXUNUSED(event)) {
	g_gui.DoUndo();
}

void MainMenuBar::OnRedo(wxCommandEvent& WXUNUSED(event)) {
	g_gui.DoRedo();
}

namespace OnSearchForItem {
	struct Finder {
		Finder(uint16_t itemId, uint32_t maxCount) :
			itemId(itemId), maxCount(maxCount) { }

		uint16_t itemId;
		uint32_t maxCount;
		std::vector<std::pair<Tile*, Item*>> result;

		bool limitReached() const {
			return result.size() >= (size_t)maxCount;
		}

		void operator()(Map& map, Tile* tile, Item* item, long long done) {
			if (result.size() >= (size_t)maxCount) {
				return;
			}

			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			if (item->getID() == itemId) {
				result.push_back(std::make_pair(tile, item));
			}
		}
	};
}

void MainMenuBar::OnSearchForItem(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Search for Item");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::FIND_ITEM_MODE));
	if (dialog.ShowModal() == wxID_OK) {
		OnSearchForItem::Finder finder(dialog.getResultID(), (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));
		g_gui.CreateLoadBar("Searching map...");

		foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, false);
		std::vector<std::pair<Tile*, Item*>>& result = finder.result;

		g_gui.DestroyLoadBar();

		if (finder.limitReached()) {
			wxString msg;
			msg << "The configured limit has been reached. Only " << finder.maxCount << " results will be displayed.";
			g_gui.PopupDialog("Notice", msg, wxOK);
		}

		SearchResultWindow* window = g_gui.ShowSearchWindow();
		window->Clear();
		for (std::vector<std::pair<Tile*, Item*>>::const_iterator iter = result.begin(); iter != result.end(); ++iter) {
			Tile* tile = iter->first;
			Item* item = iter->second;
			window->AddPosition(wxstr(item->getName()), tile->getPosition());
		}

		g_settings.setInteger(Config::FIND_ITEM_MODE, (int)dialog.getSearchMode());
	}
	dialog.Destroy();
}

namespace OnSearchForCreature {
	struct Finder {
		Finder(const std::string& creatureName, uint32_t maxCount) :
			creatureName(creatureName), maxCount(maxCount) { }

		std::string creatureName;
		uint32_t maxCount;
		std::vector<std::pair<Tile*, Creature*>> result;

		bool limitReached() const {
			return result.size() >= (size_t)maxCount;
		}

		void operator()(Map& map, Tile* tile, Creature* creature, long long done) {
			if (result.size() >= (size_t)maxCount) {
				return;
			}

			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			if (creature->getName() == creatureName) {
				result.push_back(std::make_pair(tile, creature));
			}
		}
	};
}

void MainMenuBar::OnSearchForCreature(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	// Build list of all creature names from the creature database
	wxArrayString creatureNames;
	for (CreatureDatabase::iterator it = g_creatures.begin(); it != g_creatures.end(); ++it) {
		if (it->second && !it->second->missing) {
			creatureNames.Add(wxstr(it->second->name));
		}
	}

	if (creatureNames.IsEmpty()) {
		g_gui.PopupDialog("Error", "No creatures available in the database.", wxOK);
		return;
	}

	creatureNames.Sort();

	wxSingleChoiceDialog dialog(frame, "Select a creature to find on the map:", "Find Creature", creatureNames);
	if (dialog.ShowModal() == wxID_OK) {
		std::string selectedCreature = nstr(dialog.GetStringSelection());
		OnSearchForCreature::Finder finder(selectedCreature, (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));
		g_gui.CreateLoadBar("Searching map for creatures...");

		foreach_CreatureOnMap(g_gui.GetCurrentMap(), finder, false);
		std::vector<std::pair<Tile*, Creature*>>& result = finder.result;

		g_gui.DestroyLoadBar();

		if (finder.limitReached()) {
			wxString msg;
			msg << "The configured limit has been reached. Only " << finder.maxCount << " results will be displayed.";
			g_gui.PopupDialog("Notice", msg, wxOK);
		}

		SearchResultWindow* window = g_gui.ShowSearchWindow();
		window->Clear();
		for (std::vector<std::pair<Tile*, Creature*>>::const_iterator iter = result.begin(); iter != result.end(); ++iter) {
			Tile* tile = iter->first;
			Creature* creature = iter->second;
			window->AddPosition(wxstr(creature->getName()), tile->getPosition());
		}

		if (result.empty()) {
			g_gui.PopupDialog("Notice", wxString("No instances of '") + wxstr(selectedCreature) + "' were found on the map.", wxOK);
		}
	}
}

void MainMenuBar::OnReplaceItems(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	if (MapTab* tab = g_gui.GetCurrentMapTab()) {
		if (MapWindow* window = tab->GetView()) {
			window->ShowReplaceItemsDialog(false);
		}
	}
}

namespace OnSearchForStuff {
	struct Searcher {
		struct DuplicateGroup {
			DuplicateGroup() :
				firstAdded(false) {
			}

			std::vector<Item*> items;
			bool firstAdded;
		};

		Searcher() :
			search_unique(false),
			search_action(false),
			search_container(false),
			search_writeable(false),
			search_duplicate(false),
			search_keyhole(false),
			search_doorLevel(false),
			search_doorQuestNumber(false),
			search_doorQuestValue(false),
			currentTile(nullptr) {
		}

		bool search_unique;
		bool search_action;
		bool search_container;
		bool search_writeable;
		bool search_duplicate;
		bool search_keyhole;
		bool search_doorLevel;
		bool search_doorQuestNumber;
		bool search_doorQuestValue;
		Tile* currentTile;
		std::map<uint16_t, DuplicateGroup> duplicates;
		std::vector<std::pair<Tile*, Item*>> found;

		void operator()(Map& map, Tile* tile, Item* item, long long done) {
			if (done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			if (search_duplicate) {
				if (tile != currentTile) {
					duplicates.clear();
					currentTile = tile;
				}

				DuplicateGroup& group = duplicates[item->getID()];
				if (group.items.empty()) {
					group.firstAdded = false;
				}
				group.items.push_back(item);
				if (group.items.size() == 2) {
					if (!group.firstAdded && group.items[0]) {
						found.push_back(std::make_pair(tile, group.items[0]));
						group.firstAdded = true;
					}
					found.push_back(std::make_pair(tile, item));
				} else if (group.items.size() > 2) {
					found.push_back(std::make_pair(tile, item));
				}
			}

			Container* container;
			Door* door = item->asDoor();
			if ((search_unique && item->getUniqueID() > 0) || (search_action && item->getActionID() > 0) || (search_container && ((container = dynamic_cast<Container*>(item)) && container->getItemCount())) || (search_writeable && item->getText().length() > 0) || (search_keyhole && door && door->getKeyHoleNumber() > 0) || (search_doorLevel && door && door->getDoorLevel() > 0) || (search_doorQuestNumber && door && door->getQuestNumber() > 0) || (search_doorQuestValue && door && door->getQuestValue() > 0)) {
				found.push_back(std::make_pair(tile, item));
			}
		}

		wxString desc(Item* item) {
			wxString label;
			if (item->getUniqueID() > 0) {
				label << "UID:" << item->getUniqueID() << " ";
			}

			if (item->getActionID() > 0) {
				label << "AID:" << item->getActionID() << " ";
			}

			if (search_duplicate) {
				label << "ID:" << item->getID() << " ";
			}

			label << wxstr(item->getName());

			if (dynamic_cast<Container*>(item)) {
				label << " (Container) ";
			}

			if (item->getText().length() > 0) {
				label << " (Text: " << wxstr(item->getText()) << ") ";
			}

			Door* door = item->asDoor();
			if (door) {
				if (door->getKeyHoleNumber() > 0) {
					label << " (Key Hole: " << door->getKeyHoleNumber() << ") ";
				}
				if (door->getDoorLevel() > 0) {
					label << " (Door Level: " << door->getDoorLevel() << ") ";
				}
				if (door->getQuestNumber() > 0) {
					label << " (Quest Number: " << door->getQuestNumber() << ") ";
				}
				if (door->getQuestValue() > 0) {
					label << " (Quest Value: " << door->getQuestValue() << ") ";
				}
			}

			return label;
		}

		void sort() {
			if (search_unique || search_action || search_duplicate || search_keyhole || search_doorLevel || search_doorQuestNumber || search_doorQuestValue) {
				std::sort(found.begin(), found.end(), Searcher::compare);
			}
		}

		static bool compare(const std::pair<Tile*, Item*>& pair1, const std::pair<Tile*, Item*>& pair2) {
			const Item* item1 = pair1.second;
			const Item* item2 = pair2.second;

			if (item1->getUniqueID() != 0 && item2->getUniqueID() != 0) {
				return item1->getUniqueID() < item2->getUniqueID();
			} else if (item1->getUniqueID() != 0) {
				return true; // item1 has a unique ID, item2 does not
			} else if (item2->getUniqueID() != 0) {
				return false; // item2 has a unique ID, item1 does not
			} else if (item1->getActionID() != item2->getActionID()) {
				return item1->getActionID() < item2->getActionID();
			} else {
				const Door* door1 = item1->asDoor();
				const Door* door2 = item2->asDoor();
				if (door1 && door2) {
					if (door1->getKeyHoleNumber() != door2->getKeyHoleNumber()) {
						return door1->getKeyHoleNumber() < door2->getKeyHoleNumber();
					} else if (door1->getDoorLevel() != door2->getDoorLevel()) {
						return door1->getDoorLevel() < door2->getDoorLevel();
					} else if (door1->getQuestNumber() != door2->getQuestNumber()) {
						return door1->getQuestNumber() < door2->getQuestNumber();
					} else if (door1->getQuestValue() != door2->getQuestValue()) {
						return door1->getQuestValue() < door2->getQuestValue();
					}
				}
				return item1->getID() < item2->getID();
			}
		}
	};
}

void MainMenuBar::OnSearchForStuffOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(true, true, true, true, false, false, false, false, false);
}

void MainMenuBar::OnSearchForUniqueOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(true, false, false, false, false, false, false, false, false);
}

void MainMenuBar::OnSearchForDuplicateOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, true, false, false, false, false);
}

void MainMenuBar::OnSearchForActionOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, true, false, false, false, false, false, false, false);
}

void MainMenuBar::OnSearchForContainerOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, true, false, false, false, false, false, false);
}

void MainMenuBar::OnSearchForWriteableOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, true, false, false, false, false, false);
}

void MainMenuBar::OnSearchForKeyHoleOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, true, false, false, false);
}

void MainMenuBar::OnSearchForDoorLevelOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, false, true, false, false);
}

void MainMenuBar::OnSearchForDoorQuestNumberOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, false, false, true, false);
}

void MainMenuBar::OnSearchForDoorQuestValueOnMap(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, false, false, false, true);
}

void MainMenuBar::OnSearchForStuffOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(true, true, true, true, false, false, false, false, false, true);
}

void MainMenuBar::OnSearchForUniqueOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(true, false, false, false, false, false, false, false, false, true);
}

void MainMenuBar::OnSearchForDuplicateOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, true, false, false, false, false, true);
}

void MainMenuBar::OnSearchForActionOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, true, false, false, false, false, false, false, false, true);
}

void MainMenuBar::OnSearchForContainerOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, true, false, false, false, false, false, false, true);
}

void MainMenuBar::OnSearchForWriteableOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, true, false, false, false, false, false, true);
}

void MainMenuBar::OnSearchForKeyHoleOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, true, false, false, false, true);
}

void MainMenuBar::OnSearchForDoorLevelOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, false, true, false, false, true);
}

void MainMenuBar::OnSearchForDoorQuestNumberOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, false, false, true, false, true);
}

void MainMenuBar::OnSearchForDoorQuestValueOnSelection(wxCommandEvent& WXUNUSED(event)) {
	SearchItems(false, false, false, false, false, false, false, false, true, true);
}

void MainMenuBar::OnSearchForItemOnSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Search on Selection");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::FIND_ITEM_MODE));
	if (dialog.ShowModal() == wxID_OK) {
		OnSearchForItem::Finder finder(dialog.getResultID(), (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));
		g_gui.CreateLoadBar("Searching on selected area...");

		foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, true);
		std::vector<std::pair<Tile*, Item*>>& result = finder.result;

		g_gui.DestroyLoadBar();

		if (finder.limitReached()) {
			wxString msg;
			msg << "The configured limit has been reached. Only " << finder.maxCount << " results will be displayed.";
			g_gui.PopupDialog("Notice", msg, wxOK);
		}

		SearchResultWindow* window = g_gui.ShowSearchWindow();
		window->Clear();
		for (std::vector<std::pair<Tile*, Item*>>::const_iterator iter = result.begin(); iter != result.end(); ++iter) {
			Tile* tile = iter->first;
			Item* item = iter->second;
			window->AddPosition(wxstr(item->getName()), tile->getPosition());
		}

		g_settings.setInteger(Config::FIND_ITEM_MODE, (int)dialog.getSearchMode());
	}

	dialog.Destroy();
}

void MainMenuBar::OnReplaceItemsOnSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	if (MapTab* tab = g_gui.GetCurrentMapTab()) {
		if (MapWindow* window = tab->GetView()) {
			window->ShowReplaceItemsDialog(true);
		}
	}
}

void MainMenuBar::OnRemoveItemOnSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Remove Item on Selection");
	if (dialog.ShowModal() == wxID_OK) {
		g_gui.GetCurrentEditor()->actionQueue->clear();
		g_gui.CreateLoadBar("Searching item on selection to remove...");
		OnMapRemoveItems::RemoveItemCondition condition(dialog.getResultID());
		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, true);
		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items removed.";
		g_gui.PopupDialog("Remove Item", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
		g_gui.RefreshView();
	}
	dialog.Destroy();
}

void MainMenuBar::OnSelectItemFromId(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Select Item from ID");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::FIND_ITEM_MODE));
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		Editor* editor = g_gui.GetCurrentEditor();

		g_gui.SetSelectionMode();
		g_gui.CreateLoadBar("Selecting items...");

		Selection& selection = editor->selection;
		selection.start();
		selection.clear();
		selection.commit();

		int64_t count = 0;
		MapIterator tileiter = editor->map.begin();
		MapIterator mapend = editor->map.end();
		while (tileiter != mapend) {
			Tile* tile = (*tileiter)->get();
			if (tile) {
				if (tile->ground && tile->ground->getID() == itemId) {
					selection.add(tile, tile->ground);
					++count;
				}
				for (Item* item : tile->items) {
					if (item && item->getID() == itemId) {
						selection.add(tile, item);
						++count;
					}
				}
			}
			++tileiter;
		}

		selection.finish();

		g_gui.DestroyLoadBar();

		if (count == 0) {
			wxString msg;
			msg << "No items with ID " << itemId << " found on the map.";
			g_gui.PopupDialog("Select Item", msg, wxOK);
		} else {
			wxString ss;
			ss << count << " items selected.";
			g_gui.SetStatusText(ss);
		}
		g_gui.RefreshView();

		g_settings.setInteger(Config::FIND_ITEM_MODE, (int)dialog.getSearchMode());
	}
	dialog.Destroy();
}

void MainMenuBar::OnExportCreaturesInSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	Selection& selection = editor->selection;
	if (selection.size() == 0) {
		g_gui.PopupDialog("Export Creatures", "No tiles selected.", wxOK);
		return;
	}

	Map& map = editor->map;

	// Structure to hold spawn data
	struct SpawnData {
		Position position;
		int radius;
		std::map<std::string, uint32_t> creatureCounts;
		uint32_t totalCreatures;

		SpawnData() : radius(0), totalCreatures(0) {}
	};

	std::vector<SpawnData> spawnDataList;
	uint32_t totalSpawns = 0;
	uint32_t totalCreaturesAllSpawns = 0;

	// Find all regular spawns in the selection
	for (Tile* tile : selection.getTiles()) {
		if (tile && tile->spawn) {
			SpawnData data;
			data.position = tile->getPosition();
			data.radius = tile->spawn->getSize();
			data.totalCreatures = 0;

			int z = tile->getZ();
			int center_x = tile->getX();
			int center_y = tile->getY();
			int start_x = center_x - data.radius;
			int start_y = center_y - data.radius;
			int end_x = center_x + data.radius;
			int end_y = center_y + data.radius;

			// Count creatures within this spawn's radius
			for (int y = start_y; y <= end_y; ++y) {
				for (int x = start_x; x <= end_x; ++x) {
					Tile* creatureTile = map.getTile(x, y, z);
					if (creatureTile && creatureTile->creature) {
						data.creatureCounts[creatureTile->creature->getName()]++;
						data.totalCreatures++;
					}
				}
			}

			spawnDataList.push_back(data);
			totalSpawns++;
			totalCreaturesAllSpawns += data.totalCreatures;
		}
	}

	if (totalSpawns == 0) {
		g_gui.PopupDialog("Export Creatures", "No spawns found in the selection.", wxOK);
		return;
	}

	// Show save dialog
	wxFileDialog dialog(frame, "Export Creatures in Selection...", "", "", "Text Documents (*.txt)|*.txt", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dialog.ShowModal() == wxID_OK) {
		wxFile file(dialog.GetPath(), wxFile::write);
		if (file.IsOpened()) {
			file.Write("Creatures in Selection (by Spawn)\n");
			file.Write("==================================\n\n");

			// Write spawns
			if (!spawnDataList.empty()) {
				for (const auto& spawnData : spawnDataList) {
					wxString spawnHeader;
					spawnHeader << "Spawn at (" << spawnData.position.x << ", " << spawnData.position.y << ", " << spawnData.position.z << ") - Radius: " << spawnData.radius << "\n";
					file.Write(spawnHeader);
					file.Write("----------------------------------\n");

					if (spawnData.creatureCounts.empty()) {
						file.Write("  (no creatures)\n");
					} else {
						// Sort by name for consistent output
						std::vector<std::pair<std::string, uint32_t>> sortedCreatures(spawnData.creatureCounts.begin(), spawnData.creatureCounts.end());
						std::sort(sortedCreatures.begin(), sortedCreatures.end());

						for (const auto& pair : sortedCreatures) {
							wxString line;
							line << "  " << wxstr(pair.first) << ": " << pair.second << "\n";
							file.Write(line);
						}
					}

					wxString subtotalLine;
					subtotalLine << "  Subtotal: " << spawnData.totalCreatures << " creatures\n\n";
					file.Write(subtotalLine);
				}
			}

			file.Write("==================================\n");
			wxString totalLine;
			totalLine << "Spawns: " << totalSpawns << "\n";
			totalLine << "Total creatures: " << totalCreaturesAllSpawns << "\n";
			file.Write(totalLine);

			file.Close();

			wxString msg;
			msg << "Exported " << totalSpawns << " spawns with " << totalCreaturesAllSpawns << " total creatures to file.";
			g_gui.PopupDialog("Export Creatures", msg, wxOK);
		} else {
			g_gui.PopupDialog("Error", "Failed to open file for writing.", wxOK | wxICON_ERROR);
		}
	}
}

void MainMenuBar::OnSelectionTypeChange(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::COMPENSATED_SELECT, IsItemChecked(MenuBar::SELECT_MODE_COMPENSATE));
	g_settings.setInteger(Config::LASSO_SELECT, IsItemChecked(MenuBar::SELECT_MODE_LASSO));

	if (IsItemChecked(MenuBar::SELECT_MODE_CURRENT)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_LOWER)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_ALL_FLOORS);
	} else if (IsItemChecked(MenuBar::SELECT_MODE_VISIBLE)) {
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_VISIBLE_FLOORS);
	}
}

void MainMenuBar::OnCopy(wxCommandEvent& WXUNUSED(event)) {
	g_gui.DoCopy();
}

void MainMenuBar::OnCut(wxCommandEvent& WXUNUSED(event)) {
	g_gui.DoCut();
}

void MainMenuBar::OnPaste(wxCommandEvent& WXUNUSED(event)) {
	g_gui.PreparePaste();
}

void MainMenuBar::OnToggleAutomagic(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::USE_AUTOMAGIC, IsItemChecked(MenuBar::AUTOMAGIC));
	g_settings.setInteger(Config::BORDER_IS_GROUND, IsItemChecked(MenuBar::AUTOMAGIC));
	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		g_gui.SetStatusText("Automagic enabled.");
	} else {
		g_gui.SetStatusText("Automagic disabled.");
	}
}

void MainMenuBar::OnBorderizeSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->borderizeSelection();
	g_gui.RefreshView();
}

void MainMenuBar::OnBorderizeMap(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ret = g_gui.PopupDialog("Borderize Map", "Are you sure you want to borderize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if (ret == wxID_YES) {
		g_gui.GetCurrentEditor()->borderizeMap(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnRandomizeSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->randomizeSelection();
	g_gui.RefreshView();
}

void MainMenuBar::OnRandomizeMap(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ret = g_gui.PopupDialog("Randomize Map", "Are you sure you want to randomize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if (ret == wxID_YES) {
		g_gui.GetCurrentEditor()->randomizeMap(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnContentAwareFillSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	wxDialog* dg = newd wxDialog(frame, wxID_ANY, "Content-Aware Fill", wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX);
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxCheckBox* ignore_borders_box = newd wxCheckBox(dg, wxID_ANY, "Ignore borders");
	ignore_borders_box->SetValue(g_settings.getInteger(Config::CONTENT_AWARE_FILL_IGNORE_BORDERS) != 0);
	ignore_borders_box->SetToolTip("Leave all existing borders exactly as they are - the fill won't remove, place or rebuild any borders.");
	topsizer->Add(ignore_borders_box, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 10));

	wxCheckBox* ignore_ground_box = newd wxCheckBox(dg, wxID_ANY, "Ignore ground tiles");
	ignore_ground_box->SetValue(g_settings.getInteger(Config::CONTENT_AWARE_FILL_IGNORE_GROUND) != 0);
	ignore_ground_box->SetToolTip("Leave the existing ground exactly as it is - the fill only replaces the items on top of it.");
	topsizer->Add(ignore_ground_box, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 10));

	wxSizer* choicesizer = newd wxBoxSizer(wxHORIZONTAL);
	choicesizer->Add(newd wxButton(dg, wxID_OK, "Fill"), wxSizerFlags(1).Center().Border(wxALL, 5));
	choicesizer->Add(newd wxButton(dg, wxID_CANCEL, "Cancel"), wxSizerFlags(1).Center().Border(wxALL, 5));
	topsizer->Add(choicesizer, wxSizerFlags(0).Center().Border(wxALL, 5));
	dg->SetSizerAndFit(topsizer);
	dg->Centre(wxBOTH);

	const int ret = dg->ShowModal();
	const bool ignore_borders = ignore_borders_box->GetValue();
	const bool ignore_ground = ignore_ground_box->GetValue();
	dg->Destroy();

	if (ret != wxID_OK) {
		return;
	}

	g_settings.setInteger(Config::CONTENT_AWARE_FILL_IGNORE_BORDERS, ignore_borders ? 1 : 0);
	g_settings.setInteger(Config::CONTENT_AWARE_FILL_IGNORE_GROUND, ignore_ground ? 1 : 0);

	g_gui.GetCurrentEditor()->contentAwareFillSelection(ignore_borders, ignore_ground);
	g_gui.RefreshView();
}

void MainMenuBar::OnMoveSelectionUp(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->moveSelection(Position(0, 0, 1));
	g_gui.RefreshView();
}

void MainMenuBar::OnMoveSelectionDown(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->moveSelection(Position(0, 0, -1));
	g_gui.RefreshView();
}

// While a paste is pending the transformation applies to the paste preview,
// otherwise it applies to the selected area on the map.
static void ApplyMapTransform(MapTransform transform) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	if (g_gui.TransformPaste(transform)) {
		return;
	}

	g_gui.GetCurrentEditor()->transformSelection(transform);
	g_gui.RefreshView();
}

void MainMenuBar::OnRotateSelectionClockwise(wxCommandEvent& WXUNUSED(event)) {
	ApplyMapTransform(MapTransform::RotateClockwise);
}

void MainMenuBar::OnRotateSelectionCounterClockwise(wxCommandEvent& WXUNUSED(event)) {
	ApplyMapTransform(MapTransform::RotateCounterClockwise);
}

void MainMenuBar::OnFlipSelectionHorizontal(wxCommandEvent& WXUNUSED(event)) {
	ApplyMapTransform(MapTransform::FlipHorizontal);
}

void MainMenuBar::OnFlipSelectionVertical(wxCommandEvent& WXUNUSED(event)) {
	ApplyMapTransform(MapTransform::FlipVertical);
}

void MainMenuBar::OnJumpToBrush(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	// Create the jump to dialog
	FindDialog* dlg = newd FindBrushDialog(frame);

	// Display dialog to user
	dlg->ShowModal();

	// Retrieve result, if null user canceled
	const Brush* brush = dlg->getResult();
	if (brush) {
		g_gui.SelectBrush(brush, TILESET_UNKNOWN);
	}
	delete dlg;
}

void MainMenuBar::OnFindBrushWindow(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	g_gui.ShowFindBrushWindow();
}

void MainMenuBar::OnJumpToItemBrush(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsVersionLoaded()) {
		return;
	}

	// Create the jump to dialog
	FindItemDialog dialog(frame, "Jump to Item");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::JUMP_TO_ITEM_MODE));
	if (dialog.ShowModal() == wxID_OK) {
		// Retrieve result, if null user canceled
		const Brush* brush = dialog.getResult();
		if (brush) {
			g_gui.SelectBrush(brush, TILESET_RAW);
		}
		g_settings.setInteger(Config::JUMP_TO_ITEM_MODE, (int)dialog.getSearchMode());
	}
	dialog.Destroy();
}

void MainMenuBar::OnGotoPreviousPosition(wxCommandEvent& WXUNUSED(event)) {
	MapTab* mapTab = g_gui.GetCurrentMapTab();
	if (mapTab) {
		mapTab->GoToPreviousCenterPosition();
	}
}

void MainMenuBar::OnGotoPosition(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	// Display dialog, it also controls the actual jump
	GotoPositionDialog dlg(frame, *g_gui.GetCurrentEditor());
	dlg.ShowModal();
}

void MainMenuBar::OnMapRemoveItems(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Item Type to Remove");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemid = dialog.getResultID();

		g_gui.GetCurrentEditor()->selection.clear();
		g_gui.GetCurrentEditor()->actionQueue->clear();

		OnMapRemoveItems::RemoveItemCondition condition(itemid);
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";

		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
		g_gui.RefreshView();
	}
	dialog.Destroy();
}

namespace OnMapRemoveCorpses {
	struct condition {
		condition() { }

		bool operator()(Map& map, Item* item, long long removed, long long done) {
			if (done % 0x800 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}

			return g_materials.isInTileset(item, "Corpses") & !item->isComplex();
		}
	};
}

void MainMenuBar::OnMapRemoveCorpses(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Corpses", "Do you want to remove all corpses from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentEditor()->selection.clear();
		g_gui.GetCurrentEditor()->actionQueue->clear();

		OnMapRemoveCorpses::condition func;
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), func, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";
		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
	}
}

namespace OnMapRemoveUnreachable {
	struct condition {
		condition() { }

		bool isReachable(Tile* tile) {
			if (tile == nullptr) {
				return false;
			}
			if (!tile->isBlocking()) {
				return true;
			}
			return false;
		}

		bool operator()(Map& map, Tile* tile, long long removed, long long done, long long total) {
			if (done % 0x1000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / total));
			}

			Position pos = tile->getPosition();
			int sx = std::max(pos.x - 10, 0);
			int ex = std::min(pos.x + 10, 65535);
			int sy = std::max(pos.y - 8, 0);
			int ey = std::min(pos.y + 8, 65535);
			int sz, ez;

			if (pos.z <= GROUND_LAYER) {
				sz = 0;
				ez = 9;
			} else {
				// underground
				sz = std::max(pos.z - 2, GROUND_LAYER);
				ez = std::min(pos.z + 2, MAP_MAX_LAYER);
			}

			for (int z = sz; z <= ez; ++z) {
				for (int y = sy; y <= ey; ++y) {
					for (int x = sx; x <= ex; ++x) {
						if (isReachable(map.getTile(x, y, z))) {
							return false;
						}
					}
				}
			}
			return true;
		}
	};
}

void MainMenuBar::OnMapRemoveUnreachable(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Unreachable Tiles", "Do you want to remove all unreachable items from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentEditor()->selection.clear();
		g_gui.GetCurrentEditor()->actionQueue->clear();

		OnMapRemoveUnreachable::condition func;
		g_gui.CreateLoadBar("Searching map for tiles to remove...");

		long long removed = remove_if_TileOnMap(g_gui.GetCurrentMap(), func);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " tiles deleted.";

		g_gui.PopupDialog("Search completed", msg, wxOK);

		g_gui.GetCurrentMap().doChange();
	}
}

void MainMenuBar::OnClearHouseTiles(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = g_gui.PopupDialog(
		"Clear Invalid House Tiles",
		"Are you sure you want to remove all house tiles that do not belong to a house (this action cannot be undone)?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		editor->clearInvalidHouseTiles(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnClearModifiedState(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = g_gui.PopupDialog(
		"Clear Modified State",
		"This will have the same effect as closing the map and opening it again. Do you want to proceed?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		editor->clearModifiedTileState(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnMapCleanHouseItems(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = g_gui.PopupDialog(
		"Clear Moveable House Items",
		"Are you sure you want to remove all items inside houses that can be moved (this action cannot be undone)?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		// editor->removeHouseItems(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnMapEditTowns(wxCommandEvent& WXUNUSED(event)) {
	if (g_gui.GetCurrentEditor()) {
		wxDialog* town_dialog = newd EditTownsDialog(frame, *g_gui.GetCurrentEditor());
		town_dialog->ShowModal();
		town_dialog->Destroy();
	}
}

void MainMenuBar::OnMapEditItems(wxCommandEvent& WXUNUSED(event)) {
	;
}

void MainMenuBar::OnMapEditMonsters(wxCommandEvent& WXUNUSED(event)) {
	;
}

void MainMenuBar::OnMapStatistics(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.CreateLoadBar("Collecting data...");

	Map* map = &g_gui.GetCurrentMap();

	int load_counter = 0;

	uint64_t tile_count = 0;
	uint64_t detailed_tile_count = 0;
	uint64_t blocking_tile_count = 0;
	uint64_t walkable_tile_count = 0;
	double percent_pathable = 0.0;
	double percent_detailed = 0.0;
	uint64_t spawn_count = 0;
	uint64_t creature_count = 0;
	double creatures_per_spawn = 0.0;

	uint64_t item_count = 0;
	uint64_t loose_item_count = 0;
	uint64_t depot_count = 0;
	uint64_t action_item_count = 0;
	uint64_t unique_item_count = 0;
	uint64_t container_count = 0; // Only includes containers containing more than 1 item

	int town_count = map->towns.count();
	int house_count = map->houses.count();
	std::map<uint32_t, uint32_t> town_sqm_count;
	const Town* largest_town = nullptr;
	uint64_t largest_town_size = 0;
	uint64_t total_house_sqm = 0;
	const House* largest_house = nullptr;
	uint64_t largest_house_size = 0;
	double houses_per_town = 0.0;
	double sqm_per_house = 0.0;
	double sqm_per_town = 0.0;

	for (MapIterator mit = map->begin(); mit != map->end(); ++mit) {
		Tile* tile = (*mit)->get();
		if (load_counter % 8192 == 0) {
			g_gui.SetLoadDone((unsigned int)(int64_t(load_counter) * 95ll / int64_t(map->getTileCount())));
		}

		if (tile->empty()) {
			continue;
		}

		tile_count += 1;

		bool is_detailed = false;
#define ANALYZE_ITEM(_item)                                         \
	{                                                               \
		item_count += 1;                                            \
		if (!(_item)->isGroundTile() && !(_item)->isBorder()) {     \
			is_detailed = true;                                     \
			ItemType& it = g_items[(_item)->getID()];               \
			if (it.moveable) {                                      \
				loose_item_count += 1;                              \
			}                                                       \
			if (it.isDepot()) {                                     \
				depot_count += 1;                                   \
			}                                                       \
			if ((_item)->getActionID() > 0) {                       \
				action_item_count += 1;                             \
			}                                                       \
			if ((_item)->getUniqueID() > 0) {                       \
				unique_item_count += 1;                             \
			}                                                       \
			if (Container* c = dynamic_cast<Container*>((_item))) { \
				if (c->getVector().size()) {                        \
					container_count += 1;                           \
				}                                                   \
			}                                                       \
		}                                                           \
	}

		if (tile->ground) {
			ANALYZE_ITEM(tile->ground);
		}

		for (ItemVector::const_iterator item_iter = tile->items.begin(); item_iter != tile->items.end(); ++item_iter) {
			Item* item = *item_iter;
			ANALYZE_ITEM(item);
		}
#undef ANALYZE_ITEM

		if (tile->spawn) {
			spawn_count += 1;
		}

		if (tile->creature) {
			creature_count += 1;
		}

		if (tile->isBlocking()) {
			blocking_tile_count += 1;
		} else {
			walkable_tile_count += 1;
		}

		if (is_detailed) {
			detailed_tile_count += 1;
		}

		load_counter += 1;
	}

	creatures_per_spawn = (spawn_count != 0 ? double(creature_count) / double(spawn_count) : -1.0);
	percent_pathable = 100.0 * (tile_count != 0 ? double(walkable_tile_count) / double(tile_count) : -1.0);
	percent_detailed = 100.0 * (tile_count != 0 ? double(detailed_tile_count) / double(tile_count) : -1.0);

	load_counter = 0;
	Houses& houses = map->houses;
	for (HouseMap::const_iterator hit = houses.begin(); hit != houses.end(); ++hit) {
		const House* house = hit->second;

		if (load_counter % 64) {
			g_gui.SetLoadDone((unsigned int)(95ll + int64_t(load_counter) * 5ll / int64_t(house_count)));
		}

		if (house->size() > largest_house_size) {
			largest_house = house;
			largest_house_size = house->size();
		}
		total_house_sqm += house->size();
		town_sqm_count[house->townid] += house->size();
	}

	houses_per_town = (town_count != 0 ? double(house_count) / double(town_count) : -1.0);
	sqm_per_house = (house_count != 0 ? double(total_house_sqm) / double(house_count) : -1.0);
	sqm_per_town = (town_count != 0 ? double(total_house_sqm) / double(town_count) : -1.0);

	Towns& towns = map->towns;
	for (std::map<uint32_t, uint32_t>::iterator town_iter = town_sqm_count.begin();
		 town_iter != town_sqm_count.end();
		 ++town_iter) {
		// No load bar for this, load is non-existant
		uint32_t town_id = town_iter->first;
		uint32_t town_sqm = town_iter->second;
		Town* town = towns.getTown(town_id);
		if (town && town_sqm > largest_town_size) {
			largest_town = town;
			largest_town_size = town_sqm;
		} else {
			// Non-existant town!
		}
	}

	g_gui.DestroyLoadBar();

	std::ostringstream os;
	os.setf(std::ios::fixed, std::ios::floatfield);
	os.precision(2);
	os << "Map statistics for the map \"" << map->getMapDescription() << "\"\n";
	os << "\tTile data:\n";
	os << "\t\tTotal number of tiles: " << tile_count << "\n";
	os << "\t\tNumber of pathable tiles: " << walkable_tile_count << "\n";
	os << "\t\tNumber of unpathable tiles: " << blocking_tile_count << "\n";
	if (percent_pathable >= 0.0) {
		os << "\t\tPercent walkable tiles: " << percent_pathable << "%\n";
	}
	os << "\t\tDetailed tiles: " << detailed_tile_count << "\n";
	if (percent_detailed >= 0.0) {
		os << "\t\tPercent detailed tiles: " << percent_detailed << "%\n";
	}

	os << "\tItem data:\n";
	os << "\t\tTotal number of items: " << item_count << "\n";
	os << "\t\tNumber of moveable tiles: " << loose_item_count << "\n";
	os << "\t\tNumber of depots: " << depot_count << "\n";
	os << "\t\tNumber of containers: " << container_count << "\n";
	os << "\t\tNumber of items with Action ID: " << action_item_count << "\n";
	os << "\t\tNumber of items with Unique ID: " << unique_item_count << "\n";

	os << "\tCreature data:\n";
	os << "\t\tTotal creature count: " << creature_count << "\n";
	os << "\t\tTotal spawn count: " << spawn_count << "\n";
	if (creatures_per_spawn >= 0) {
		os << "\t\tMean creatures per spawn: " << creatures_per_spawn << "\n";
	}

	os << "\tTown/House data:\n";
	os << "\t\tTotal number of towns: " << town_count << "\n";
	os << "\t\tTotal number of houses: " << house_count << "\n";
	if (houses_per_town >= 0) {
		os << "\t\tMean houses per town: " << houses_per_town << "\n";
	}
	os << "\t\tTotal amount of housetiles: " << total_house_sqm << "\n";
	if (sqm_per_house >= 0) {
		os << "\t\tMean tiles per house: " << sqm_per_house << "\n";
	}
	if (sqm_per_town >= 0) {
		os << "\t\tMean tiles per town: " << sqm_per_town << "\n";
	}

	if (largest_town) {
		os << "\t\tLargest Town: \"" << largest_town->getName() << "\" (" << largest_town_size << " sqm)\n";
	}
	if (largest_house) {
		os << "\t\tLargest House: \"" << largest_house->name << "\" (" << largest_house_size << " sqm)\n";
	}

	os << "\n";
	os << "Generated by Remere's Map Editor version " + __RME_VERSION__ + "\n";

	wxDialog* dg = newd wxDialog(frame, wxID_ANY, "Map Statistics", wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxTextCtrl* text_field = newd wxTextCtrl(dg, wxID_ANY, wxstr(os.str()), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	text_field->SetMinSize(wxSize(400, 300));
	topsizer->Add(text_field, wxSizerFlags(5).Expand());

	wxSizer* choicesizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* export_button = newd wxButton(dg, wxID_OK, "Export as XML");
	choicesizer->Add(export_button, wxSizerFlags(1).Center());
	export_button->Enable(false);
	choicesizer->Add(newd wxButton(dg, wxID_CANCEL, "OK"), wxSizerFlags(1).Center());
	topsizer->Add(choicesizer, wxSizerFlags(1).Center());
	dg->SetSizerAndFit(topsizer);
	dg->Centre(wxBOTH);

	int ret = dg->ShowModal();

	if (ret == wxID_OK) {
		// std::cout << "XML EXPORT";
	} else if (ret == wxID_CANCEL) {
		// std::cout << "OK";
	}
}

void MainMenuBar::OnMapCleanup(wxCommandEvent& WXUNUSED(event)) {
	int ok = g_gui.PopupDialog("Clean map", "Do you want to remove all invalid items from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentMap().cleanInvalidTiles(true);
	}
}

void MainMenuBar::OnMapProperties(wxCommandEvent& WXUNUSED(event)) {
	wxDialog* properties = newd MapPropertiesWindow(
		frame,
		static_cast<MapTab*>(g_gui.GetCurrentTab()),
		*g_gui.GetCurrentEditor()
	);

	if (properties->ShowModal() == 0) {
		// FAIL!
		g_gui.CloseAllEditors();
	}
	properties->Destroy();
}

void MainMenuBar::OnToolbars(wxCommandEvent& event) {
	using namespace MenuBar;

	ActionID id = static_cast<ActionID>(event.GetId() - (wxID_HIGHEST + 1));
	switch (id) {
		case VIEW_TOOLBARS_BRUSHES:
			g_gui.ShowToolbar(TOOLBAR_BRUSHES, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_BRUSHES, event.IsChecked());
			break;
		case VIEW_TOOLBARS_POSITION:
			g_gui.ShowToolbar(TOOLBAR_POSITION, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_POSITION, event.IsChecked());
			break;
		case VIEW_TOOLBARS_SIZES:
			g_gui.ShowToolbar(TOOLBAR_SIZES, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_SIZES, event.IsChecked());
			break;
		case VIEW_TOOLBARS_STANDARD:
			g_gui.ShowToolbar(TOOLBAR_STANDARD, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_STANDARD, event.IsChecked());
			break;
		default:
			break;
	}
}

void MainMenuBar::OnPalettePanels(wxCommandEvent& event) {
	using namespace MenuBar;

	ActionID id = static_cast<ActionID>(event.GetId() - (wxID_HIGHEST + 1));
	switch (id) {
		case VIEW_PALETTE_TOOLS:
			g_settings.setInteger(Config::SHOW_PALETTE_TOOLS, event.IsChecked());
			break;
		case VIEW_PALETTE_BRUSH_SIZE:
			g_settings.setInteger(Config::SHOW_PALETTE_BRUSH_SIZE, event.IsChecked());
			break;
		default:
			return;
	}

	for (PaletteWindow* palette : g_gui.GetPalettes()) {
		palette->UpdateToolPanelVisibility();
	}
	if (g_gui.aui_manager) {
		g_gui.aui_manager->Update();
	}
}

void MainMenuBar::OnNewView(wxCommandEvent& WXUNUSED(event)) {
	g_gui.NewMapView();
}

void MainMenuBar::OnToggleFullscreen(wxCommandEvent& WXUNUSED(event)) {
	if (frame->IsFullScreen()) {
		frame->ShowFullScreen(false);
	} else {
		frame->ShowFullScreen(true, wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);
	}
}

void MainMenuBar::OnTakeScreenshot(wxCommandEvent& WXUNUSED(event)) {
	wxString path = wxstr(g_settings.getString(Config::SCREENSHOT_DIRECTORY));
	if (path.size() > 0 && (path.Last() == '/' || path.Last() == '\\')) {
		path = path + "/";
	}

	g_gui.GetCurrentMapTab()->GetView()->GetCanvas()->TakeScreenshot(
		path, wxstr(g_settings.getString(Config::SCREENSHOT_FORMAT))
	);
}

void MainMenuBar::OnZoomIn(wxCommandEvent& event) {
	double zoom = g_gui.GetCurrentZoom();
	g_gui.SetCurrentZoom(zoom - 0.1);
}

void MainMenuBar::OnZoomOut(wxCommandEvent& event) {
	double zoom = g_gui.GetCurrentZoom();
	g_gui.SetCurrentZoom(zoom + 0.1);
}

void MainMenuBar::OnZoomNormal(wxCommandEvent& event) {
	g_gui.SetCurrentZoom(1.0);
}

void MainMenuBar::OnChangeViewSettings(wxCommandEvent& event) {
	g_settings.setInteger(Config::SHOW_ALL_FLOORS, IsItemChecked(MenuBar::SHOW_ALL_FLOORS));
	if (IsItemChecked(MenuBar::SHOW_ALL_FLOORS)) {
		EnableItem(MenuBar::SELECT_MODE_VISIBLE, true);
		EnableItem(MenuBar::SELECT_MODE_LOWER, true);
		EnableItem(MenuBar::SHOW_ALL_FLOORS_UNDERGROUND, true);
	} else {
		EnableItem(MenuBar::SELECT_MODE_VISIBLE, false);
		EnableItem(MenuBar::SELECT_MODE_LOWER, false);
		EnableItem(MenuBar::SHOW_ALL_FLOORS_UNDERGROUND, false);
		CheckItem(MenuBar::SELECT_MODE_CURRENT, true);
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	}
	g_settings.setInteger(Config::SHOW_ALL_FLOORS_UNDERGROUND, IsItemChecked(MenuBar::SHOW_ALL_FLOORS_UNDERGROUND));
	g_settings.setInteger(Config::TRANSPARENT_FLOORS, IsItemChecked(MenuBar::GHOST_HIGHER_FLOORS));
	g_settings.setInteger(Config::TRANSPARENT_ITEMS, IsItemChecked(MenuBar::GHOST_ITEMS));
	g_settings.setInteger(Config::TRANSPARENT_ITEMS_ON_VOID, IsItemChecked(MenuBar::GHOST_ITEMS_ON_VOID));
	g_settings.setInteger(Config::SHOW_INGAME_BOX, IsItemChecked(MenuBar::SHOW_INGAME_BOX));
	g_settings.setInteger(Config::SHOW_LIGHTS, IsItemChecked(MenuBar::SHOW_LIGHTS));
	g_settings.setInteger(Config::SHOW_LIGHT_STR, IsItemChecked(MenuBar::SHOW_LIGHT_STR));
	g_settings.setInteger(Config::SHOW_TECHNICAL_ITEMS, IsItemChecked(MenuBar::SHOW_TECHNICAL_ITEMS));
	g_settings.setInteger(Config::SHOW_GRID, IsItemChecked(MenuBar::SHOW_GRID));
	g_settings.setInteger(Config::SHOW_TILE_COORDINATES, IsItemChecked(MenuBar::SHOW_TILE_COORDINATES));
	g_settings.setInteger(Config::SHOW_HUD, IsItemChecked(MenuBar::SHOW_HUD));
	g_settings.setInteger(Config::SHOW_AUTOBORDER_INDICATOR, IsItemChecked(MenuBar::SHOW_AUTOBORDER_INDICATOR));
	g_settings.setInteger(Config::SHOW_EXTRA, !IsItemChecked(MenuBar::SHOW_EXTRA));

	g_settings.setInteger(Config::SHOW_SHADE, IsItemChecked(MenuBar::SHOW_SHADE));
	g_settings.setInteger(Config::SHOW_SPECIAL_TILES, IsItemChecked(MenuBar::SHOW_SPECIAL));
	g_settings.setInteger(Config::SHOW_AS_MINIMAP, IsItemChecked(MenuBar::SHOW_AS_MINIMAP));
	g_settings.setInteger(Config::SHOW_ONLY_TILEFLAGS, IsItemChecked(MenuBar::SHOW_ONLY_COLORS));
	g_settings.setInteger(Config::SHOW_ONLY_MODIFIED_TILES, IsItemChecked(MenuBar::SHOW_ONLY_MODIFIED));
	g_settings.setInteger(Config::SHOW_CREATURES, IsItemChecked(MenuBar::SHOW_CREATURES));
	g_settings.setInteger(Config::SHOW_CREATURE_SPAWN_TIME, IsItemChecked(MenuBar::SHOW_CREATURE_SPAWN_TIME));
	g_settings.setInteger(Config::SHOW_SPAWNS, IsItemChecked(MenuBar::SHOW_SPAWNS));
	g_settings.setInteger(Config::SHOW_HOUSES, IsItemChecked(MenuBar::SHOW_HOUSES));
	g_settings.setInteger(Config::HIGHLIGHT_ITEMS, IsItemChecked(MenuBar::HIGHLIGHT_ITEMS));
	g_settings.setInteger(Config::HIGHLIGHT_LOCKED_DOORS, IsItemChecked(MenuBar::HIGHLIGHT_LOCKED_DOORS));
	g_settings.setInteger(Config::SHOW_BLOCKING, IsItemChecked(MenuBar::SHOW_PATHING));
	g_settings.setInteger(Config::SHOW_TOOLTIPS, IsItemChecked(MenuBar::SHOW_TOOLTIPS));
	g_settings.setInteger(Config::SHOW_TOOLTIP_KEY_NUMBER, IsItemChecked(MenuBar::SHOW_TOOLTIP_KEY_NUMBER));
	g_settings.setInteger(Config::SHOW_TOOLTIP_DOOR_PROPERTIES, IsItemChecked(MenuBar::SHOW_TOOLTIP_DOOR_PROPERTIES));
	g_settings.setInteger(Config::SHOW_PREVIEW, IsItemChecked(MenuBar::SHOW_PREVIEW));
	g_settings.setInteger(Config::SHOW_WALL_HOOKS, IsItemChecked(MenuBar::SHOW_WALL_HOOKS));
	g_settings.setInteger(Config::SHOW_FISHABLE_WATER, IsItemChecked(MenuBar::SHOW_FISHABLE_WATER));
	g_settings.setInteger(Config::SHOW_BORDERS, IsItemChecked(MenuBar::SHOW_BORDERS));
	g_settings.setInteger(Config::SHOW_WALLS, IsItemChecked(MenuBar::SHOW_WALLS));
	g_settings.setInteger(Config::SHOW_GROUND, IsItemChecked(MenuBar::SHOW_GROUND));
	g_settings.setInteger(Config::SHOW_TOWNS, IsItemChecked(MenuBar::SHOW_TOWNS));
	g_settings.setInteger(Config::ALWAYS_SHOW_ZONES, IsItemChecked(MenuBar::ALWAYS_SHOW_ZONES));
	g_settings.setInteger(Config::EXT_HOUSE_SHADER, IsItemChecked(MenuBar::EXT_HOUSE_SHADER));
	g_settings.setInteger(Config::SHOW_INVISIBLE_ITEMS, IsItemChecked(MenuBar::SHOW_INVISIBLE_ITEMS));

	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MainMenuBar::OnChangeFloor(wxCommandEvent& event) {
	// Workaround to stop events from looping
	if (checking_programmaticly) {
		return;
	}

	// this will have to be changed if you want to have more floors
	// see MAKE_ACTION(FLOOR_0, wxITEM_RADIO, OnChangeFloor);
	if (MAP_MAX_LAYER < 16) {
		for (int i = 0; i < MAP_LAYERS; ++i) {
			if (IsItemChecked(MenuBar::ActionID(MenuBar::FLOOR_0 + i))) {
				g_gui.ChangeFloor(i);
			}
		}
	}
}

void MainMenuBar::OnMinimapWindow(wxCommandEvent& event) {
	g_gui.CreateMinimap();
}

void MainMenuBar::OnNewPalette(wxCommandEvent& event) {
	g_gui.NewPalette();
}

void MainMenuBar::OnSelectTerrainPalette(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_TERRAIN);
}

void MainMenuBar::OnSelectDoodadPalette(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_DOODAD);
}

void MainMenuBar::OnSelectItemPalette(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_ITEM);
}


void MainMenuBar::OnSelectHousePalette(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_HOUSE);
}

void MainMenuBar::OnSelectCreaturePalette(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_CREATURE);
}


void MainMenuBar::OnSelectRawPalette(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SelectPalettePage(TILESET_RAW);
}


void MainMenuBar::SearchItems(bool unique, bool action, bool container, bool writable, bool duplicate, bool keyhole, bool doorLevel, bool doorQuestNumber, bool doorQuestValue, bool onSelection /* = false*/) {
	if (!unique && !action && !container && !writable && !duplicate && !keyhole && !doorLevel && !doorQuestNumber && !doorQuestValue) {
		return;
	}

	if (!g_gui.IsEditorOpen()) {
		return;
	}

	if (onSelection) {
		g_gui.CreateLoadBar("Searching on selected area...");
	} else {
		g_gui.CreateLoadBar("Searching on map...");
	}

	OnSearchForStuff::Searcher searcher;
	searcher.search_unique = unique;
	searcher.search_action = action;
	searcher.search_container = container;
	searcher.search_writeable = writable;
	searcher.search_duplicate = duplicate;
	searcher.search_keyhole = keyhole;
	searcher.search_doorLevel = doorLevel;
	searcher.search_doorQuestNumber = doorQuestNumber;
	searcher.search_doorQuestValue = doorQuestValue;

	foreach_ItemOnMap(g_gui.GetCurrentMap(), searcher, onSelection);

	// Sort the found items
	searcher.sort();

	std::vector<std::pair<Tile*, Item*>>& found = searcher.found;

	g_gui.DestroyLoadBar();

	SearchResultWindow* result = g_gui.ShowSearchWindow();
	result->Clear();
	for (std::vector<std::pair<Tile*, Item*>>::iterator iter = found.begin(); iter != found.end(); ++iter) {
		result->AddPosition(searcher.desc(iter->second), iter->first->getPosition());
	}
}
