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

#include <sstream>
#include <time.h>
#include <limits>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <wx/wfstream.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/colordlg.h>

#include "gui.h"
#include "editor.h"
#include "live_client.h"
#include "live_session_bounds.h"
#include "brush.h"
#include "sprites.h"
#include "map.h"
#include "tile.h"
#include "old_properties_window.h"
#include "properties_window.h"
#include "tileset_window.h"
#include "palette_window.h"
#include "map_display.h"
#include "map_drawer.h"
#include "map_comment.h"
#include "application.h"
#include "browse_tile_window.h"
#include "wall_creator_window.h"
#include "replace_wall_window.h"
#include "replace_ground_window.h"
#include "find_item_window.h"
#include "main_menubar.h"

#include "doodad_brush.h"
#include "house_exit_brush.h"
#include "house_brush.h"
#include "wall_brush.h"
#include "spawn_brush.h"
#include "creature_brush.h"
#include "ground_brush.h"
#include "raw_brush.h"
#include "carpet_brush.h"
#include "table_brush.h"

namespace {

// A lasso needs one byte of scratch per tile in its bounding box. Traces that
// large only come from scrolling the view around for a very long drag, and the
// resulting selection would be unusable anyway, so refuse rather than allocate.
constexpr size_t MAX_LASSO_AREA = 8u * 1024u * 1024u;

// Sorts and removes duplicate positions in place. Callers that build a tile list
// out of overlapping pieces need this: Editor::draw walks the list emitting one
// change per entry, so a repeated tile means two changes for it in one action.
void dedupePositions(PositionVector& positions) {
	std::sort(positions.begin(), positions.end());
	positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
}

bool liveEditAllowed(const Editor& editor, int x, int y) {
	if (!editor.IsLive()) {
		return true;
	}
	const LiveSessionBounds& bounds = editor.GetLive().getSessionBounds();
	return !bounds.enabled || bounds.contains(x, y);
}

// The representative item id of a tile for wall creation: the top selected
// item, otherwise the topmost item, otherwise the ground.
uint16_t wallTileItemId(Tile* tile) {
	if (!tile) {
		return 0;
	}
	ItemVector selected = tile->getSelectedItems();
	if (!selected.empty()) {
		return selected.back()->getID();
	}
	if (!tile->items.empty()) {
		return tile->items.back()->getID();
	}
	if (tile->ground) {
		return tile->ground->getID();
	}
	return 0;
}

// Detects a 2x2 square of selected tiles (same floor) and maps each corner to
// a wall piece, filling pieceIds in the order {horizontal, vertical, corner,
// pole}. Layout: top-left=pole, top-right=horizontal, bottom-left=vertical,
// bottom-right=corner.
bool extractWallSquare(Selection& selection, uint16_t pieceIds[4]) {
	if (selection.size() != 4) {
		return false;
	}

	int minX = std::numeric_limits<int>::max();
	int minY = std::numeric_limits<int>::max();
	int floor = -1;
	for (Tile* tile : selection.getTiles()) {
		const Position pos = tile->getPosition();
		if (floor == -1) {
			floor = pos.z;
		} else if (pos.z != floor) {
			return false;
		}
		minX = std::min(minX, pos.x);
		minY = std::min(minY, pos.y);
	}

	uint16_t corner = 0, horizontal = 0, vertical = 0, pole = 0;
	for (Tile* tile : selection.getTiles()) {
		const Position pos = tile->getPosition();
		const bool right = (pos.x == minX + 1);
		const bool bottom = (pos.y == minY + 1);
		if ((pos.x != minX && !right) || (pos.y != minY && !bottom)) {
			return false; // not within the 2x2 footprint
		}

		const uint16_t id = wallTileItemId(tile);
		if (id == 0) {
			return false;
		}

		if (!right && !bottom) {
			pole = id;
		} else if (right && !bottom) {
			horizontal = id;
		} else if (!right && bottom) {
			vertical = id;
		} else {
			corner = id;
		}
	}

	// All four distinct corners must be filled.
	if (corner == 0 || horizontal == 0 || vertical == 0 || pole == 0) {
		return false;
	}

	pieceIds[0] = horizontal;
	pieceIds[1] = vertical;
	pieceIds[2] = corner;
	pieceIds[3] = pole;
	return true;
}

void getVisibleMapBounds(const MapCanvas& canvas, int& start_x, int& start_y, int& end_x, int& end_y, int& start_z, int& end_z) {
	int view_scroll_x = 0;
	int view_scroll_y = 0;
	int screensize_x = 0;
	int screensize_y = 0;
	canvas.GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	const double zoom = canvas.GetZoom();
	const int current_floor = canvas.GetFloor();
	const int tile_size = std::max(1, int(TileSize / zoom));

	if (g_settings.getBoolean(Config::SHOW_ALL_FLOORS)) {
		if (current_floor <= GROUND_LAYER) {
			start_z = GROUND_LAYER;
		} else if (g_settings.getBoolean(Config::SHOW_ALL_FLOORS_UNDERGROUND)) {
			start_z = MAP_MAX_LAYER;
		} else {
			start_z = std::min(MAP_MAX_LAYER, current_floor + 2);
		}
	} else {
		start_z = current_floor;
	}
	end_z = current_floor;

	start_x = view_scroll_x / TileSize;
	start_y = view_scroll_y / TileSize;
	if (current_floor > GROUND_LAYER) {
		start_x -= 2;
		start_y -= 2;
	}
	end_x = start_x + screensize_x / tile_size + 2;
	end_y = start_y + screensize_y / tile_size + 2;

	const int floor_expansion = start_z - end_z;
	start_x -= floor_expansion;
	start_y -= floor_expansion;
	end_x += floor_expansion;
	end_y += floor_expansion;
}

bool isItemDrawnInView(const Item* item, double zoom) {
	if (!item) {
		return false;
	}

	const ItemType& it = g_items.getItemType(item->getID());
	if (it.isMetaItem()) {
		return false;
	}
	if (!g_settings.getBoolean(Config::SHOW_ITEMS) && it.pickupable && !item->isGroundTile()) {
		return false;
	}
	if (g_settings.getBoolean(Config::HIDE_ITEMS_WHEN_ZOOMED) && zoom > 10.0 && !item->isGroundTile()) {
		return false;
	}
	return true;
}

void selectVisibleItemsById(MapCanvas& canvas, Editor& editor, uint16_t itemId) {
	int start_x = 0;
	int start_y = 0;
	int end_x = 0;
	int end_y = 0;
	int start_z = 0;
	int end_z = 0;
	getVisibleMapBounds(canvas, start_x, start_y, end_x, end_y, start_z, end_z);

	const double zoom = canvas.GetZoom();

	editor.selection.start();
	editor.selection.clear();
	editor.selection.commit();

	for (int map_z = start_z; map_z >= end_z; --map_z) {
		for (int map_x = start_x; map_x <= end_x; ++map_x) {
			for (int map_y = start_y; map_y <= end_y; ++map_y) {
				Tile* tile = editor.map.getTile(map_x, map_y, map_z);
				if (!tile) {
					continue;
				}

				auto trySelect = [&](Item* item) {
					if (item && item->getID() == itemId && isItemDrawnInView(item, zoom)) {
						editor.selection.add(tile, item);
					}
				};

				trySelect(tile->ground);
				for (Item* item : tile->items) {
					trySelect(item);
				}
			}
		}
	}

	editor.selection.finish();
	editor.selection.updateSelectionCount();
}

bool selectionHasSelectedItems(Selection& selection) {
	for (Tile* tile : selection.getTiles()) {
		if (tile->ground && tile->ground->isSelected()) {
			return true;
		}
		for (Item* item : tile->items) {
			if (item->isSelected()) {
				return true;
			}
		}
	}
	return false;
}

void replaceSelectedItemsWith(Editor& editor, uint16_t withId) {
	std::map<Tile*, std::vector<Item*>> itemsByTile;
	for (Tile* tile : editor.selection.getTiles()) {
		if (tile->ground && tile->ground->isSelected()) {
			itemsByTile[tile].push_back(tile->ground);
		}
		for (Item* item : tile->items) {
			if (item->isSelected()) {
				itemsByTile[tile].push_back(item);
			}
		}
	}

	Action* action = editor.actionQueue->createAction(ACTION_REPLACE_ITEMS);
	for (const auto& entry : itemsByTile) {
		Tile* origTile = entry.first;
		Tile* new_tile = origTile->deepCopy(editor.map);
		bool changed = false;

		for (Item* origItem : entry.second) {
			const int index = origTile->getIndexOf(origItem);
			if (index == wxNOT_FOUND) {
				continue;
			}
			Item* item = new_tile->getItemAt(index);
			if (!item || item->getID() == withId) {
				continue;
			}
			transformItem(item, withId, new_tile);
			changed = true;
		}

		if (changed) {
			action->addChange(newd Change(new_tile));
		}
	}

	if (action->size() > 0) {
		editor.addAction(action);
	} else {
		delete action;
	}
}

} // namespace

void GetLineTiles(int start_x, int start_y, int end_x, int end_y, int z, PositionVector& tilestodraw, PositionVector* tilestoborder) {
	const int dx = std::abs(end_x - start_x);
	const int dy = std::abs(end_y - start_y);
	const int step_x = start_x < end_x ? 1 : -1;
	const int step_y = start_y < end_y ? 1 : -1;
	const int length = std::max(dx, dy) + 1;

	tilestodraw.reserve(tilestodraw.size() + length);

	std::set<Position> border_tiles;
	int error = dx - dy;
	int x = start_x;
	int y = start_y;
	while (true) {
		tilestodraw.push_back(Position(x, y, z));
		if (tilestoborder) {
			for (int offset_y = -1; offset_y <= 1; ++offset_y) {
				for (int offset_x = -1; offset_x <= 1; ++offset_x) {
					border_tiles.insert(Position(x + offset_x, y + offset_y, z));
				}
			}
		}
		if (x == end_x && y == end_y) {
			break;
		}
		const int double_error = error * 2;
		if (double_error > -dy) {
			error -= dy;
			x += step_x;
		}
		if (double_error < dx) {
			error += dx;
			y += step_y;
		}
	}

	if (tilestoborder) {
		tilestoborder->reserve(tilestoborder->size() + border_tiles.size());
		tilestoborder->insert(tilestoborder->end(), border_tiles.begin(), border_tiles.end());
	}
}

bool GetLassoTiles(const PositionVector& trace, PositionVector& tiles) {
	if (trace.empty()) {
		return true;
	}

	const int z = trace.front().z;

	// Mouse motion events skip tiles whenever the cursor moves faster than one
	// tile per event, so bridge consecutive samples — a gap in the boundary
	// would let the flood fill below leak out. The wrap on the last sample
	// closes the loop, which is what makes an open trace still enclose an area.
	PositionVector boundary;
	boundary.reserve(trace.size() * 2);
	for (size_t i = 0; i < trace.size(); ++i) {
		const Position& from = trace[i];
		const Position& to = trace[(i + 1) % trace.size()];
		GetLineTiles(from.x, from.y, to.x, to.y, z, boundary);
	}

	int min_x = boundary.front().x;
	int min_y = boundary.front().y;
	int max_x = min_x;
	int max_y = min_y;
	for (const Position& pos : boundary) {
		min_x = std::min(min_x, pos.x);
		min_y = std::min(min_y, pos.y);
		max_x = std::max(max_x, pos.x);
		max_y = std::max(max_y, pos.y);
	}

	// Pad by one tile so the flood fill always has an unobstructed ring of
	// outside cells to start from, whatever shape the trace has.
	--min_x;
	--min_y;
	++max_x;
	++max_y;

	const size_t width = static_cast<size_t>(max_x - min_x) + 1;
	const size_t height = static_cast<size_t>(max_y - min_y) + 1;
	if (width * height > MAX_LASSO_AREA) {
		return false;
	}

	enum LassoCell : uint8_t {
		CELL_ENCLOSED = 0,
		CELL_BOUNDARY = 1,
		CELL_OUTSIDE = 2
	};

	std::vector<uint8_t> cells(width * height, CELL_ENCLOSED);
	auto cellIndex = [min_x, min_y, width](int x, int y) -> size_t {
		return static_cast<size_t>(y - min_y) * width + static_cast<size_t>(x - min_x);
	};

	for (const Position& pos : boundary) {
		cells[cellIndex(pos.x, pos.y)] = CELL_BOUNDARY;
	}

	// Flood inwards from the padding ring. The boundary is 8-connected (its
	// lines may step diagonally) and this fill is 4-connected, so the fill can
	// never squeeze through it. Whatever it fails to reach is enclosed.
	std::vector<size_t> pending;
	for (size_t x = 0; x < width; ++x) {
		pending.push_back(x);
		pending.push_back((height - 1) * width + x);
	}
	for (size_t y = 0; y < height; ++y) {
		pending.push_back(y * width);
		pending.push_back(y * width + width - 1);
	}
	for (size_t index : pending) {
		cells[index] = CELL_OUTSIDE;
	}

	while (!pending.empty()) {
		const size_t index = pending.back();
		pending.pop_back();

		const size_t cell_x = index % width;
		const size_t cell_y = index / width;

		if (cell_x > 0 && cells[index - 1] == CELL_ENCLOSED) {
			cells[index - 1] = CELL_OUTSIDE;
			pending.push_back(index - 1);
		}
		if (cell_x + 1 < width && cells[index + 1] == CELL_ENCLOSED) {
			cells[index + 1] = CELL_OUTSIDE;
			pending.push_back(index + 1);
		}
		if (cell_y > 0 && cells[index - width] == CELL_ENCLOSED) {
			cells[index - width] = CELL_OUTSIDE;
			pending.push_back(index - width);
		}
		if (cell_y + 1 < height && cells[index + width] == CELL_ENCLOSED) {
			cells[index + width] = CELL_OUTSIDE;
			pending.push_back(index + width);
		}
	}

	for (int y = min_y; y <= max_y; ++y) {
		for (int x = min_x; x <= max_x; ++x) {
			if (cells[cellIndex(x, y)] != CELL_OUTSIDE) {
				tiles.push_back(Position(x, y, z));
			}
		}
	}

	return true;
}

BEGIN_EVENT_TABLE(MapCanvas, wxGLCanvas)
EVT_KEY_DOWN(MapCanvas::OnKeyDown)
EVT_KEY_DOWN(MapCanvas::OnKeyUp)

// Mouse events
EVT_MOTION(MapCanvas::OnMouseMove)
EVT_LEFT_UP(MapCanvas::OnMouseLeftRelease)
EVT_LEFT_DOWN(MapCanvas::OnMouseLeftClick)
EVT_LEFT_DCLICK(MapCanvas::OnMouseLeftDoubleClick)
EVT_MIDDLE_DOWN(MapCanvas::OnMouseCenterClick)
EVT_MIDDLE_UP(MapCanvas::OnMouseCenterRelease)
EVT_RIGHT_DOWN(MapCanvas::OnMouseRightClick)
EVT_RIGHT_UP(MapCanvas::OnMouseRightRelease)
EVT_MOUSEWHEEL(MapCanvas::OnWheel)
EVT_ENTER_WINDOW(MapCanvas::OnGainMouse)
EVT_LEAVE_WINDOW(MapCanvas::OnLoseMouse)

// Drawing events
EVT_PAINT(MapCanvas::OnPaint)
EVT_ERASE_BACKGROUND(MapCanvas::OnEraseBackground)

// Menu events
EVT_MENU(MAP_POPUP_MENU_CUT, MapCanvas::OnCut)
EVT_MENU(MAP_POPUP_MENU_COPY, MapCanvas::OnCopy)
EVT_MENU(MAP_POPUP_MENU_COPY_POSITION, MapCanvas::OnCopyPosition)
EVT_MENU(MAP_POPUP_MENU_COPY_RAID_AREA, MapCanvas::OnCopyRaidArea)
EVT_MENU(MAP_POPUP_MENU_EXPORT_SPRITESHEET, MapCanvas::OnExportSpritesheet)
EVT_MENU(MAP_POPUP_MENU_PASTE, MapCanvas::OnPaste)
EVT_MENU(MAP_POPUP_MENU_DELETE, MapCanvas::OnDelete)
EVT_MENU(MAP_POPUP_MENU_ROTATE_SELECTION_CW, MapCanvas::OnRotateSelectionClockwise)
EVT_MENU(MAP_POPUP_MENU_ROTATE_SELECTION_CCW, MapCanvas::OnRotateSelectionCounterClockwise)
EVT_MENU(MAP_POPUP_MENU_FLIP_SELECTION_HORIZONTAL, MapCanvas::OnFlipSelectionHorizontal)
EVT_MENU(MAP_POPUP_MENU_FLIP_SELECTION_VERTICAL, MapCanvas::OnFlipSelectionVertical)
EVT_MENU(MAP_POPUP_MENU_CONTENT_AWARE_FILL, MapCanvas::OnContentAwareFill)
EVT_MENU(MAP_POPUP_MENU_ADD_COMMENT, MapCanvas::OnAddComment)
EVT_MENU(MAP_POPUP_MENU_REMOVE_COMMENT, MapCanvas::OnRemoveComment)
EVT_MENU(MAP_POPUP_MENU_PING_HERE, MapCanvas::OnPingHere)
//----
EVT_MENU(MAP_POPUP_MENU_COPY_SERVER_ID, MapCanvas::OnCopyServerId)
EVT_MENU(MAP_POPUP_MENU_COPY_CLIENT_ID, MapCanvas::OnCopyClientId)
EVT_MENU(MAP_POPUP_MENU_COPY_NAME, MapCanvas::OnCopyName)
EVT_MENU(MAP_POPUP_MENU_CREATE_GENERATE_SCRIPT, MapCanvas::OnCreateGenerateScript)
EVT_MENU(MAP_POPUP_MENU_CREATE_REMOVE_SCRIPT, MapCanvas::OnCreateRemoveScript)
EVT_MENU(MAP_POPUP_MENU_CREATE_CREATE_SCRIPT, MapCanvas::OnCreateCreateScript)
EVT_MENU(MAP_POPUP_MENU_CREATE_CHECK_SCRIPT, MapCanvas::OnCreateCheckScript)

// ----
EVT_MENU(MAP_POPUP_MENU_ROTATE, MapCanvas::OnRotateItem)
EVT_MENU(MAP_POPUP_MENU_GOTO, MapCanvas::OnGotoDestination)
EVT_MENU(MAP_POPUP_MENU_SWITCH_DOOR, MapCanvas::OnSwitchDoor)
// ----
EVT_MENU(MAP_POPUP_MENU_SELECT_RAW_BRUSH, MapCanvas::OnSelectRAWBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_GROUND_BRUSH, MapCanvas::OnSelectGroundBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_DOODAD_BRUSH, MapCanvas::OnSelectDoodadBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_DOOR_BRUSH, MapCanvas::OnSelectDoorBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_WALL_BRUSH, MapCanvas::OnSelectWallBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_CARPET_BRUSH, MapCanvas::OnSelectCarpetBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_TABLE_BRUSH, MapCanvas::OnSelectTableBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_CREATURE_BRUSH, MapCanvas::OnSelectCreatureBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, MapCanvas::OnSelectSpawnBrush)
EVT_MENU(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, MapCanvas::OnSelectHouseBrush)
EVT_MENU(MAP_POPUP_MENU_MOVE_TO_TILESET, MapCanvas::OnSelectMoveTo)
EVT_MENU(	MAP_POPUP_MENU_CREATE_WALL, MapCanvas::OnCreateWall)
EVT_MENU(MAP_POPUP_MENU_REPLACE_WALL, MapCanvas::OnReplaceWall)
EVT_MENU(MAP_POPUP_MENU_REPLACE_GROUND, MapCanvas::OnReplaceGround)
EVT_MENU(MAP_POPUP_MENU_REPLACE_WITH_SEARCH, MapCanvas::OnReplaceWithSearchItem)
// ----
EVT_MENU(MAP_POPUP_MENU_PROPERTIES, MapCanvas::OnProperties)
// ----
EVT_MENU(MAP_POPUP_MENU_BROWSE_TILE, MapCanvas::OnBrowseTile)
END_EVENT_TABLE()

bool MapCanvas::processed[] = { 0 };

MapCanvas::MapCanvas(MapWindow* parent, Editor& editor, int* attriblist) :
	wxGLCanvas(parent, wxID_ANY, nullptr, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
	editor(editor),
	floor(GROUND_LAYER),
	zoom(1.0),
	cursor_x(-1),
	cursor_y(-1),
	dragging(false),
	boundbox_selection(false),
	boundbox_deselection(false),
	lasso_selection(false),
	screendragging(false),
	drawing(false),
	dragging_draw(false),
	dragging_draw_line(false),
	replace_dragging(false),

	screenshot_buffer(nullptr),

	drag_start_x(-1),
	drag_start_y(-1),
	drag_start_z(-1),

	last_cursor_map_x(-1),
	last_cursor_map_y(-1),
	last_cursor_map_z(-1),

	last_click_map_x(-1),
	last_click_map_y(-1),
	last_click_map_z(-1),
	last_click_abs_x(-1),
	last_click_abs_y(-1),
	last_click_x(-1),
	last_click_y(-1),

	last_mmb_click_x(-1),
	last_mmb_click_y(-1),
	options_revision(std::numeric_limits<uint32_t>::max()) {
	popup_menu = newd MapPopupMenu(editor);
	animation_timer = newd AnimationTimer(this);
	drawer = new MapDrawer(this);
	keyCode = WXK_NONE;
}

MapCanvas::~MapCanvas() {
	CloseModelessProperties();
	delete popup_menu;
	delete animation_timer;
	delete drawer;
	free(screenshot_buffer);
}

void MapCanvas::CloseModelessProperties() {
	std::vector<wxWeakRef<ObjectPropertiesWindowBase>> windows = modeless_property_windows;
	modeless_property_windows.clear();

	for (wxWeakRef<ObjectPropertiesWindowBase>& window_ref : windows) {
		if (ObjectPropertiesWindowBase* window = window_ref) {
			window->Finish(0);
		}
	}
}

void MapCanvas::ShowObjectProperties(ObjectPropertiesWindowBase* window, Tile* edited_tile, Item* protected_item) {
	if (!window) {
		delete edited_tile;
		return;
	}

	wxWeakRef<ObjectPropertiesWindowBase> window_ref(window);
	modeless_property_windows.push_back(window_ref);
	if (protected_item) {
		editor.protectItemProperties(protected_item);
	}

	window->ShowModeless([this, window_ref, edited_tile, protected_item](int ret) mutable {
		ObjectPropertiesWindowBase* completed_window = window_ref;
		modeless_property_windows.erase(
			std::remove_if(
				modeless_property_windows.begin(),
				modeless_property_windows.end(),
				[completed_window](const wxWeakRef<ObjectPropertiesWindowBase>& candidate_ref) {
					ObjectPropertiesWindowBase* candidate = candidate_ref;
					return !candidate || candidate == completed_window;
				}
			),
			modeless_property_windows.end()
		);

		if (protected_item) {
			editor.releaseItemProperties(protected_item);
		}

		if (ret != 0) {
			Action* action = editor.actionQueue->createAction(ACTION_CHANGE_PROPERTIES);
			action->addChange(newd Change(edited_tile));
			editor.addAction(action);
		} else {
			delete edited_tile;
		}
		g_gui.RefreshView();
	});
}

void MapCanvas::Refresh() {
	if (refresh_watch.Time() > g_settings.getInteger(Config::HARD_REFRESH_RATE)) {
		refresh_watch.Start();
		wxGLCanvas::Update();
	}
	wxGLCanvas::Refresh();
}

void MapCanvas::SetZoom(double value) {
	if (value < 0.125) {
		value = 0.125;
	}

	if (value > 25.00) {
		value = 25.0;
	}

	if (zoom != value) {
		int center_x, center_y;
		GetScreenCenter(&center_x, &center_y);

		zoom = value;
		static_cast<MapWindow*>(GetParent())->SetScreenCenterPosition(Position(center_x, center_y, floor));

		UpdatePositionStatus();
		UpdateZoomStatus();
		Refresh();
	}
}

void MapCanvas::GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const {
	static_cast<MapWindow*>(GetParent())->GetViewSize(screensize_x, screensize_y);
	static_cast<MapWindow*>(GetParent())->GetViewStart(view_scroll_x, view_scroll_y);
}

void MapCanvas::OnPaint(wxPaintEvent& event) {
	SetCurrent(*g_gui.GetGLContext(this));

	if (g_gui.IsRenderingEnabled()) {
		DrawingOptions& options = drawer->getOptions();
		if (screenshot_buffer) {
			options.SetIngame();
		} else {
			const uint32_t settingsRevision = g_settings.getRevision();
			if (settingsRevision != options_revision) {
				options_revision = settingsRevision;
				options.transparent_floors = g_settings.getBoolean(Config::TRANSPARENT_FLOORS);
				options.transparent_items = g_settings.getBoolean(Config::TRANSPARENT_ITEMS);
				options.transparent_void_items = g_settings.getBoolean(Config::TRANSPARENT_ITEMS_ON_VOID);
				options.show_ingame_box = g_settings.getBoolean(Config::SHOW_INGAME_BOX);
				options.show_lights = g_settings.getBoolean(Config::SHOW_LIGHTS);
				options.show_light_str = g_settings.getBoolean(Config::SHOW_LIGHT_STR);
				options.show_tech_items = g_settings.getBoolean(Config::SHOW_TECHNICAL_ITEMS);
				options.show_grid = g_settings.getInteger(Config::SHOW_GRID);
				options.show_chunk_boundaries = g_settings.getBoolean(Config::SHOW_CHUNK_BOUNDARIES);
				options.show_tile_coordinates = g_settings.getBoolean(Config::SHOW_TILE_COORDINATES);
				options.show_hud = g_settings.getBoolean(Config::SHOW_HUD);
				options.show_autoborder_indicator = g_settings.getBoolean(Config::SHOW_AUTOBORDER_INDICATOR);
				options.show_mouse_crosshair = g_settings.getBoolean(Config::SHOW_MOUSE_CROSSHAIR);
				options.ingame = !g_settings.getBoolean(Config::SHOW_EXTRA);
				options.show_all_floors = g_settings.getBoolean(Config::SHOW_ALL_FLOORS);
				options.show_all_floors_underground = g_settings.getBoolean(Config::SHOW_ALL_FLOORS_UNDERGROUND);
				options.show_creatures = g_settings.getBoolean(Config::SHOW_CREATURES);
				options.show_spawns = g_settings.getBoolean(Config::SHOW_SPAWNS);
				options.show_houses = g_settings.getBoolean(Config::SHOW_HOUSES);
				options.show_shade = g_settings.getBoolean(Config::SHOW_SHADE);
				options.show_special_tiles = g_settings.getBoolean(Config::SHOW_SPECIAL_TILES);
				options.show_items = g_settings.getBoolean(Config::SHOW_ITEMS);
				options.highlight_items = g_settings.getBoolean(Config::HIGHLIGHT_ITEMS);
				options.highlight_locked_doors = g_settings.getBoolean(Config::HIGHLIGHT_LOCKED_DOORS);
				options.show_blocking = g_settings.getBoolean(Config::SHOW_BLOCKING);
				options.show_tooltips = g_settings.getBoolean(Config::SHOW_TOOLTIPS);
				options.show_as_minimap = g_settings.getBoolean(Config::SHOW_AS_MINIMAP);
				options.show_only_colors = g_settings.getBoolean(Config::SHOW_ONLY_TILEFLAGS);
				options.show_only_modified = g_settings.getBoolean(Config::SHOW_ONLY_MODIFIED_TILES);
				options.show_preview = g_settings.getBoolean(Config::SHOW_PREVIEW);
				options.show_hooks = g_settings.getBoolean(Config::SHOW_WALL_HOOKS);
				options.show_fishable_water = g_settings.getBoolean(Config::SHOW_FISHABLE_WATER);
				options.show_borders = g_settings.getBoolean(Config::SHOW_BORDERS);
				options.show_walls = g_settings.getBoolean(Config::SHOW_WALLS);
				options.show_ground = g_settings.getBoolean(Config::SHOW_GROUND);
				options.hide_items_when_zoomed = g_settings.getBoolean(Config::HIDE_ITEMS_WHEN_ZOOMED);
				options.show_towns = g_settings.getBoolean(Config::SHOW_TOWNS);
				options.always_show_zones = g_settings.getBoolean(Config::ALWAYS_SHOW_ZONES);
				options.extended_house_shader = g_settings.getBoolean(Config::EXT_HOUSE_SHADER);
				options.show_invisible_items = g_settings.getBoolean(Config::SHOW_INVISIBLE_ITEMS);
				options.show_creature_spawn_time = g_settings.getBoolean(Config::SHOW_CREATURE_SPAWN_TIME);
			options.mouse_crosshair_color = wxColor(
				g_settings.getInteger(Config::CURSOR_CROSSHAIR_RED),
				g_settings.getInteger(Config::CURSOR_CROSSHAIR_GREEN),
				g_settings.getInteger(Config::CURSOR_CROSSHAIR_BLUE)
			);
			const auto clamp_component = [](int value) {
				return std::clamp(value, 0, 255);
			};
			options.viewport_background_color = wxColor(
				clamp_component(g_settings.getInteger(Config::VIEWPORT_BACKGROUND_RED)),
				clamp_component(g_settings.getInteger(Config::VIEWPORT_BACKGROUND_GREEN)),
				clamp_component(g_settings.getInteger(Config::VIEWPORT_BACKGROUND_BLUE))
			);
			}
		}

		options.dragging = boundbox_selection;

		if (options.show_preview || (editor.IsLive() && editor.GetLive().hasActivePings())) {
			animation_timer->Start();
		} else {
			animation_timer->Stop();
		}

		drawer->SetupVars();
		drawer->SetupGL();
		drawer->Draw();

		if (screenshot_buffer) {
			drawer->TakeScreenshot(screenshot_buffer);
		}

		drawer->Release();
	}

	// Clean unused textures
	g_gui.gfx.garbageCollection();

	// Swap buffer
	SwapBuffers();
}

void MapCanvas::TakeScreenshot(wxFileName path, wxString format) {
	int screensize_x, screensize_y;
	GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	delete[] screenshot_buffer;
	screenshot_buffer = newd uint8_t[3 * screensize_x * screensize_y];

	// Draw the window
	Refresh();
	wxGLCanvas::Update(); // Forces immediate redraws the window.

	// screenshot_buffer should now contain the screenbuffer
	if (screenshot_buffer == nullptr) {
		g_gui.PopupDialog("Capture failed", "Image capture failed. Old Video Driver?", wxOK);
	} else {
		// We got the shit
		int screensize_x, screensize_y;
		static_cast<MapWindow*>(GetParent())->GetViewSize(&screensize_x, &screensize_y);
		wxImage screenshot(screensize_x, screensize_y, screenshot_buffer);

		time_t t = time(nullptr);
		struct tm* current_time = localtime(&t);
		ASSERT(current_time);

		wxString date;
		date << "screenshot_" << (1900 + current_time->tm_year);
		if (current_time->tm_mon < 9) {
			date << "-" << "0" << current_time->tm_mon + 1;
		} else {
			date << "-" << current_time->tm_mon + 1;
		}
		date << "-" << current_time->tm_mday;
		date << "-" << current_time->tm_hour;
		date << "-" << current_time->tm_min;
		date << "-" << current_time->tm_sec;

		int type = 0;
		path.SetName(date);
		if (format == "bmp") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_BMP;
		} else if (format == "png") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_PNG;
		} else if (format == "jpg" || format == "jpeg") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_JPEG;
		} else if (format == "tga") {
			path.SetExt(format);
			type = wxBITMAP_TYPE_TGA;
		} else {
			g_gui.SetStatusText("Unknown screenshot format \'" + format + "\", switching to default (png)");
			path.SetExt("png");
			;
			type = wxBITMAP_TYPE_PNG;
		}

		path.Mkdir(0755, wxPATH_MKDIR_FULL);
		wxFileOutputStream of(path.GetFullPath());
		if (of.IsOk()) {
			if (screenshot.SaveFile(of, static_cast<wxBitmapType>(type))) {
				g_gui.SetStatusText("Took screenshot and saved as " + path.GetFullName());
			} else {
				g_gui.PopupDialog("File error", "Couldn't save image file correctly.", wxOK);
			}
		} else {
			g_gui.PopupDialog("File error", "Couldn't open file " + path.GetFullPath() + " for writing.", wxOK);
		}
	}

	Refresh();

	screenshot_buffer = nullptr;
	options_revision = std::numeric_limits<uint32_t>::max();
}

void MapCanvas::ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y) {
	int start_x, start_y;
	static_cast<MapWindow*>(GetParent())->GetViewStart(&start_x, &start_y);

	screen_x *= GetContentScaleFactor();
	screen_y *= GetContentScaleFactor();

	if (screen_x < 0) {
		*map_x = (start_x + screen_x) / TileSize;
	} else {
		*map_x = int(start_x + (screen_x * zoom)) / TileSize;
	}

	if (screen_y < 0) {
		*map_y = (start_y + screen_y) / TileSize;
	} else {
		*map_y = int(start_y + (screen_y * zoom)) / TileSize;
	}

	if (floor <= GROUND_LAYER) {
		*map_x += GROUND_LAYER - floor;
		*map_y += GROUND_LAYER - floor;
	} /* else {
		 *map_x += MAP_MAX_LAYER - floor;
		 *map_y += MAP_MAX_LAYER - floor;
	 }*/
}

void MapCanvas::GetScreenCenter(int* map_x, int* map_y) {
	int width, height;
	static_cast<MapWindow*>(GetParent())->GetViewSize(&width, &height);
	return ScreenToMap(width / 2, height / 2, map_x, map_y);
}

Position MapCanvas::GetCursorPosition() const {
	return Position(last_cursor_map_x, last_cursor_map_y, floor);
}

void MapCanvas::UpdatePositionStatus(int x, int y) {
	if (x == -1) {
		x = cursor_x;
	}
	if (y == -1) {
		y = cursor_y;
	}

	int map_x, map_y;
	ScreenToMap(x, y, &map_x, &map_y);

	wxString ss;
	ss << "x: " << map_x << " y:" << map_y << " z:" << floor;
	g_gui.root->SetStatusText(ss, 2);

	ss = "";
	Tile* tile = editor.map.getTile(map_x, map_y, floor);
	if (tile) {
		if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
			ss << "Spawn radius: " << tile->spawn->getSize();
		} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			ss << (tile->creature->isNpc() ? "NPC" : "Monster");
			ss << " \"" << wxstr(tile->creature->getName()) << "\" spawntime: " << tile->creature->getSpawnTime();
		} else if (Item* item = tile->getTopVisibleItem()) {
			ss << "Item \"" << wxstr(item->getName()) << "\"";
			ss << " id:" << item->getID();
			ss << " cid:" << item->getClientID();
			if (item->getUniqueID()) {
				ss << " uid:" << item->getUniqueID();
			}
			if (item->getActionID()) {
				ss << " aid:" << item->getActionID();
			}
			if (item->hasWeight()) {
				wxString s;
				s.Printf("%.2f", item->getWeight());
				ss << " weight: " << s;
			}
		} else {
			ss << "Nothing";
		}
	} else {
		ss << "Nothing";
	}


	g_gui.root->SetStatusText(ss, 1);
}

void MapCanvas::UpdateZoomStatus() {
	int percentage = (int)((1.0 / zoom) * 100);
	wxString ss;
	ss << "zoom: " << percentage << "%";
	g_gui.root->SetStatusText(ss, 3);
}

void MapCanvas::OnMouseMove(wxMouseEvent& event) {
	if (screendragging) {
		static_cast<MapWindow*>(GetParent())->ScrollRelative(int(g_settings.getFloat(Config::SCROLL_SPEED) * zoom * (event.GetX() - cursor_x)), int(g_settings.getFloat(Config::SCROLL_SPEED) * zoom * (event.GetY() - cursor_y)));
		Refresh();
	}

	cursor_x = event.GetX();
	cursor_y = event.GetY();

	if (editor.IsLive() && drawer->UpdateLiveParticipantHover(cursor_x, cursor_y)) {
		Refresh();
	}

	int mouse_map_x, mouse_map_y;
	MouseToMap(&mouse_map_x, &mouse_map_y);

	bool map_update = false;
	if (last_cursor_map_x != mouse_map_x || last_cursor_map_y != mouse_map_y || last_cursor_map_z != floor) {
		map_update = true;
	}
	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	if (map_update) {
		UpdatePositionStatus(cursor_x, cursor_y);
		UpdateZoomStatus();
	}

	if (editor.IsLive() && map_update) {
		editor.GetLive().updateCursor(Position(mouse_map_x, mouse_map_y, floor));
	}

	if (g_gui.IsSelectionMode()) {
		if (map_update && isPasting()) {
			Refresh();
		} else if (map_update && dragging) {
			wxString ss;

			int move_x = drag_start_x - mouse_map_x;
			int move_y = drag_start_y - mouse_map_y;
			int move_z = drag_start_z - floor;
			ss << "Dragging " << -move_x << "," << -move_y << "," << -move_z;
			g_gui.SetStatusText(ss);

			Refresh();
		} else if (boundbox_selection) {
			if (map_update) {
				wxString ss;

				if (lasso_selection) {
					AppendLassoPoint(mouse_map_x, mouse_map_y);
					ss << (boundbox_deselection ? "Lasso deselection " : "Lasso selection ") << lasso_trace.size() << " points";
				} else {
					int move_x = std::abs(last_click_map_x - mouse_map_x);
					int move_y = std::abs(last_click_map_y - mouse_map_y);
					ss << (boundbox_deselection ? "Deselection " : "Selection ") << move_x + 1 << ":" << move_y + 1;
				}
				g_gui.SetStatusText(ss);
			}

			Refresh();
		}
	} else { // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		if (map_update && drawing && brush && liveEditAllowed(editor, mouse_map_x, mouse_map_y)) {
			if (brush->isDoodad()) {
				if (event.ControlDown()) {
					PositionVector tilestodraw;
					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);
					editor.undraw(tilestodraw, event.ShiftDown() || event.AltDown());
				} else {
					editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());
				}
			} else if (brush->isDoor()) {
				if (!brush->canDraw(&editor.map, Position(mouse_map_x, mouse_map_y, floor))) {
					// We don't have to waste an action in this case...
				} else {
					PositionVector tilestodraw;
					PositionVector tilestoborder;
					tilestodraw.reserve(1);
					tilestoborder.reserve(4);

					tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

					tilestoborder.push_back(Position(mouse_map_x, mouse_map_y - 1, floor));
					tilestoborder.push_back(Position(mouse_map_x - 1, mouse_map_y, floor));
					tilestoborder.push_back(Position(mouse_map_x, mouse_map_y + 1, floor));
					tilestoborder.push_back(Position(mouse_map_x + 1, mouse_map_y, floor));

					if (event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				}
			} else if (brush->needBorders()) {
				PositionVector tilestodraw, tilestoborder;

				getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, &tilestoborder);

				if (event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, event.AltDown());
				} else {
					editor.draw(tilestodraw, tilestoborder, event.AltDown());
				}
			} else if (brush->oneSizeFitsAll()) {
				drawing = true;
				PositionVector tilestodraw;
				tilestodraw.reserve(1);
				tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

				if (event.ControlDown()) {
					editor.undraw(tilestodraw, event.AltDown());
				} else {
					editor.draw(tilestodraw, event.AltDown());
				}
			} else { // No borders
				PositionVector tilestodraw;
				const int brushSize = g_gui.GetBrushSize();
				const BrushShape brushShape = g_gui.GetBrushShape();
				const int minOffset = -brushSize;
				const int maxOffset = brushSize;
				const double radius = static_cast<double>(brushSize);
				const double fillRadiusSquared = (radius + 0.005) * (radius + 0.005);
				const int range = (brushSize * 2) + 1;
				tilestodraw.reserve(static_cast<size_t>(range) * static_cast<size_t>(range));

				for (int y = minOffset; y <= maxOffset; y++) {
					for (int x = minOffset; x <= maxOffset; x++) {
						if (brushShape == BRUSHSHAPE_SQUARE) {
							tilestodraw.push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						} else if (brushShape == BRUSHSHAPE_CIRCLE) {
							double distanceSquared = double(x * x + y * y);
							if (distanceSquared < fillRadiusSquared) {
								tilestodraw.push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
							}
						}
					}
				}
				if (event.ControlDown()) {
					editor.undraw(tilestodraw, event.AltDown());
				} else {
					editor.draw(tilestodraw, event.AltDown());
				}
			}

			// Create newd doodad layout (does nothing if a non-doodad brush is selected)
			g_gui.FillDoodadPreviewBuffer();

			g_gui.RefreshView();
		} else if (dragging_draw) {
			// Alt may be pressed or released halfway through the drag.
			dragging_draw_line = event.AltDown();
			if (dragging_draw_line && map_update) {
				wxString ss;
				ss << "Line " << std::max(std::abs(last_click_map_x - mouse_map_x), std::abs(last_click_map_y - mouse_map_y)) + 1 << " tiles";
				g_gui.SetStatusText(ss);
			}
			g_gui.RefreshView();
		} else if (map_update && brush) {
			Refresh();
		}
	}
}

void MapCanvas::OnMouseLeftRelease(wxMouseEvent& event) {
	OnMouseActionRelease(event);
}

void MapCanvas::OnMouseLeftClick(wxMouseEvent& event) {
	OnMouseActionClick(event);
}

void MapCanvas::OnMouseLeftDoubleClick(wxMouseEvent& event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);

	if (event.AltDown() && tile) {
		if (Item* item = tile->getTopVisibleItem()) {
			if (g_gui.IsDrawingMode()) {
				g_gui.SetSelectionMode();
			}
			selectVisibleItemsById(*this, editor, item->getID());
			g_gui.RefreshView();
		}
		return;
	}

	if (g_settings.getInteger(Config::DOUBLECLICK_PROPERTIES)) {
		if (tile && tile->size() > 0) {
			Tile* new_tile = tile->deepCopy(editor.map);
			ObjectPropertiesWindowBase* w = nullptr;
			Item* protected_item = nullptr;
			if (new_tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
				w = newd OldPropertiesWindow(this, &editor.map, new_tile, new_tile->creature);
			} else if (Item* item = new_tile->getTopVisibleItem()) {
				protected_item = tile->getTopVisibleItem();
				if (editor.map.getVersion().otbm >= MAP_OTBM_4) {
					w = newd PropertiesWindow(this, &editor.map, new_tile, item);
				} else {
					w = newd OldPropertiesWindow(this, &editor.map, new_tile, item);
				}
			} else {
				delete new_tile;
				return;
			}

			ShowObjectProperties(w, new_tile, protected_item);
		}
	}
}

void MapCanvas::OnMouseCenterClick(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMousePropertiesClick(event);
	} else {
		OnMouseCameraClick(event);
	}
}

void MapCanvas::OnMouseCenterRelease(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMousePropertiesRelease(event);
	} else {
		OnMouseCameraRelease(event);
	}
}

void MapCanvas::OnMouseRightClick(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMouseCameraClick(event);
	} else {
		OnMousePropertiesClick(event);
	}
}

void MapCanvas::OnMouseRightRelease(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMouseCameraRelease(event);
	} else {
		OnMousePropertiesRelease(event);
	}
}

void MapCanvas::OnMouseActionClick(wxMouseEvent& event) {
	SetFocus();

	if (editor.IsLive()) {
		uint32_t participantId = 0;
		if (drawer->HitTestLiveParticipant(event.GetX(), event.GetY(), participantId)) {
			const LiveSocket& live = editor.GetLive();
			if (participantId == live.getOwnClientId()) {
				wxColourData colourData;
				colourData.SetColour(editor.getLiveCursorColor());
				wxColourDialog dialog(this, &colourData);
				if (dialog.ShowModal() == wxID_OK) {
					editor.setLiveCursorColor(dialog.GetColourData().GetColour());
				}
				return;
			}

			Position position;
			if (live.getCursorPosition(participantId, position) && position.isValid()) {
				static_cast<MapWindow*>(GetParent())->SetScreenCenterPosition(position);
			}
			return;
		}
	}

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	if (editor.IsLive() && event.AltDown() && event.ShiftDown()) {
		if (liveEditAllowed(editor, mouse_map_x, mouse_map_y)) {
			editor.sendLivePing(Position(mouse_map_x, mouse_map_y, floor));
			g_gui.SetStatusText(wxString::Format("Ping sent at %d, %d, %d", mouse_map_x, mouse_map_y, floor));
			g_gui.RefreshView();
		}
		return;
	}

	if (!g_gui.GetCurrentBrush() && !g_gui.IsSelectionMode()) {
		if (editor.showMapCommentsAt(Position(mouse_map_x, mouse_map_y, floor))) {
			return;
		}
	}

	if (event.ControlDown() && event.AltDown()) {
		Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
		if (tile && tile->size() > 0) {
			// Check for creature first (if visible)
			if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
				CreatureBrush* brush = tile->creature->getBrush();
				if (brush) {
					g_gui.SelectBrush(brush, TILESET_CREATURE);
					return;
				}
			}
			// Fall back to item selection
			Item* item = tile->getTopVisibleItem();
			if (item && item->getRAWBrush()) {
				g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
			}
		}
	} else if (g_gui.IsSelectionMode()) {
		if (isPasting() && editor.IsClipboardAllowed()) {
			// Set paste to false (no rendering etc.)
			EndPasting();

			// Paste to the map
			editor.copybuffer.paste(editor, Position(mouse_map_x, mouse_map_y, floor));

			// Start dragging
			dragging = true;
			drag_start_x = mouse_map_x;
			drag_start_y = mouse_map_y;
			drag_start_z = floor;
		} else {
			do {
				boundbox_selection = false;
				boundbox_deselection = false;
				lasso_selection = false;
				lasso_trace.clear();
				if (event.ShiftDown()) {
					boundbox_selection = true;
					lasso_selection = g_settings.getBoolean(Config::LASSO_SELECT);
					if (lasso_selection) {
						AppendLassoPoint(mouse_map_x, mouse_map_y);
					}

					if (!event.ControlDown()) {
						editor.selection.start(); // Start selection session
						editor.selection.clear(); // Clear out selection
						editor.selection.finish(); // End selection session
						editor.selection.updateSelectionCount();
					}
				} else if (event.ControlDown()) {
					Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
					if (tile) {
						if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
							editor.selection.start(); // Start selection session
							if (tile->spawn->isSelected()) {
								editor.selection.remove(tile, tile->spawn);
							} else {
								editor.selection.add(tile, tile->spawn);
							}
							editor.selection.finish(); // Finish selection session
							editor.selection.updateSelectionCount();
						} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
							editor.selection.start(); // Start selection session
							if (tile->creature->isSelected()) {
								editor.selection.remove(tile, tile->creature);
							} else {
								editor.selection.add(tile, tile->creature);
							}
							editor.selection.finish(); // Finish selection session
							editor.selection.updateSelectionCount();
						} else {
							Item* item = tile->getTopVisibleItem();
							if (item) {
								editor.selection.start(); // Start selection session
								if (item->isSelected()) {
									editor.selection.remove(tile, item);
								} else {
									editor.selection.add(tile, item);
								}
								editor.selection.finish(); // Finish selection session
								editor.selection.updateSelectionCount();
							}
						}
					}
				} else {
					Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
					if (!tile) {
						editor.selection.start(); // Start selection session
						editor.selection.clear(); // Clear out selection
						editor.selection.finish(); // End selection session
						editor.selection.updateSelectionCount();
					} else if (tile->isSelected()) {
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
					} else {
						editor.selection.start(); // Start a selection session
						editor.selection.clear();
						editor.selection.commit();
						if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
							editor.selection.add(tile, tile->spawn);
							dragging = true;
							drag_start_x = mouse_map_x;
							drag_start_y = mouse_map_y;
							drag_start_z = floor;
						} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
							editor.selection.add(tile, tile->creature);
							dragging = true;
							drag_start_x = mouse_map_x;
							drag_start_y = mouse_map_y;
							drag_start_z = floor;
						} else {
							Item* item = tile->getTopVisibleItem();
							if (item) {
								editor.selection.add(tile, item);
								dragging = true;
								drag_start_x = mouse_map_x;
								drag_start_y = mouse_map_y;
								drag_start_z = floor;
							}
						}
						editor.selection.finish(); // Finish the selection session
						editor.selection.updateSelectionCount();
					}
				}
			} while (false);
		}
	} else if (g_gui.GetCurrentBrush()) { // Drawing mode
		if (!liveEditAllowed(editor, mouse_map_x, mouse_map_y)) {
			return;
		}
		Brush* brush = g_gui.GetCurrentBrush();
		if (event.ShiftDown() && brush->canDrag()) {
			dragging_draw = true;
			// Alt turns the drag into a straight line instead of a square/circle.
			dragging_draw_line = event.AltDown();
		} else {
			if (g_gui.IsSingleTileBrush() && !brush->oneSizeFitsAll()) {
				drawing = true;
			} else {
				drawing = g_gui.GetCurrentBrush()->canSmear();
			}
			if (brush->isWall()) {
				if (event.AltDown() && g_gui.IsSingleTileBrush()) {
					// z0mg, just clicked a tile, shift variaton.
					if (event.ControlDown()) {
						editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					} else {
						editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					}
				} else {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					const int start_offset_x = g_gui.GetSquareBrushMinOffset();
					const int end_offset_x = g_gui.GetSquareBrushMaxOffset();
					const int start_offset_y = start_offset_x;
					const int end_offset_y = end_offset_x;
					const int brush_width = end_offset_x - start_offset_x + 1;
					const int brush_height = end_offset_y - start_offset_y + 1;
					const size_t perimeter = static_cast<size_t>((brush_width + brush_height) * 2);
					tilestodraw.reserve(tilestodraw.size() + perimeter);
					const size_t borderArea = static_cast<size_t>((brush_width + 2) * (brush_height + 2));
					tilestoborder.reserve(tilestoborder.size() + borderArea);

					int start_map_x = mouse_map_x + start_offset_x;
					int start_map_y = mouse_map_y + start_offset_y;
					int end_map_x = mouse_map_x + end_offset_x;
					int end_map_y = mouse_map_y + end_offset_y;

					for (int y = start_map_y - 1; y <= end_map_y + 1; ++y) {
						for (int x = start_map_x - 1; x <= end_map_x + 1; ++x) {
							if ((x <= start_map_x + 1 || x >= end_map_x - 1) || (y <= start_map_y + 1 || y >= end_map_y - 1)) {
								tilestoborder.push_back(Position(x, y, floor));
							}
							if (((x == start_map_x || x == end_map_x) || (y == start_map_y || y == end_map_y)) && ((x >= start_map_x && x <= end_map_x) && (y >= start_map_y && y <= end_map_y))) {
								tilestodraw.push_back(Position(x, y, floor));
							}
						}
					}
					if (event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				}
			} else if (brush->isDoor()) {
				PositionVector tilestodraw;
				PositionVector tilestoborder;
				tilestodraw.reserve(1);
				tilestoborder.reserve(4);

				tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

				tilestoborder.push_back(Position(mouse_map_x, mouse_map_y - 1, floor));
				tilestoborder.push_back(Position(mouse_map_x - 1, mouse_map_y, floor));
				tilestoborder.push_back(Position(mouse_map_x, mouse_map_y + 1, floor));
				tilestoborder.push_back(Position(mouse_map_x + 1, mouse_map_y, floor));

				if (event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, event.AltDown());
				} else {
					editor.draw(tilestodraw, tilestoborder, event.AltDown());
				}
			} else if (brush->isDoodad() || brush->isSpawn() || brush->isCreature()) {
				if (event.ControlDown()) {
					if (brush->isDoodad()) {
						PositionVector tilestodraw;
						getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);
						editor.undraw(tilestodraw, event.AltDown());
					} else {
						editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());
					}
				} else {
					bool will_show_spawn = false;
					if (brush->isSpawn() || brush->isCreature()) {
						if (!g_settings.getBoolean(Config::SHOW_SPAWNS)) {
							Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
							if (!tile || !tile->spawn) {
								will_show_spawn = true;
							}
						}
					}

					editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());

					if (will_show_spawn) {
						Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
						if (tile && tile->spawn) {
							g_settings.setInteger(Config::SHOW_SPAWNS, true);
							g_gui.UpdateMenubar();
						}
					}
				}
			} else {
				if (brush->isGround() && event.AltDown()) {
					replace_dragging = true;
					Tile* draw_tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
					if (draw_tile) {
						editor.replace_brush = draw_tile->getGroundBrush();
					} else {
						editor.replace_brush = nullptr;
					}
				}

				if (brush->needBorders()) {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					bool fill = keyCode == WXK_CONTROL_D && event.ControlDown() && brush->isGround();
					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, &tilestoborder, fill);

					if (!fill && event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				} else if (brush->oneSizeFitsAll()) {
					if (brush->isHouseExit()) {
						editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					} else {
						PositionVector tilestodraw;
						tilestodraw.reserve(1);
						tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));
						if (event.ControlDown()) {
							editor.undraw(tilestodraw, event.AltDown());
						} else {
							editor.draw(tilestodraw, event.AltDown());
						}
					}
				} else {
					PositionVector tilestodraw;

					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);

					if (event.ControlDown()) {
						editor.undraw(tilestodraw, event.AltDown());
					} else {
						editor.draw(tilestodraw, event.AltDown());
					}
				}
			}
			// Change the doodad layout brush
			g_gui.FillDoodadPreviewBuffer();
		}
	}
	last_click_x = int(event.GetX() * zoom);
	last_click_y = int(event.GetY() * zoom);

	int start_x, start_y;
	static_cast<MapWindow*>(GetParent())->GetViewStart(&start_x, &start_y);
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	last_click_map_z = floor;
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::AppendLassoPoint(int map_x, int map_y) {
	// Only the corners matter — GetLassoTiles bridges the samples — so skip a
	// point that repeats the previous one and keep the trace compact.
	if (!lasso_trace.empty()) {
		const Position& last = lasso_trace.back();
		if (last.x == map_x && last.y == map_y) {
			return;
		}
	}
	lasso_trace.push_back(Position(map_x, map_y, floor));
}

void MapCanvas::ApplyLassoSelection(bool deselect) {
	PositionVector tiles;
	if (!GetLassoTiles(lasso_trace, tiles)) {
		g_gui.SetStatusText("Lasso area is too large.");
		return;
	}

	// Mirrors the floor range and per-floor drift that SelectionThread applies
	// for the rectangular boundbox, so both selection shapes honour the
	// Selection Mode settings identically.
	int start_z = floor;
	int end_z = floor;
	switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
		case SELECT_ALL_FLOORS: {
			start_z = MAP_MAX_LAYER;
			break;
		}
		case SELECT_VISIBLE_FLOORS: {
			start_z = floor <= GROUND_LAYER ? GROUND_LAYER : std::min(MAP_MAX_LAYER, floor + 2);
			break;
		}
		case SELECT_CURRENT_FLOOR:
		default: {
			break;
		}
	}

	const bool compensate = start_z != end_z && g_settings.getBoolean(Config::COMPENSATED_SELECT);
	int offset = compensate && floor < GROUND_LAYER ? -(GROUND_LAYER - floor) : 0;

	editor.selection.start(); // Start a selection session
	for (int z = start_z; z >= end_z; --z) {
		for (const Position& pos : tiles) {
			Tile* tile = editor.map.getTile(pos.x + offset, pos.y + offset, z);
			if (!tile) {
				continue;
			}
			if (deselect) {
				if (tile->isSelected()) {
					editor.selection.remove(tile);
				}
			} else {
				editor.selection.add(tile);
			}
		}
		if (z <= GROUND_LAYER && compensate) {
			++offset;
		}
	}
	editor.selection.finish(); // Finish the selection session
	editor.selection.updateSelectionCount();
}

void MapCanvas::OnMouseActionRelease(wxMouseEvent& event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	int move_x = last_click_map_x - mouse_map_x;
	int move_y = last_click_map_y - mouse_map_y;
	int move_z = last_click_map_z - floor;

	if (g_gui.IsSelectionMode()) {
		if (dragging && (move_x != 0 || move_y != 0 || move_z != 0)) {
			editor.moveSelection(Position(move_x, move_y, move_z));
		} else {
			if (boundbox_selection) {
				if (mouse_map_x == last_click_map_x && mouse_map_y == last_click_map_y && event.ControlDown()) {
					// Mouse hasn't moved, do control+shift thingy!
					Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
					if (tile) {
						editor.selection.start(); // Start a selection session
						if (tile->isSelected()) {
							editor.selection.remove(tile);
						} else {
							editor.selection.add(tile);
						}
						editor.selection.finish(); // Finish the selection session
						editor.selection.updateSelectionCount();
					}
				} else if (lasso_selection) {
					ApplyLassoSelection(false);
				} else {
					// The cursor has moved, do some boundboxing!
					if (last_click_map_x > mouse_map_x) {
						int tmp = mouse_map_x;
						mouse_map_x = last_click_map_x;
						last_click_map_x = tmp;
					}
					if (last_click_map_y > mouse_map_y) {
						int tmp = mouse_map_y;
						mouse_map_y = last_click_map_y;
						last_click_map_y = tmp;
					}

					int numtiles = 0;
					int threadcount = std::max(g_settings.getInteger(Config::WORKER_THREADS), 1);

					int start_x = 0, start_y = 0, start_z = 0;
					int end_x = 0, end_y = 0, end_z = 0;

					switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
						case SELECT_CURRENT_FLOOR: {
							start_z = end_z = floor;
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							break;
						}
						case SELECT_ALL_FLOORS: {
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							start_z = MAP_MAX_LAYER;
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							end_z = floor;

							if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
								start_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
								start_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);

								end_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
								end_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
							}

							numtiles = (start_z - end_z) * (end_x - start_x) * (end_y - start_y);
							break;
						}
						case SELECT_VISIBLE_FLOORS: {
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							if (floor <= GROUND_LAYER) {
								start_z = GROUND_LAYER;
							} else {
								start_z = std::min(MAP_MAX_LAYER, floor + 2);
							}
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							end_z = floor;

							if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
								start_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
								start_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);

								end_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
								end_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
							}
							break;
						}
					}

					if (numtiles < 500) {
						// No point in threading for such a small set.
						threadcount = 1;
					}
					// Subdivide the selection area
					// We know it's a square, just split it into several areas
					int width = end_x - start_x;
					if (width < threadcount) {
						threadcount = min(1, width);
					}
					// Let's divide!
					int remainder = width;
					int cleared = 0;
					std::vector<SelectionThread*> threads;
					if (width == 0) {
						threads.push_back(newd SelectionThread(editor, Position(start_x, start_y, start_z), Position(start_x, end_y, end_z)));
					} else {
						for (int i = 0; i < threadcount; ++i) {
							int chunksize = width / threadcount;
							// The last threads takes all the remainder
							if (i == threadcount - 1) {
								chunksize = remainder;
							}
							threads.push_back(newd SelectionThread(editor, Position(start_x + cleared, start_y, start_z), Position(start_x + cleared + chunksize, end_y, end_z)));
							cleared += chunksize;
							remainder -= chunksize;
						}
					}
					ASSERT(cleared == width);
					ASSERT(remainder == 0);

					editor.selection.start(); // Start a selection session
					for (std::vector<SelectionThread*>::iterator iter = threads.begin(); iter != threads.end(); ++iter) {
						(*iter)->Execute();
					}
					for (std::vector<SelectionThread*>::iterator iter = threads.begin(); iter != threads.end(); ++iter) {
						editor.selection.join(*iter);
					}
					editor.selection.finish(); // Finish the selection session
					editor.selection.updateSelectionCount();
				}
			} else if (event.ControlDown()) {
				////
			} else {
				// User hasn't moved anything, meaning selection/deselection
				Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
				if (tile) {
					if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
						if (!tile->spawn->isSelected()) {
							editor.selection.start(); // Start a selection session
							editor.selection.add(tile, tile->spawn);
							editor.selection.finish(); // Finish the selection session
							editor.selection.updateSelectionCount();
						}
					} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
						if (!tile->creature->isSelected()) {
							editor.selection.start(); // Start a selection session
							editor.selection.add(tile, tile->creature);
							editor.selection.finish(); // Finish the selection session
							editor.selection.updateSelectionCount();
						}
					} else {
						Item* item = tile->getTopVisibleItem();
						if (item && !item->isSelected()) {
							editor.selection.start(); // Start a selection session
							editor.selection.add(tile, item);
							editor.selection.finish(); // Finish the selection session
							editor.selection.updateSelectionCount();
						}
					}
				}
			}
		}
		editor.actionQueue->resetTimer();
		dragging = false;
		boundbox_selection = false;
		boundbox_deselection = false;
		lasso_selection = false;
		lasso_trace.clear();
	} else if (g_gui.GetCurrentBrush()) { // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		if (dragging_draw) {
			if (!liveEditAllowed(editor, last_click_map_x, last_click_map_y) || !liveEditAllowed(editor, mouse_map_x, mouse_map_y)) {
				dragging_draw = false;
				dragging_draw_line = false;
				drawing = false;
				return;
			}
			if (brush->isSpawn()) {
				int start_map_x = std::min(last_click_map_x, mouse_map_x);
				int start_map_y = std::min(last_click_map_y, mouse_map_y);
				int end_map_x = std::max(last_click_map_x, mouse_map_x);
				int end_map_y = std::max(last_click_map_y, mouse_map_y);

				int map_x = start_map_x + (end_map_x - start_map_x) / 2;
				int map_y = start_map_y + (end_map_y - start_map_y) / 2;

				int width = min(g_settings.getInteger(Config::MAX_SPAWN_RADIUS), ((end_map_x - start_map_x) / 2 + (end_map_y - start_map_y) / 2) / 2);
				int old = g_gui.GetBrushSize();
				g_gui.SetBrushSize(width);
				editor.draw(Position(map_x, map_y, floor), event.AltDown());
				g_gui.SetBrushSize(old);
			} else {
				PositionVector tilestodraw;
				PositionVector tilestoborder;
				// Alt turns the drag into a straight line of any angle, taking
				// precedence over the wall rectangle and the square/circle brush
				// shape. Alt therefore can't double as the alternate-draw flag
				// while a line is being drawn.
				const bool draw_line = event.AltDown();
				const bool alt_draw = draw_line ? false : event.AltDown();
				if (draw_line) {
					getLineTilesToDraw(last_click_map_x, last_click_map_y, mouse_map_x, mouse_map_y, floor, tilestodraw, &tilestoborder);
				} else if (brush->isWall()) {
					int start_map_x = std::min(last_click_map_x, mouse_map_x);
					int start_map_y = std::min(last_click_map_y, mouse_map_y);
					int end_map_x = std::max(last_click_map_x, mouse_map_x);
					int end_map_y = std::max(last_click_map_y, mouse_map_y);

					for (int y = start_map_y - 1; y <= end_map_y + 1; y++) {
						for (int x = start_map_x - 1; x <= end_map_x + 1; x++) {
							if ((x <= start_map_x + 1 || x >= end_map_x - 1) || (y <= start_map_y + 1 || y >= end_map_y - 1)) {
								tilestoborder.push_back(Position(x, y, floor));
							}
							if (((x == start_map_x || x == end_map_x) || (y == start_map_y || y == end_map_y)) && ((x >= start_map_x && x <= end_map_x) && (y >= start_map_y && y <= end_map_y))) {
								tilestodraw.push_back(Position(x, y, floor));
							}
						}
					}
				} else {
					if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
						if (last_click_map_x > mouse_map_x) {
							int tmp = mouse_map_x;
							mouse_map_x = last_click_map_x;
							last_click_map_x = tmp;
						}
						if (last_click_map_y > mouse_map_y) {
							int tmp = mouse_map_y;
							mouse_map_y = last_click_map_y;
							last_click_map_y = tmp;
						}

						for (int x = last_click_map_x - 1; x <= mouse_map_x + 1; x++) {
							for (int y = last_click_map_y - 1; y <= mouse_map_y + 1; y++) {
								if ((x <= last_click_map_x || x >= mouse_map_x) || (y <= last_click_map_y || y >= mouse_map_y)) {
									tilestoborder.push_back(Position(x, y, floor));
								}
								if ((x >= last_click_map_x && x <= mouse_map_x) && (y >= last_click_map_y && y <= mouse_map_y)) {
									tilestodraw.push_back(Position(x, y, floor));
								}
							}
						}
					} else {
						int start_x, end_x;
						int start_y, end_y;
						int width = std::max(
							std::abs(
								std::max(mouse_map_y, last_click_map_y) - std::min(mouse_map_y, last_click_map_y)
							),
							std::abs(
								std::max(mouse_map_x, last_click_map_x) - std::min(mouse_map_x, last_click_map_x)
							)
						);
						if (mouse_map_x < last_click_map_x) {
							start_x = last_click_map_x - width;
							end_x = last_click_map_x;
						} else {
							start_x = last_click_map_x;
							end_x = last_click_map_x + width;
						}
						if (mouse_map_y < last_click_map_y) {
							start_y = last_click_map_y - width;
							end_y = last_click_map_y;
						} else {
							start_y = last_click_map_y;
							end_y = last_click_map_y + width;
						}

						int center_x = start_x + (end_x - start_x) / 2;
						int center_y = start_y + (end_y - start_y) / 2;
						float radii = width / 2.0f + 0.005f;

						for (int y = start_y - 1; y <= end_y + 1; y++) {
							float dy = center_y - y;
							for (int x = start_x - 1; x <= end_x + 1; x++) {
								float dx = center_x - x;
								// printf("%f;%f\n", dx, dy);
								float distance = sqrt(dx * dx + dy * dy);
								if (distance < radii) {
									tilestodraw.push_back(Position(x, y, floor));
								}
								if (std::abs(distance - radii) < 1.5) {
									tilestoborder.push_back(Position(x, y, floor));
								}
							}
						}
					}
				}
				if (event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, alt_draw);
				} else {
					editor.draw(tilestodraw, tilestoborder, alt_draw);
				}
			}
		}
		editor.actionQueue->resetTimer();
		drawing = false;
		dragging_draw = false;
		dragging_draw_line = false;
		replace_dragging = false;
		editor.replace_brush = nullptr;
	}
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::OnMouseCameraClick(wxMouseEvent& event) {
	SetFocus();

	last_mmb_click_x = event.GetX();
	last_mmb_click_y = event.GetY();
	if (event.ControlDown()) {
		int screensize_x, screensize_y;
		static_cast<MapWindow*>(GetParent())->GetViewSize(&screensize_x, &screensize_y);

		static_cast<MapWindow*>(GetParent())->ScrollRelative(int(-screensize_x * (1.0 - zoom) * (std::max(cursor_x, 1) / double(screensize_x))), int(-screensize_y * (1.0 - zoom) * (std::max(cursor_y, 1) / double(screensize_y))));
		zoom = 1.0;
		Refresh();
	} else {
		screendragging = true;
	}
}

void MapCanvas::OnMouseCameraRelease(wxMouseEvent& event) {
	SetFocus();
	screendragging = false;
	if (event.ControlDown()) {
		// ...
		// Haven't moved much, it's a click!
	} else if (last_mmb_click_x > event.GetX() - 3 && last_mmb_click_x < event.GetX() + 3 && last_mmb_click_y > event.GetY() - 3 && last_mmb_click_y < event.GetY() + 3) {
		int screensize_x, screensize_y;
		static_cast<MapWindow*>(GetParent())->GetViewSize(&screensize_x, &screensize_y);
		static_cast<MapWindow*>(GetParent())->ScrollRelative(int(zoom * (2 * cursor_x - screensize_x)), int(zoom * (2 * cursor_y - screensize_y)));
		Refresh();
	}
}

void MapCanvas::OnMousePropertiesClick(wxMouseEvent& event) {
	SetFocus();

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);

	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	EndPasting();

	boundbox_selection = false;
	boundbox_deselection = false;
	lasso_selection = false;
	lasso_trace.clear();
	if (event.ShiftDown()) {
		// Shift + right drag = rectangular deselect; keep the current selection intact
		boundbox_selection = true;
		boundbox_deselection = true;
		lasso_selection = g_settings.getBoolean(Config::LASSO_SELECT);
		if (lasso_selection) {
			AppendLassoPoint(mouse_map_x, mouse_map_y);
		}
	} else if (!tile) {
		editor.selection.start(); // Start selection session
		editor.selection.clear(); // Clear out selection
		editor.selection.finish(); // End selection session
		editor.selection.updateSelectionCount();
	} else if (tile->isSelected()) {
		// Do nothing!
	} else {
		editor.selection.start(); // Start a selection session
		editor.selection.clear();
		editor.selection.commit();
		if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
			editor.selection.add(tile, tile->spawn);
		} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			editor.selection.add(tile, tile->creature);
		} else {
			Item* item = tile->getTopVisibleItem();
			if (item) {
				editor.selection.add(tile, item);
			}
		}
		editor.selection.finish(); // Finish the selection session
		editor.selection.updateSelectionCount();
	}

	last_click_x = int(event.GetX() * zoom);
	last_click_y = int(event.GetY() * zoom);

	int start_x, start_y;
	static_cast<MapWindow*>(GetParent())->GetViewStart(&start_x, &start_y);
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	last_click_map_z = floor;
	g_gui.RefreshView();
}

void MapCanvas::OnMousePropertiesRelease(wxMouseEvent& event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	bool did_boundbox_deselect = false;
	if (boundbox_selection && lasso_selection) {
		// Shift + right drag with Lasso Select on = freehand deselect
		did_boundbox_deselect = true;
		ApplyLassoSelection(true);
	} else if (boundbox_selection) {
		// Shift + right drag = rectangular deselect (zero-size box deselects a single tile)
		did_boundbox_deselect = true;
		if (last_click_map_x > mouse_map_x) {
			int tmp = mouse_map_x;
			mouse_map_x = last_click_map_x;
			last_click_map_x = tmp;
		}
		if (last_click_map_y > mouse_map_y) {
			int tmp = mouse_map_y;
			mouse_map_y = last_click_map_y;
			last_click_map_y = tmp;
		}

		editor.selection.start(); // Start a selection session
		switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
			case SELECT_CURRENT_FLOOR: {
				for (int x = last_click_map_x; x <= mouse_map_x; x++) {
					for (int y = last_click_map_y; y <= mouse_map_y; y++) {
						Tile* tile = editor.map.getTile(x, y, floor);
						if (!tile || !tile->isSelected()) {
							continue;
						}
						editor.selection.remove(tile);
					}
				}
				break;
			}
			case SELECT_ALL_FLOORS: {
				int start_x, start_y, start_z;
				int end_x, end_y, end_z;

				start_x = last_click_map_x;
				start_y = last_click_map_y;
				start_z = MAP_MAX_LAYER;
				end_x = mouse_map_x;
				end_y = mouse_map_y;
				end_z = floor;

				if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
					start_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
					start_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);

					end_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
					end_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
				}

				for (int z = start_z; z >= end_z; z--) {
					for (int x = start_x; x <= end_x; x++) {
						for (int y = start_y; y <= end_y; y++) {
							Tile* tile = editor.map.getTile(x, y, z);
							if (!tile || !tile->isSelected()) {
								continue;
							}
							editor.selection.remove(tile);
						}
					}
					if (z <= GROUND_LAYER && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
						start_x++;
						start_y++;
						end_x++;
						end_y++;
					}
				}
				break;
			}
			case SELECT_VISIBLE_FLOORS: {
				int start_x, start_y, start_z;
				int end_x, end_y, end_z;

				start_x = last_click_map_x;
				start_y = last_click_map_y;
				if (floor <= GROUND_LAYER) {
					start_z = GROUND_LAYER;
				} else {
					start_z = std::min(MAP_MAX_LAYER, floor + 2);
				}
				end_x = mouse_map_x;
				end_y = mouse_map_y;
				end_z = floor;

				if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
					start_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
					start_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);

					end_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
					end_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
				}

				for (int z = start_z; z >= end_z; z--) {
					for (int x = start_x; x <= end_x; x++) {
						for (int y = start_y; y <= end_y; y++) {
							Tile* tile = editor.map.getTile(x, y, z);
							if (!tile || !tile->isSelected()) {
								continue;
							}
							editor.selection.remove(tile);
						}
					}
					if (z <= GROUND_LAYER && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
						start_x++;
						start_y++;
						end_x++;
						end_y++;
					}
				}
				break;
			}
		}
		editor.selection.finish(); // Finish the selection session
		editor.selection.updateSelectionCount();
	} else if (event.ControlDown()) {
		// Nothing
	}

	if (!did_boundbox_deselect) {
		popup_menu->Update(Position(mouse_map_x, mouse_map_y, floor));
		PopupMenu(popup_menu);
	}

	editor.actionQueue->resetTimer();
	dragging = false;
	boundbox_selection = false;
	boundbox_deselection = false;
	lasso_selection = false;
	lasso_trace.clear();

	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	g_gui.RefreshView();
}

void MapCanvas::OnWheel(wxMouseEvent& event) {
	if (event.ControlDown()) {
		static double diff = 0.0;
		diff += event.GetWheelRotation();
		if (diff <= 1.0 || diff >= 1.0) {
			if (diff < 0.0) {
				g_gui.ChangeFloor(floor - 1);
			} else {
				g_gui.ChangeFloor(floor + 1);
			}
			diff = 0.0;
		}
		UpdatePositionStatus();
	} else if (event.AltDown()) {
		static double diff = 0.0;
		diff += event.GetWheelRotation();
		if (diff <= 1.0 || diff >= 1.0) {
			if (diff < 0.0) {
				g_gui.IncreaseBrushSize();
			} else {
				g_gui.DecreaseBrushSize();
			}
			diff = 0.0;
		}
	} else {
		double diff = -event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 640.0;
		double oldzoom = zoom;
		zoom += diff;

		if (zoom < 0.125) {
			diff = 0.125 - oldzoom;
			zoom = 0.125;
		}
		if (zoom > 25.00) {
			diff = 25.00 - oldzoom;
			zoom = 25.0;
		}

		UpdateZoomStatus();

		int screensize_x, screensize_y;
		static_cast<MapWindow*>(GetParent())->GetViewSize(&screensize_x, &screensize_y);

		// This took a day to figure out!
		int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x))) * GetContentScaleFactor();
		int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y))) * GetContentScaleFactor();

		static_cast<MapWindow*>(GetParent())->ScrollRelative(-scroll_x, -scroll_y);
	}

	Refresh();
}

void MapCanvas::OnLoseMouse(wxMouseEvent& event) {
	Refresh();
}

void MapCanvas::OnGainMouse(wxMouseEvent& event) {
	if (!event.LeftIsDown() && !event.RightIsDown()) {
		dragging = false;
		boundbox_selection = false;
		boundbox_deselection = false;
		lasso_selection = false;
		lasso_trace.clear();
		drawing = false;
	}
	if (!event.MiddleIsDown()) {
		screendragging = false;
	}

	Refresh();
}

void MapCanvas::OnKeyDown(wxKeyEvent& event) {
	// char keycode = event.GetKeyCode();
	//  std::cout << "Keycode " << keycode << std::endl;
	switch (event.GetKeyCode()) {
		case WXK_NUMPAD_ADD:
		case WXK_PAGEUP: {
			g_gui.ChangeFloor(floor - 1);
			break;
		}
		case WXK_NUMPAD_SUBTRACT:
		case WXK_PAGEDOWN: {
			g_gui.ChangeFloor(floor + 1);
			break;
		}
		case WXK_NUMPAD_MULTIPLY: {
			double diff = -0.3;

			double oldzoom = zoom;
			zoom += diff;

			if (zoom < 0.125) {
				diff = 0.125 - oldzoom;
				zoom = 0.125;
			}

			int screensize_x, screensize_y;
			static_cast<MapWindow*>(GetParent())->GetViewSize(&screensize_x, &screensize_y);

			// This took a day to figure out!
			int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x)));
			int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y)));

			static_cast<MapWindow*>(GetParent())->ScrollRelative(-scroll_x, -scroll_y);

			UpdatePositionStatus();
			UpdateZoomStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_DIVIDE: {
			double diff = 0.3;
			double oldzoom = zoom;
			zoom += diff;

			if (zoom > 25.00) {
				diff = 25.00 - oldzoom;
				zoom = 25.0;
			}

			int screensize_x, screensize_y;
			static_cast<MapWindow*>(GetParent())->GetViewSize(&screensize_x, &screensize_y);

			// This took a day to figure out!
			int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x)));
			int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y)));

			static_cast<MapWindow*>(GetParent())->ScrollRelative(-scroll_x, -scroll_y);

			UpdatePositionStatus();
			UpdateZoomStatus();
			Refresh();
			break;
		}
		// This will work like crap with non-us layouts, well, sucks for them until there is another solution.
		case '[':
		case '+': {
			g_gui.IncreaseBrushSize();
			Refresh();
			break;
		}
		case ']':
		case '-': {
			g_gui.DecreaseBrushSize();
			Refresh();
			break;
		}
		case WXK_NUMPAD_UP:
		case WXK_UP: {
			int start_x, start_y;
			static_cast<MapWindow*>(GetParent())->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			static_cast<MapWindow*>(GetParent())->Scroll(start_x, int(start_y - TileSize * tiles * zoom));
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_DOWN:
		case WXK_DOWN: {
			int start_x, start_y;
			static_cast<MapWindow*>(GetParent())->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			static_cast<MapWindow*>(GetParent())->Scroll(start_x, int(start_y + TileSize * tiles * zoom));
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_LEFT:
		case WXK_LEFT: {
			int start_x, start_y;
			static_cast<MapWindow*>(GetParent())->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			static_cast<MapWindow*>(GetParent())->Scroll(int(start_x - TileSize * tiles * zoom), start_y);
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_RIGHT:
		case WXK_RIGHT: {
			int start_x, start_y;
			static_cast<MapWindow*>(GetParent())->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if (event.ControlDown()) {
				tiles = 10;
			} else if (zoom == 1.0) {
				tiles = 1;
			}

			static_cast<MapWindow*>(GetParent())->Scroll(int(start_x + TileSize * tiles * zoom), start_y);
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_SPACE: { // Utility keys
			if (event.ControlDown()) {
				g_gui.FillDoodadPreviewBuffer();
				g_gui.RefreshView();
			} else {
				g_gui.SwitchMode();
			}
			break;
		}
		case WXK_TAB: { // Tab switch
			if (event.ShiftDown()) {
				g_gui.CycleTab(false);
			} else {
				g_gui.CycleTab(true);
			}
			break;
		}
		case WXK_DELETE: { // Delete
			editor.destroySelection();
			g_gui.RefreshView();
			break;
		}
		case 'z':
		case 'Z': { // Rotate counterclockwise (actually shift variaton, but whatever... :P)
			if (event.ControlDown() || event.AltDown()) {
				// Ctrl+Z / Ctrl+Shift+Z are Undo/Redo accelerators - don't eat them.
				event.Skip();
				break;
			}
			if (g_gui.TransformPaste(MapTransform::RotateCounterClockwise)) {
				break;
			}
			if (Brush* brush = g_gui.GetCurrentBrush()) {
				if (brush->isDoodad()) {
					const int old_variation = g_gui.GetBrushVariation();
					int nv = old_variation - 1;
					if (nv < 0) {
						nv = max(0, brush->getMaxVariation() - 1);
					}
					g_gui.SetBrushVariation(nv);
					if (g_gui.GetBrushVariation() == old_variation) {
						g_gui.RotateDoodadPreviewItems();
					}
				}
			}
			g_gui.RefreshView();
			break;
		}
		case 'x':
		case 'X': { // Rotate clockwise (actually shift variaton, but whatever... :P)
			if (event.ControlDown() || event.AltDown()) {
				// Ctrl+X is the Cut accelerator - don't eat it.
				event.Skip();
				break;
			}
			if (g_gui.TransformPaste(MapTransform::RotateClockwise)) {
				break;
			}
			if (Brush* brush = g_gui.GetCurrentBrush()) {
				if (brush->isDoodad()) {
					const int old_variation = g_gui.GetBrushVariation();
					int nv = old_variation + 1;
					if (nv >= brush->getMaxVariation()) {
						nv = 0;
					}
					g_gui.SetBrushVariation(nv);
					if (g_gui.GetBrushVariation() == old_variation) {
						g_gui.RotateDoodadPreviewItems();
					}
				}
			}
			g_gui.RefreshView();
			break;
		}
		case 'q':
		case 'Q': { // Select previous brush
			g_gui.SelectPreviousBrush();
			break;
		}
		// Hotkeys
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9': {
			int index = event.GetKeyCode() - '0';
			if (event.ControlDown()) {
				Hotkey hk;
				if (g_gui.IsSelectionMode()) {
					int view_start_x, view_start_y;
					static_cast<MapWindow*>(GetParent())->GetViewStart(&view_start_x, &view_start_y);
					int view_start_map_x = view_start_x / TileSize, view_start_map_y = view_start_y / TileSize;

					int view_screensize_x, view_screensize_y;
					static_cast<MapWindow*>(GetParent())->GetViewSize(&view_screensize_x, &view_screensize_y);

					int map_x = int(view_start_map_x + (view_screensize_x * zoom) / TileSize / 2);
					int map_y = int(view_start_map_y + (view_screensize_y * zoom) / TileSize / 2);

					hk = Hotkey(Position(map_x, map_y, floor));
				} else if (g_gui.GetCurrentBrush()) {
					// Drawing mode
					hk = Hotkey(g_gui.GetCurrentBrush());
				} else {
					break;
				}
				g_gui.SetHotkey(index, hk);
			} else {
				// Click hotkey
				Hotkey hk = g_gui.GetHotkey(index);
				if (hk.IsPosition()) {
					g_gui.SetSelectionMode();

					int map_x = hk.GetPosition().x;
					int map_y = hk.GetPosition().y;
					int map_z = hk.GetPosition().z;

					static_cast<MapWindow*>(GetParent())->Scroll(TileSize * map_x, TileSize * map_y, true);
					floor = map_z;

					g_gui.SetStatusText("Used hotkey " + i2ws(index));
					g_gui.RefreshView();
				} else if (hk.IsBrush()) {
					g_gui.SetDrawingMode();

					std::string name = hk.GetBrushname();
					Brush* brush = g_brushes.getBrush(name);
					if (brush == nullptr) {
						g_gui.SetStatusText("Brush \"" + wxstr(name) + "\" not found");
						return;
					}

					if (!g_gui.SelectBrush(brush)) {
						g_gui.SetStatusText("Brush \"" + wxstr(name) + "\" is not in any palette");
						return;
					}

					g_gui.SetStatusText("Used hotkey " + i2ws(index));
					g_gui.RefreshView();
				} else {
					g_gui.SetStatusText("Unassigned hotkey " + i2ws(index));
				}
			}
			break;
		}
		case 'd':
		case 'D': {
			keyCode = WXK_CONTROL_D;
			break;
		}
		case 'm':
		case 'M': {
			if (event.ControlDown() && event.ShiftDown()) {
				int mouse_map_x, mouse_map_y;
				MouseToMap(&mouse_map_x, &mouse_map_y);
				if (liveEditAllowed(editor, mouse_map_x, mouse_map_y)) {
					editor.promptAddMapComment(Position(mouse_map_x, mouse_map_y, floor));
				}
				break;
			}
			event.Skip();
			break;
		}
		case 'p':
		case 'P': {
			if (event.ControlDown() && event.ShiftDown() && editor.IsLive()) {
				int mouse_map_x, mouse_map_y;
				MouseToMap(&mouse_map_x, &mouse_map_y);
				if (liveEditAllowed(editor, mouse_map_x, mouse_map_y)) {
					editor.sendLivePing(Position(mouse_map_x, mouse_map_y, floor));
					g_gui.SetStatusText(wxString::Format("Ping sent at %d, %d, %d", mouse_map_x, mouse_map_y, floor));
					g_gui.RefreshView();
				}
				break;
			}
			event.Skip();
			break;
		}
		default: {
			event.Skip();
			break;
		}
	}
}

void MapCanvas::OnKeyUp(wxKeyEvent& event) {
	keyCode = WXK_NONE;
}

void MapCanvas::OnCopy(wxCommandEvent& WXUNUSED(event)) {
	if (!editor.IsClipboardAllowed()) {
		return;
	}
	if (g_gui.IsSelectionMode()) {
		editor.copybuffer.copy(editor, GetFloor());
	}
}

void MapCanvas::OnCut(wxCommandEvent& WXUNUSED(event)) {
	if (!editor.IsClipboardAllowed()) {
		return;
	}
	if (g_gui.IsSelectionMode()) {
		editor.copybuffer.cut(editor, GetFloor());
	}
	g_gui.RefreshView();
}

void MapCanvas::OnPaste(wxCommandEvent& WXUNUSED(event)) {
	if (!editor.IsClipboardAllowed()) {
		return;
	}
	g_gui.DoPaste();
	g_gui.RefreshView();
}

void MapCanvas::OnAddComment(wxCommandEvent& WXUNUSED(event)) {
	if (!liveEditAllowed(editor, last_click_map_x, last_click_map_y)) {
		return;
	}
	editor.promptAddMapComment(Position(last_click_map_x, last_click_map_y, last_click_map_z));
	g_gui.RefreshView();
}

void MapCanvas::OnRemoveComment(wxCommandEvent& WXUNUSED(event)) {
	if (!liveEditAllowed(editor, last_click_map_x, last_click_map_y)) {
		return;
	}
	editor.removeMapCommentAt(Position(last_click_map_x, last_click_map_y, last_click_map_z));
	g_gui.RefreshView();
}

void MapCanvas::OnPingHere(wxCommandEvent& WXUNUSED(event)) {
	if (!editor.IsLive()) {
		return;
	}
	if (!liveEditAllowed(editor, last_click_map_x, last_click_map_y)) {
		return;
	}
	editor.sendLivePing(Position(last_click_map_x, last_click_map_y, last_click_map_z));
	g_gui.SetStatusText(wxString::Format(
		"Ping sent at %d, %d, %d",
		last_click_map_x,
		last_click_map_y,
		last_click_map_z
	));
	g_gui.RefreshView();
}

void MapCanvas::OnDelete(wxCommandEvent& WXUNUSED(event)) {
	editor.destroySelection();
	g_gui.RefreshView();
}

void MapCanvas::OnCopyPosition(wxCommandEvent& WXUNUSED(event)) {
        if (editor.selection.size() == 0) {
                return;
        }

        Position minPos = editor.selection.minPosition();
        Position maxPos = editor.selection.maxPosition();

        std::ostringstream clip;
        if (minPos != maxPos) {
                clip << "{";
                clip << "fromx = " << minPos.x << ", ";
                clip << "tox = " << maxPos.x << ", ";
                clip << "fromy = " << minPos.y << ", ";
                clip << "toy = " << maxPos.y << ", ";
                if (minPos.z != maxPos.z) {
                        clip << "fromz = " << minPos.z << ", ";
                        clip << "toz = " << maxPos.z;
                } else {
                        clip << "z = " << minPos.z;
                }
                clip << "}";
        } else {
                switch (g_settings.getInteger(Config::COPY_POSITION_FORMAT)) {
			case 0:
				clip << "{x = " << minPos.x << ", y = " << minPos.y << ", z = " << minPos.z << "}";
				break;
			case 1:
				clip << "{\"x\":" << minPos.x << ",\"y\":" << minPos.y << ",\"z\":" << minPos.z << "}";
				break;
			case 2:
				clip << minPos.x << ", " << minPos.y << ", " << minPos.z;
				break;
			case 3:
				clip << "(" << minPos.x << ", " << minPos.y << ", " << minPos.z << ")";
				break;
			case 4:
				clip << "Position(" << minPos.x << ", " << minPos.y << ", " << minPos.z << ")";
				break;
			case 5:
				clip << "x=\"" << minPos.x << "\" y=\"" << minPos.y << "\" z=\"" << minPos.z << "\""; // New format
				break;
			case 6:
				clip << "centerx=\"" << minPos.x << "\" centery=\"" << minPos.y << "\" centerz=\"" << minPos.z << "\""; // Another new format
				break;
		}
	}

	if (wxTheClipboard->Open()) {
		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(wxstr(clip.str()));
		wxTheClipboard->SetData(obj);

                wxTheClipboard->Close();
        }
}

void MapCanvas::OnCopyRaidArea(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() == 0) {
		return;
	}

	Position minPos = editor.selection.minPosition();
	Position maxPos = editor.selection.maxPosition();

	wxString monsterName;
	while (true) {
		wxTextEntryDialog monsterDialog(this, wxT("Enter monster name"), wxT("Copy Raid Area"), monsterName);
		if (monsterDialog.ShowModal() != wxID_OK) {
			return;
		}

		monsterName = monsterDialog.GetValue();
		monsterName.Trim(true);
		monsterName.Trim(false);

		if (!monsterName.IsEmpty()) {
			break;
		}

		wxMessageBox(wxT("Monster name cannot be empty."), wxT("Copy Raid Area"), wxOK | wxICON_ERROR, this);
	}

	wxString amountStr = wxT("1");
	long amountValue = 0;
	while (true) {
		wxTextEntryDialog amountDialog(this, wxT("Amount"), wxT("Copy Raid Area"), amountStr);
		if (amountDialog.ShowModal() != wxID_OK) {
			return;
		}

		amountStr = amountDialog.GetValue();
		amountStr.Trim(true);
		amountStr.Trim(false);

		if (amountStr.ToLong(&amountValue) && amountValue > 0) {
			break;
		}

		wxMessageBox(wxT("Amount must be a positive integer."), wxT("Copy Raid Area"), wxOK | wxICON_ERROR, this);
	}

	wxString clip = wxString::Format(
		wxT("<areaspawn delay=\"0\" fromx=\"%d\" fromy=\"%d\" fromz=\"%d\" tox=\"%d\" toy=\"%d\" toz=\"%d\">\n"),
		minPos.x,
		minPos.y,
		minPos.z,
		maxPos.x,
		maxPos.y,
		maxPos.z);
	clip << wxString::Format(wxT("\t<monster name=\"%s\" amount=\"%ld\" />\n"), monsterName.wx_str(), amountValue);
	clip << wxT("</areaspawn>");

	if (wxTheClipboard->Open()) {
		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(clip);
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

namespace {

// Composites the base outfit sprite, its mount (if any), and all currently active addons for one
// direction/animation frame, mirroring DrawCreatureBrushInSlot's layering (palette_creature.cpp).
wxImage ComposeCreatureFrame(GameSprite* spr, const Outfit& outfit, int dir, int frame) {
	wxImage image;
	int pattern_z = 0;
	if (outfit.lookMount != 0) {
		if (GameSprite* mountSpr = g_gui.gfx.getCreatureSprite(outfit.lookMount)) {
			Outfit mountOutfit;
			mountOutfit.lookType = outfit.lookMount;
			mountOutfit.lookHead = outfit.lookMountHead;
			mountOutfit.lookBody = outfit.lookMountBody;
			mountOutfit.lookLegs = outfit.lookMountLegs;
			mountOutfit.lookFeet = outfit.lookMountFeet;
			image = mountSpr->getCreatureImage(dir, 0, 0, mountOutfit, frame);
			pattern_z = std::min<int>(1, spr->pattern_z - 1);
		}
	}

	for (int addon = 0; addon < spr->pattern_y; ++addon) {
		if (addon > 0 && !(outfit.lookAddon & (1 << (addon - 1)))) {
			continue;
		}
		wxImage part = spr->getCreatureImage(dir, addon, pattern_z, outfit, frame);
		if (!image.IsOk()) {
			image = part;
		} else if (part.IsOk()) {
			image.Paste(part, 0, 0);
		}
	}

	return image;
}

// Builds a grid of the creature's full outfit with directions running left-to-right (North, East,
// South, West - matching the Direction enum order) and animation frames stacked top-to-bottom,
// so the export includes every walking frame, not just the single pose it faces on the map.
wxBitmap BuildCreatureSheet(GameSprite* spr, const Outfit& outfit, const wxColour& transparentKey) {
	const int directions = std::max<int>(1, std::min<int>(spr->pattern_x, DIRECTION_LAST - DIRECTION_FIRST + 1));
	const int frameCount = std::max<int>(1, spr->frames);
	const int cellWidth = std::max<int>(1, spr->width) * TileSize;
	const int cellHeight = std::max<int>(1, spr->height) * TileSize;

	wxBitmap sheet(cellWidth * directions, cellHeight * frameCount, 24);
	wxMemoryDC dc;
	dc.SelectObject(sheet);
	dc.SetBackground(wxBrush(transparentKey));
	dc.Clear();

	for (int frame = 0; frame < frameCount; ++frame) {
		for (int dir = 0; dir < directions; ++dir) {
			wxImage frameImage = ComposeCreatureFrame(spr, outfit, dir, frame);
			if (!frameImage.IsOk()) {
				continue;
			}
			if (frameImage.GetWidth() != cellWidth || frameImage.GetHeight() != cellHeight) {
				frameImage = frameImage.GetSubImage(wxRect(0, 0, cellWidth, cellHeight));
			}
			wxBitmap cellBitmap(frameImage, -1);
			dc.DrawBitmap(cellBitmap, dir * cellWidth, frame * cellHeight, true);
		}
	}

	dc.SelectObject(wxNullBitmap);
	return sheet;
}

} // namespace

void MapCanvas::OnExportSpritesheet(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() == 0) {
		return;
	}

	Position minPos = editor.selection.minPosition();

	// Same colour key GameSprite uses for transparency, so masked-out pixels convert cleanly to PNG alpha.
	const wxColour transparentKey(0xFF, 0x00, 0xFF);

	struct SpriteDraw {
		GameSprite* sprite; // set for items; null for creatures (use image instead)
		wxImage image;
		int pixelX;
		int pixelY;
		int pixelWidth;
		int pixelHeight;
	};

	struct CreatureSheetSource {
		GameSprite* sprite;
		Outfit outfit;
	};

	std::vector<SpriteDraw> draws;
	std::vector<CreatureSheetSource> creatureSheetSources;
	std::set<std::string> names;

	int canvasMinX = 0, canvasMinY = 0, canvasMaxX = 0, canvasMaxY = 0;
	bool first = true;

	auto extendCanvas = [&](int pixelX, int pixelY, int pixelWidth, int pixelHeight) {
		if (first) {
			canvasMinX = pixelX;
			canvasMinY = pixelY;
			canvasMaxX = pixelX + pixelWidth;
			canvasMaxY = pixelY + pixelHeight;
			first = false;
		} else {
			canvasMinX = std::min(canvasMinX, pixelX);
			canvasMinY = std::min(canvasMinY, pixelY);
			canvasMaxX = std::max(canvasMaxX, pixelX + pixelWidth);
			canvasMaxY = std::max(canvasMaxY, pixelY + pixelHeight);
		}
	};

	for (Tile* tile : editor.selection) {
		// A multi-tile sprite is anchored so its bottom-right cell sits on the item's own tile,
		// extending up and to the left (see MapDrawer::BlitSpriteType) - so its top-left pixel
		// can fall outside this tile's own 32x32 cell, and outside the selection's tile bounds.
		int tilePixelRight = (tile->getX() - minPos.x + 1) * TileSize;
		int tilePixelBottom = (tile->getY() - minPos.y + 1) * TileSize;

		for (Item* item : tile->getSelectedItems()) {
			GameSprite* sprite = g_items.getItemType(item->getID()).sprite;
			if (!sprite) {
				continue;
			}

			int pixelWidth = std::max<int>(1, sprite->width) * TileSize;
			int pixelHeight = std::max<int>(1, sprite->height) * TileSize;
			int pixelX = tilePixelRight - pixelWidth;
			int pixelY = tilePixelBottom - pixelHeight;

			draws.push_back({ sprite, wxImage(), pixelX, pixelY, pixelWidth, pixelHeight });

			std::string name = item->getName();
			if (!name.empty()) {
				names.insert(name);
			}

			extendCanvas(pixelX, pixelY, pixelWidth, pixelHeight);
		}

		if (tile->creature && tile->creature->isSelected()) {
			Creature* creature = tile->creature;
			const Outfit& outfit = creature->getLookType();

			if (outfit.lookItem != 0) {
				GameSprite* sprite = g_items.getItemType(outfit.lookItem).sprite;
				if (sprite) {
					int pixelWidth = std::max<int>(1, sprite->width) * TileSize;
					int pixelHeight = std::max<int>(1, sprite->height) * TileSize;
					int pixelX = tilePixelRight - pixelWidth;
					int pixelY = tilePixelBottom - pixelHeight;

					draws.push_back({ sprite, wxImage(), pixelX, pixelY, pixelWidth, pixelHeight });
					extendCanvas(pixelX, pixelY, pixelWidth, pixelHeight);
				}
			} else if (GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType)) {
				// The creature's full directions/frames grid (built below) already covers its
				// current pose, so it isn't placed into the positional item scene as well.
				creatureSheetSources.push_back({ spr, outfit });
			}

			std::string name = creature->getName();
			if (!name.empty()) {
				names.insert(name);
			}
		}
	}

	if (draws.empty() && creatureSheetSources.empty()) {
		return;
	}

	// Each selected creature's full directions/frames grid is stacked above the positional item
	// scene, in that order (creature sheets first, then the placed items below).
	std::vector<wxBitmap> blocks;
	for (const CreatureSheetSource& source : creatureSheetSources) {
		blocks.push_back(BuildCreatureSheet(source.sprite, source.outfit, transparentKey));
	}

	if (!draws.empty()) {
		int sceneWidth = canvasMaxX - canvasMinX;
		int sceneHeight = canvasMaxY - canvasMinY;

		wxBitmap scene(sceneWidth, sceneHeight, 24);
		wxMemoryDC dc;
		dc.SelectObject(scene);
		dc.SetBackground(wxBrush(transparentKey));
		dc.Clear();

		for (const SpriteDraw& draw : draws) {
			int drawX = draw.pixelX - canvasMinX;
			int drawY = draw.pixelY - canvasMinY;
			if (draw.sprite) {
				draw.sprite->DrawTo(&dc, SPRITE_SIZE_ACTUAL, drawX, drawY, draw.pixelWidth, draw.pixelHeight);
			} else {
				wxBitmap creatureBitmap(draw.image, -1);
				dc.DrawBitmap(creatureBitmap, drawX, drawY, true);
			}
		}

		dc.SelectObject(wxNullBitmap);
		blocks.push_back(scene);
	}

	// No gap between blocks - keeps the sheet's total height an exact multiple of tile pixels,
	// so nothing drifts off a tile-aligned grid.
	const int blockGap = 0;
	int finalWidth = 0;
	int finalHeight = 0;
	for (const wxBitmap& block : blocks) {
		finalWidth = std::max(finalWidth, block.GetWidth());
		finalHeight += (finalHeight > 0 ? blockGap : 0) + block.GetHeight();
	}

	wxBitmap finalBitmap(finalWidth, finalHeight, 24);
	wxMemoryDC finalDc;
	finalDc.SelectObject(finalBitmap);
	finalDc.SetBackground(wxBrush(transparentKey));
	finalDc.Clear();

	int runningY = 0;
	for (const wxBitmap& block : blocks) {
		finalDc.DrawBitmap(block, 0, runningY);
		runningY += block.GetHeight() + blockGap;
	}
	finalDc.SelectObject(wxNullBitmap);

	wxString defaultName;
	for (const std::string& name : names) {
		if (!defaultName.IsEmpty()) {
			defaultName += " ";
		}
		defaultName += wxstr(name);
	}
	if (defaultName.IsEmpty()) {
		defaultName = "spritesheet";
	}
	static const wxString invalidChars = wxT("\\/:*?\"<>|");
	for (size_t i = 0; i < invalidChars.size(); ++i) {
		defaultName.Replace(wxString(invalidChars[i]), wxT("_"));
	}
	defaultName += ".png";

	wxFileDialog dialog(this, "Export Spritesheet", "", defaultName, "PNG files (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dialog.ShowModal() == wxID_OK) {
		wxImage image = finalBitmap.ConvertToImage();
		image.SetMaskColour(transparentKey.Red(), transparentKey.Green(), transparentKey.Blue());
		image.SaveFile(dialog.GetPath(), wxBITMAP_TYPE_PNG);
	}
}

void MapCanvas::OnCopyServerId(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(editor.selection.size() == 1);

	if (wxTheClipboard->Open()) {
		Tile* tile = editor.selection.getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(i2ws(item->getID()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnCopyClientId(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(editor.selection.size() == 1);

	if (wxTheClipboard->Open()) {
		Tile* tile = editor.selection.getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(i2ws(item->getClientID()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnCopyName(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(editor.selection.size() == 1);

	if (wxTheClipboard->Open()) {
		Tile* tile = editor.selection.getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(wxstr(item->getName()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnBrowseTile(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}

	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	ASSERT(tile->isSelected());
	Tile* new_tile = tile->deepCopy(editor.map);

	wxDialog* w = new BrowseTileWindow(g_gui.root, new_tile, wxPoint(cursor_x, cursor_y));

	int ret = w->ShowModal();
	if (ret != 0) {
		Action* action = editor.actionQueue->createAction(ACTION_DELETE_TILES);
		action->addChange(newd Change(new_tile));
		editor.addAction(action);
	} else {
		// Cancel
		delete new_tile;
	}

	w->Destroy();
}

void MapCanvas::ApplyTransform(MapTransform transform) {
	// A pending paste takes precedence, so it can be oriented before being placed
	if (g_gui.TransformPaste(transform)) {
		return;
	}

	editor.transformSelection(transform);
	g_gui.RefreshView();
}

void MapCanvas::OnRotateSelectionClockwise(wxCommandEvent& WXUNUSED(event)) {
	ApplyTransform(MapTransform::RotateClockwise);
}

void MapCanvas::OnRotateSelectionCounterClockwise(wxCommandEvent& WXUNUSED(event)) {
	ApplyTransform(MapTransform::RotateCounterClockwise);
}

void MapCanvas::OnFlipSelectionHorizontal(wxCommandEvent& WXUNUSED(event)) {
	ApplyTransform(MapTransform::FlipHorizontal);
}

void MapCanvas::OnFlipSelectionVertical(wxCommandEvent& WXUNUSED(event)) {
	ApplyTransform(MapTransform::FlipVertical);
}

void MapCanvas::OnContentAwareFill(wxCommandEvent& WXUNUSED(event)) {
	// Route through the menubar action so the options dialog and the fill
	// behavior live in one place
	wxCommandEvent menu_event(wxEVT_COMMAND_MENU_SELECTED, MAIN_FRAME_MENU + MenuBar::CONTENT_AWARE_FILL_SELECTION);
	g_gui.root->GetEventHandler()->AddPendingEvent(menu_event);
}

void MapCanvas::OnRotateItem(wxCommandEvent& WXUNUSED(event)) {
	Tile* tile = editor.selection.getSelectedTile();

	Action* action = editor.actionQueue->createAction(ACTION_ROTATE_ITEM);

	Tile* new_tile = tile->deepCopy(editor.map);

	ItemVector selected_items = new_tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);

	selected_items.front()->doRotate();

	action->addChange(newd Change(new_tile));

	editor.actionQueue->addAction(action);
	g_gui.RefreshView();
}

void MapCanvas::OnGotoDestination(wxCommandEvent& WXUNUSED(event)) {
	Tile* tile = editor.selection.getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);
	Teleport* teleport = dynamic_cast<Teleport*>(selected_items.front());
	if (teleport) {
		Position pos = teleport->getDestination();
		g_gui.SetScreenCenterPosition(pos);
	}
}

void MapCanvas::OnSwitchDoor(wxCommandEvent& WXUNUSED(event)) {
	Tile* tile = editor.selection.getSelectedTile();

	Action* action = editor.actionQueue->createAction(ACTION_SWITCHDOOR);

	Tile* new_tile = tile->deepCopy(editor.map);

	ItemVector selected_items = new_tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);

	DoorBrush::switchDoor(selected_items.front());

	action->addChange(newd Change(new_tile));

	editor.actionQueue->addAction(action);
	g_gui.RefreshView();
}

void MapCanvas::OnSelectRAWBrush(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = tile->getTopSelectedItem();

	if (item && item->getRAWBrush()) {
		g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
	}
}

void MapCanvas::OnSelectGroundBrush(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	GroundBrush* bb = tile->getGroundBrush();

	if (bb) {
		g_gui.SelectBrush(bb, TILESET_TERRAIN);
	}
}

void MapCanvas::OnSelectDoodadBrush(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = tile->getTopSelectedItem();
	if (!item) {
		return;
	}

	// getDoodadBrush() is the brush shown for this item in the doodad palette.
	// It is not necessarily an actual DoodadBrush (it can be a RAW fallback,
	// see Tileset::loadTileset), so select it as a generic brush and only apply
	// the doodad variation when asDoodad() actually returns a DoodadBrush.
	Brush* brush = item->getDoodadBrush();
	if (!brush) {
		return;
	}

	g_gui.SelectBrush(brush, TILESET_DOODAD);
	if (DoodadBrush* doodad_brush = brush->asDoodad()) {
		g_gui.SetBrushVariation(doodad_brush->getVariationForItemId(item->getID()));
	}
}

void MapCanvas::OnSelectDoorBrush(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = tile->getTopSelectedItem();

	if (item) {
		g_gui.SelectBrush(item->getDoorBrush(), TILESET_TERRAIN);
	}
}

void MapCanvas::OnSelectWallBrush(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getWall();
	WallBrush* wb = wall->getWallBrush();

	if (wb) {
		g_gui.SelectBrush(wb, TILESET_TERRAIN);
	}
}

void MapCanvas::OnSelectCarpetBrush(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getCarpet();
	CarpetBrush* cb = wall->getCarpetBrush();

	if (cb) {
		g_gui.SelectBrush(cb);
	}
}

void MapCanvas::OnSelectTableBrush(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getTable();
	TableBrush* tb = wall->getTableBrush();

	if (tb) {
		g_gui.SelectBrush(tb);
	}
}

void MapCanvas::OnSelectHouseBrush(wxCommandEvent& WXUNUSED(event)) {
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}

	if (tile->isHouseTile()) {
		House* house = editor.map.houses.getHouse(tile->getHouseID());
		if (house) {
			g_gui.house_brush->setHouse(house);
			g_gui.SelectBrush(g_gui.house_brush, TILESET_HOUSE);
		}
	}
}


void MapCanvas::OnSelectCreatureBrush(wxCommandEvent& WXUNUSED(event)) {
	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}

	if (tile->creature) {
		g_gui.SelectBrush(tile->creature->getBrush(), TILESET_CREATURE);
	}
}

void MapCanvas::OnSelectSpawnBrush(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SelectBrush(g_gui.spawn_brush, TILESET_CREATURE);
}

void MapCanvas::OnSelectMoveTo(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}

	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	ASSERT(tile->isSelected());
	Tile* new_tile = tile->deepCopy(editor.map);

	wxDialog* w = nullptr;

	ItemVector selected_items = new_tile->getSelectedItems();

	Item* item = nullptr;
	int count = 0;
	for (ItemVector::iterator it = selected_items.begin(); it != selected_items.end(); ++it) {
		++count;
		if ((*it)->isSelected()) {
			item = *it;
		}
	}

	if (item) {
		w = newd TilesetWindow(g_gui.root, &editor.map, new_tile, item);
	} else {
		return;
	}

	int ret = w->ShowModal();
	if (ret != 0) {
		Action* action = editor.actionQueue->createAction(ACTION_CHANGE_PROPERTIES);
		action->addChange(newd Change(new_tile));
		editor.addAction(action);

		g_gui.RebuildPalettes();
	} else {
		// Cancel!
		delete new_tile;
	}
	w->Destroy();
}

void MapCanvas::OnCreateWall(wxCommandEvent& WXUNUSED(event)) {
	uint16_t pieceIds[4];
	if (!extractWallSquare(editor.selection, pieceIds)) {
		g_gui.PopupDialog("Create Wall", "Select exactly four items placed in a 2x2 square first.", wxOK);
		return;
	}

	wxString name = wxGetTextFromUser(
		"Name for the new wall (top-left = pole, top-right = horizontal,\nbottom-left = vertical, bottom-right = corner):",
		"Create Wall", "", this
	);
	name.Trim(true).Trim(false);
	if (name.IsEmpty()) {
		return; // cancelled
	}

	wxString error;
	if (CreateWallBrush(std::string(name.mb_str()), pieceIds, "Walls", error)) {
		g_gui.PopupDialog("Wall created", "The wall has been created and added to the 'Walls' tileset.", wxOK);
		g_gui.DestroyPalettes();
		g_gui.NewPalette();
	} else {
		g_gui.PopupDialog("Error", error, wxOK);
	}
}

void MapCanvas::OnReplaceWall(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}

	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}

	Item* wallItem = nullptr;
	ItemVector selected_items = tile->getSelectedItems();
	if (selected_items.size() == 1 && selected_items.front()->isWall()) {
		wallItem = selected_items.front();
	} else {
		wallItem = tile->getWall();
	}

	if (!wallItem) {
		return;
	}

	WallBrush* sourceBrush = wallItem->getWallBrush();
	if (!sourceBrush || sourceBrush->isWallDecoration() || !sourceBrush->visibleInPalette()) {
		return;
	}

	ReplaceWallDialog dialog(this, editor, tile->getPosition(), sourceBrush);
	dialog.ShowModal();
}

void MapCanvas::OnReplaceGround(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}

	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}

	GroundBrush* sourceBrush = tile->getGroundBrush();
	if (!sourceBrush || !sourceBrush->visibleInPalette()) {
		return;
	}

	ReplaceGroundDialog dialog(this, editor, tile->getPosition(), sourceBrush);
	dialog.ShowModal();
}

void MapCanvas::OnReplaceWithSearchItem(wxCommandEvent& WXUNUSED(event)) {
	if (!selectionHasSelectedItems(editor.selection)) {
		return;
	}

	FindItemDialog dialog(g_gui.root, "Replace With Item");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::FIND_ITEM_MODE));
	if (dialog.ShowModal() != wxID_OK) {
		dialog.Destroy();
		return;
	}

	const uint16_t withId = dialog.getResultID();
	if (withId != 0) {
		replaceSelectedItemsWith(editor, withId);
		g_settings.setInteger(Config::FIND_ITEM_MODE, (int)dialog.getSearchMode());
		g_gui.RefreshView();
	}

	dialog.Destroy();
}

void MapCanvas::OnProperties(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() != 1) {
		return;
	}

	Tile* tile = editor.selection.getSelectedTile();
	if (!tile) {
		return;
	}
	ASSERT(tile->isSelected());
	Tile* new_tile = tile->deepCopy(editor.map);

	ObjectPropertiesWindowBase* w = nullptr;
	Item* protected_item = nullptr;

	if (new_tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
		w = newd OldPropertiesWindow(this, &editor.map, new_tile, new_tile->creature);
	} else {
		ItemVector original_selected_items = tile->getSelectedItems();
		ItemVector selected_items = new_tile->getSelectedItems();

		Item* item = nullptr;
		for (size_t i = 0; i < selected_items.size(); ++i) {
			if (selected_items[i]->isSelected()) {
				item = selected_items[i];
				if (i < original_selected_items.size()) {
					protected_item = original_selected_items[i];
				}
			}
		}

		if (item) {
			if (editor.map.getVersion().otbm >= MAP_OTBM_4) {
				w = newd PropertiesWindow(this, &editor.map, new_tile, item);
			} else {
				w = newd OldPropertiesWindow(this, &editor.map, new_tile, item);
			}
		} else {
			delete new_tile;
			return;
		}
	}

	ShowObjectProperties(w, new_tile, protected_item);
}

void MapCanvas::ChangeFloor(int new_floor) {
	ASSERT(new_floor >= 0 || new_floor < MAP_LAYERS);
	int old_floor = floor;
	floor = new_floor;
	if (old_floor != new_floor) {
		UpdatePositionStatus();
		g_gui.root->UpdateFloorMenu();
		g_gui.UpdateMinimap(true);
	}
	Refresh();
}

void MapCanvas::EnterDrawingMode() {
	dragging = false;
	boundbox_selection = false;
	boundbox_deselection = false;
	lasso_selection = false;
	lasso_trace.clear();
	EndPasting();
	Refresh();
}

void MapCanvas::EnterSelectionMode() {
	drawing = false;
	dragging_draw = false;
	dragging_draw_line = false;
	replace_dragging = false;
	editor.replace_brush = nullptr;
	Refresh();
}

bool MapCanvas::isPasting() const {
	return g_gui.IsPasting();
}

void MapCanvas::StartPasting() {
	g_gui.StartPasting();
}

void MapCanvas::EndPasting() {
	g_gui.EndPasting();
}

void MapCanvas::Reset() {
	cursor_x = 0;
	cursor_y = 0;

	zoom = 1.0;
	floor = GROUND_LAYER;

	dragging = false;
	boundbox_selection = false;
	boundbox_deselection = false;
	lasso_selection = false;
	lasso_trace.clear();
	screendragging = false;
	drawing = false;
	dragging_draw = false;
	dragging_draw_line = false;

	replace_dragging = false;
	editor.replace_brush = nullptr;

	drag_start_x = -1;
	drag_start_y = -1;
	drag_start_z = -1;

	last_click_map_x = -1;
	last_click_map_y = -1;
	last_click_map_z = -1;

	last_mmb_click_x = -1;
	last_mmb_click_y = -1;
	options_revision = std::numeric_limits<uint32_t>::max();

	editor.selection.clear();
	editor.actionQueue->clear();
}

MapPopupMenu::MapPopupMenu(Editor& editor) : wxMenu(""), editor(editor) {
	////
}

MapPopupMenu::~MapPopupMenu() {
	////
}

void MapPopupMenu::Update(const Position& cursorTile) {
	// Clear the menu of all items
	while (GetMenuItemCount() != 0) {
		wxMenuItem* m_item = FindItemByPosition(0);
		wxMenu* submenu = m_item->GetSubMenu();
		Delete(m_item);
		if (submenu) {
			delete submenu;
		}
	}

	const std::vector<const MapComment*> tileComments = editor.map.getComments().atPosition(cursorTile.x, cursorTile.y, cursorTile.z);
	const bool allowCommentEdit = liveEditAllowed(editor, cursorTile.x, cursorTile.y);

	wxMenuItem* addCommentItem = Append(MAP_POPUP_MENU_ADD_COMMENT, "Add &Comment...", "Leave a note for other mappers on this tile");
	addCommentItem->Enable(allowCommentEdit);
	wxMenuItem* removeCommentItem = Append(MAP_POPUP_MENU_REMOVE_COMMENT, "Remove Comment...", "Remove a comment from this tile");
	removeCommentItem->Enable(allowCommentEdit && !tileComments.empty());

	if (editor.IsLive()) {
		wxMenuItem* pingItem = Append(MAP_POPUP_MENU_PING_HERE, "Ping &Here\tCTRL+SHIFT+P", "Draw attention to this tile for other mappers (Alt+Shift+click)");
		pingItem->Enable(allowCommentEdit);
	}

	AppendSeparator();

	bool anything_selected = editor.selection.size() != 0;
	const bool allow_clipboard = editor.IsClipboardAllowed();

	wxMenuItem* cutItem = Append(MAP_POPUP_MENU_CUT, "&Cut\tCTRL+X", "Cut out all selected items");
	cutItem->Enable(anything_selected && allow_clipboard);

	wxMenuItem* copyItem = Append(MAP_POPUP_MENU_COPY, "&Copy\tCTRL+C", "Copy all selected items");
	copyItem->Enable(anything_selected && allow_clipboard);

	wxMenuItem* copyPositionItem = Append(MAP_POPUP_MENU_COPY_POSITION, "&Copy Position", "Copy the position as a lua table");
	copyPositionItem->Enable(anything_selected);

	wxMenuItem* copyRaidAreaItem = Append(MAP_POPUP_MENU_COPY_RAID_AREA, "Copy Raid Area", "Copy the selection bounds for raid scripts");
	copyRaidAreaItem->Enable(anything_selected);

	wxMenuItem* exportSpritesheetItem = Append(MAP_POPUP_MENU_EXPORT_SPRITESHEET, "Export &Spritesheet...", "Export the selected items as a PNG spritesheet, preserving their layout");
	exportSpritesheetItem->Enable(anything_selected);

	wxMenuItem* pasteItem = Append(MAP_POPUP_MENU_PASTE, "&Paste\tCTRL+V", "Paste items in the copybuffer here");
	pasteItem->Enable(editor.copybuffer.canPaste() && allow_clipboard);

	wxMenuItem* deleteItem = Append(MAP_POPUP_MENU_DELETE, "&Delete\tDEL", "Removes all seleceted items");
	deleteItem->Enable(anything_selected);

	if (anything_selected) {
		wxMenu* transformMenu = newd wxMenu;
		transformMenu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_CW, "Rotate &Clockwise\tCTRL+R", "Rotate the selected area 90 degrees clockwise");
		transformMenu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_CCW, "Rotate Counter-Clock&wise\tCTRL+ALT+R", "Rotate the selected area 90 degrees counter-clockwise");
		transformMenu->Append(MAP_POPUP_MENU_FLIP_SELECTION_HORIZONTAL, "Flip &Horizontally\tCTRL+SHIFT+H", "Mirror the selected area along its vertical axis");
		transformMenu->Append(MAP_POPUP_MENU_FLIP_SELECTION_VERTICAL, "Flip &Vertically\tCTRL+SHIFT+V", "Mirror the selected area along its horizontal axis");
		AppendSubMenu(transformMenu, "&Transform", "Rotate or flip the selected area");

		Append(MAP_POPUP_MENU_CONTENT_AWARE_FILL, "Content-Aware &Fill...\tCTRL+SHIFT+B", "Replace the selected area with content sampled from its surroundings");
	}

	const bool has_selected_items = selectionHasSelectedItems(editor.selection);
	const bool allow_replace = has_selected_items && !editor.IsLiveClient();
	if (allow_replace) {
		wxMenu* replaceMenu = newd wxMenu;
		replaceMenu->Append(MAP_POPUP_MENU_REPLACE_WITH_SEARCH, "Search for &Item...", "Replace selected items with another item");
		AppendSubMenu(replaceMenu, "Replace &With", "Replace selected items with another item");
	}

	uint16_t wallPieces[4];
	if (extractWallSquare(editor.selection, wallPieces)) {
		AppendSeparator();
		Append(MAP_POPUP_MENU_CREATE_WALL, "Create &Wall from Square...", "Turn this 2x2 of items into a new wall brush in the Walls tileset");
	}

	if (anything_selected) {
		if (editor.selection.size() == 1) {
			Tile* tile = editor.selection.getSelectedTile();
			ItemVector selected_items = tile->getSelectedItems();

			bool hasWall = false;
			bool hasCarpet = false;
			bool hasTable = false;
			Item* topItem = nullptr;
			Item* topSelectedItem = (selected_items.size() == 1 ? selected_items.back() : nullptr);
			Creature* topCreature = tile->creature;
			Spawn* topSpawn = tile->spawn;

			for (auto* item : tile->items) {
				if (item->isWall()) {
					Brush* wb = item->getWallBrush();
					if (wb && wb->visibleInPalette()) {
						hasWall = true;
					}
				}
				if (item->isTable()) {
					Brush* tb = item->getTableBrush();
					if (tb && tb->visibleInPalette()) {
						hasTable = true;
					}
				}
				if (item->isCarpet()) {
					Brush* cb = item->getCarpetBrush();
					if (cb && cb->visibleInPalette()) {
						hasCarpet = true;
					}
				}
				if (item->isSelected()) {
					topItem = item;
				}
			}
			if (!topItem) {
				topItem = tile->ground;
			}

			AppendSeparator();

			if (topSelectedItem) {
				Append(MAP_POPUP_MENU_COPY_SERVER_ID, "Copy Item Server Id", "Copy the server id of this item");
				Append(MAP_POPUP_MENU_COPY_CLIENT_ID, "Copy Item Client Id", "Copy the client id of this item");
				Append(MAP_POPUP_MENU_COPY_NAME, "Copy Item Name", "Copy the name of this item");
				if (g_settings.getBoolean(Config::SHOW_MAKE_SCRIPT_MENU)) {
					Append(MAP_POPUP_MENU_CREATE_GENERATE_SCRIPT, "Make [Create] Script", "generate(...) script");
					Append(MAP_POPUP_MENU_CREATE_REMOVE_SCRIPT, "Make [Remove] Script", "remove(...) script");
					Append(MAP_POPUP_MENU_CREATE_CREATE_SCRIPT, "Make [Create and Remove] Script", "create(...) script");
					Append(MAP_POPUP_MENU_CREATE_CHECK_SCRIPT, "Make [Check] Script", "checkitem(...) script");
				}
				AppendSeparator();
			}

			if (topSelectedItem || topCreature || topItem) {
				Teleport* teleport = dynamic_cast<Teleport*>(topSelectedItem);
				if (topSelectedItem && (topSelectedItem->isBrushDoor() || topSelectedItem->isRoteable() || teleport)) {

					if (topSelectedItem->isRoteable()) {
						Append(MAP_POPUP_MENU_ROTATE, "&Rotate item", "Rotate this item");
					}

					if (teleport && teleport->hasDestination()) {
						Append(MAP_POPUP_MENU_GOTO, "&Go To Destination", "Go to the destination of this teleport");
					}

					if (topSelectedItem->isDoor()) {
						if (topSelectedItem->isOpen()) {
							Append(MAP_POPUP_MENU_SWITCH_DOOR, "&Close door", "Close this door");
						} else {
							Append(MAP_POPUP_MENU_SWITCH_DOOR, "&Open door", "Open this door");
						}
						AppendSeparator();
					}
				}

				if (topCreature) {
					Append(MAP_POPUP_MENU_SELECT_CREATURE_BRUSH, "Select Creature", "Uses the current creature as a creature brush");
				}

				if (topSpawn) {
					Append(MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, "Select Spawn", "Select the spawn brush");
				}

				Append(MAP_POPUP_MENU_SELECT_RAW_BRUSH, "Select RAW", "Uses the top item as a RAW brush");

				if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
					Append(MAP_POPUP_MENU_MOVE_TO_TILESET, "Move To Tileset", "Move this item to any tileset");
				}

				if (hasWall) {
					Append(MAP_POPUP_MENU_SELECT_WALL_BRUSH, "Select Wallbrush", "Uses the current item as a wallbrush");
					Append(MAP_POPUP_MENU_REPLACE_WALL, "Replace &Wall...", "Replace all connected walls, doors, and windows with another wall brush");
				}

				if (hasCarpet) {
					Append(MAP_POPUP_MENU_SELECT_CARPET_BRUSH, "Select Carpetbrush", "Uses the current item as a carpetbrush");
				}

				if (hasTable) {
					Append(MAP_POPUP_MENU_SELECT_TABLE_BRUSH, "Select Tablebrush", "Uses the current item as a tablebrush");
				}

				if (topSelectedItem && topSelectedItem->getDoodadBrush() && topSelectedItem->getDoodadBrush()->visibleInPalette()) {
					Append(MAP_POPUP_MENU_SELECT_DOODAD_BRUSH, "Select Doodadbrush", "Use this doodad brush");
				}

				if (topSelectedItem && topSelectedItem->isBrushDoor() && topSelectedItem->getDoorBrush()) {
					Append(MAP_POPUP_MENU_SELECT_DOOR_BRUSH, "Select Doorbrush", "Use this door brush");
				}

				if (tile->hasGround() && tile->getGroundBrush() && tile->getGroundBrush()->visibleInPalette()) {
					Append(MAP_POPUP_MENU_SELECT_GROUND_BRUSH, "Select Groundbrush", "Uses the current item as a groundbrush");
					Append(MAP_POPUP_MENU_REPLACE_GROUND, "Replace &Ground...", "Replace all connected ground with another ground brush");
				}


				if (tile->isHouseTile()) {
					Append(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, "Select House", "Draw with the house on this tile.");
				}

				AppendSeparator();
				Append(MAP_POPUP_MENU_PROPERTIES, "&Properties", "Properties for the current object");
			} else {

				if (topCreature) {
					Append(MAP_POPUP_MENU_SELECT_CREATURE_BRUSH, "Select Creature", "Uses the current creature as a creature brush");
				}

				if (topSpawn) {
					Append(MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, "Select Spawn", "Select the spawn brush");
				}

				Append(MAP_POPUP_MENU_SELECT_RAW_BRUSH, "Select RAW", "Uses the top item as a RAW brush");
				if (hasWall) {
					Append(MAP_POPUP_MENU_SELECT_WALL_BRUSH, "Select Wallbrush", "Uses the current item as a wallbrush");
					Append(MAP_POPUP_MENU_REPLACE_WALL, "Replace &Wall...", "Replace all connected walls, doors, and windows with another wall brush");
				}
				if (tile->hasGround() && tile->getGroundBrush() && tile->getGroundBrush()->visibleInPalette()) {
					Append(MAP_POPUP_MENU_SELECT_GROUND_BRUSH, "Select Groundbrush", "Uses the current tile as a groundbrush");
					Append(MAP_POPUP_MENU_REPLACE_GROUND, "Replace &Ground...", "Replace all connected ground with another ground brush");
				}


				if (tile->isHouseTile()) {
					Append(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, "Select House", "Draw with the house on this tile.");
				}

				if (tile->hasGround() || topCreature || topSpawn) {
					AppendSeparator();
					Append(MAP_POPUP_MENU_PROPERTIES, "&Properties", "Properties for the current object");
				}
			}

			AppendSeparator();

			wxMenuItem* browseTile = Append(MAP_POPUP_MENU_BROWSE_TILE, "Browse Field", "Navigate from tile items");
			browseTile->Enable(anything_selected);
		}
	}
}

void MapCanvas::getTilesToDraw(int mouse_map_x, int mouse_map_y, int floor, PositionVector* tilestodraw, PositionVector* tilestoborder, bool fill /*= false*/) {
	if (fill) {
		Brush* brush = g_gui.GetCurrentBrush();
		if (!brush || !brush->isGround()) {
			return;
		}

		GroundBrush* newBrush = brush->asGround();
		Position position(mouse_map_x, mouse_map_y, floor);

		Tile* tile = editor.map.getTile(position);
		GroundBrush* oldBrush = nullptr;
		if (tile) {
			oldBrush = tile->getGroundBrush();
		}

		if (oldBrush && oldBrush->getID() == newBrush->getID()) {
			return;
		}

		if ((tile && tile->ground && !oldBrush) || (!tile && oldBrush)) {
			return;
		}

		if (tile && oldBrush) {
			GroundBrush* groundBrush = tile->getGroundBrush();
			if (!groundBrush || groundBrush->getID() != oldBrush->getID()) {
				return;
			}
		}

		std::fill(std::begin(processed), std::end(processed), false);
		floodFill(&editor.map, position, BLOCK_SIZE / 2, BLOCK_SIZE / 2, oldBrush, tilestodraw);

	} else {
		const int brushSize = g_gui.GetBrushSize();
		const BrushShape brushShape = g_gui.GetBrushShape();
		const int squareMinOffset = g_gui.GetSquareBrushMinOffset();
		const int squareMaxOffset = g_gui.GetSquareBrushMaxOffset();
		const int minOffset = g_gui.GetSquareBrushOuterMinOffset();
		const int maxOffset = g_gui.GetSquareBrushOuterMaxOffset();
		const double radius = static_cast<double>(brushSize);
		const double fillRadiusSquared = (radius + 0.005) * (radius + 0.005);
		const double borderInnerRadius = std::max(0.0, radius - 1.5);
		const double borderInnerRadiusSquared = borderInnerRadius * borderInnerRadius;
		const double borderOuterRadius = radius + 1.5;
		const double borderOuterRadiusSquared = borderOuterRadius * borderOuterRadius;
		const size_t span = static_cast<size_t>(maxOffset - minOffset + 1);
		if (tilestodraw) {
			tilestodraw->reserve(tilestodraw->size() + span * span);
		}
		if (tilestoborder) {
			tilestoborder->reserve(tilestoborder->size() + span * span);
		}

		for (int y = minOffset; y <= maxOffset; y++) {
			for (int x = minOffset; x <= maxOffset; x++) {
				if (brushShape == BRUSHSHAPE_SQUARE) {
					if (x >= squareMinOffset && x <= squareMaxOffset && y >= squareMinOffset && y <= squareMaxOffset) {
						if (tilestodraw) {
							tilestodraw->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
					if (x >= minOffset && x <= maxOffset && y >= minOffset && y <= maxOffset) {
						if (tilestoborder) {
							tilestoborder->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
				} else if (brushShape == BRUSHSHAPE_CIRCLE) {
					double distanceSquared = double(x * x + y * y);
					if (distanceSquared < fillRadiusSquared) {
						if (tilestodraw) {
							tilestodraw->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
					if (distanceSquared >= borderInnerRadiusSquared && distanceSquared <= borderOuterRadiusSquared) {
						if (tilestoborder) {
							tilestoborder->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
					}
				}
			}
		}
	}
}

void MapCanvas::getLineTilesToDraw(int start_x, int start_y, int end_x, int end_y, int floor, PositionVector& tilestodraw, PositionVector* tilestoborder) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush || brush->oneSizeFitsAll()) {
		// Brush size means nothing to these brushes (creatures, doodads, spawns,
		// house exits), so the line stays one tile wide.
		GetLineTiles(start_x, start_y, end_x, end_y, floor, tilestodraw, tilestoborder);
		return;
	}

	PositionVector centers;
	GetLineTiles(start_x, start_y, end_x, end_y, floor, centers);

	// Stamp the brush — current size and square/circle shape — at every tile
	// along the line, exactly as a single click would at each of them.
	for (const Position& center : centers) {
		getTilesToDraw(center.x, center.y, floor, &tilestodraw, tilestoborder);
	}

	// Consecutive stamps overlap heavily, and a duplicate would make
	// Editor::draw emit two changes for the same tile inside one action.
	dedupePositions(tilestodraw);
	if (tilestoborder) {
		dedupePositions(*tilestoborder);
	}
}

bool MapCanvas::floodFill(Map* map, const Position& center, int x, int y, GroundBrush* brush, PositionVector* positions) {
	countMaxFills++;
	if (countMaxFills > (BLOCK_SIZE * 4 * 4)) {
		countMaxFills = 0;
		return true;
	}

	if (x <= 0 || y <= 0 || x >= BLOCK_SIZE || y >= BLOCK_SIZE) {
		return false;
	}

	processed[getFillIndex(x, y)] = true;

	int px = (center.x + x) - (BLOCK_SIZE / 2);
	int py = (center.y + y) - (BLOCK_SIZE / 2);
	if (px <= 0 || py <= 0 || px >= map->getWidth() || py >= map->getHeight()) {
		return false;
	}

	Tile* tile = map->getTile(px, py, center.z);
	if ((tile && tile->ground && !brush) || (!tile && brush)) {
		return false;
	}

	if (tile && brush) {
		GroundBrush* groundBrush = tile->getGroundBrush();
		if (!groundBrush || groundBrush->getID() != brush->getID()) {
			return false;
		}
	}

	positions->push_back(Position(px, py, center.z));

	bool deny = false;
	if (!processed[getFillIndex(x - 1, y)]) {
		deny = floodFill(map, center, x - 1, y, brush, positions);
	}

	if (!deny && !processed[getFillIndex(x, y - 1)]) {
		deny = floodFill(map, center, x, y - 1, brush, positions);
	}

	if (!deny && !processed[getFillIndex(x + 1, y)]) {
		deny = floodFill(map, center, x + 1, y, brush, positions);
	}

	if (!deny && !processed[getFillIndex(x, y + 1)]) {
		deny = floodFill(map, center, x, y + 1, brush, positions);
	}

	return deny;
}

// ============================================================================
// AnimationTimer

AnimationTimer::AnimationTimer(MapCanvas* canvas) : wxTimer(),
													map_canvas(canvas),
													started(false) {
														////
													};

AnimationTimer::~AnimationTimer() {
	////
};

void AnimationTimer::Notify() {
	if (map_canvas->GetZoom() <= 2.0) {
		map_canvas->Refresh();
	}
};

void AnimationTimer::Start() {
	if (!started) {
		started = true;
		wxTimer::Start(100);
	}
};

void AnimationTimer::Stop() {
	if (started) {
		started = false;
		wxTimer::Stop();
	}
};

void MapCanvas::OnCreateGenerateScript(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() == 1) {
		Tile* tile = editor.selection.getSelectedTile();
		if (tile) {
			Item* item = tile->getTopSelectedItem();
			if (item) {
				Position pos = tile->getPosition();
				std::ostringstream out;
				out << "generate(" << item->getID() << ", Position(" << pos.x << ", " << pos.y << ", " << pos.z << "))\n";
				out << "lever()";

				if (wxTheClipboard->Open()) {
					wxTextDataObject* obj = new wxTextDataObject();
					obj->SetText(wxstr(out.str()));
					wxTheClipboard->SetData(obj);
					wxTheClipboard->Close();
				}
			}
		}
	}
}

void MapCanvas::OnCreateRemoveScript(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() == 1) {
		Tile* tile = editor.selection.getSelectedTile();
		if (tile) {
			Item* item = tile->getTopSelectedItem();
			if (item) {
				Position pos = tile->getPosition();
				std::ostringstream out;
				out << "remove(" << item->getID() << ", Position(" << pos.x << ", " << pos.y << ", " << pos.z << "))\n";
				out << "lever()";

				if (wxTheClipboard->Open()) {
					wxTextDataObject* obj = new wxTextDataObject();
					obj->SetText(wxstr(out.str()));
					wxTheClipboard->SetData(obj);
					wxTheClipboard->Close();
				}
			}
		}
	}
}

void MapCanvas::OnCreateCreateScript(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() == 1) {
		Tile* tile = editor.selection.getSelectedTile();
		if (tile) {
			Item* item = tile->getTopSelectedItem();
			if (item) {
				Position pos = tile->getPosition();
				std::ostringstream out;
				out << "create(" << item->getID() << ", Position(" << pos.x << ", " << pos.y << ", " << pos.z << "))\n";
				out << "lever()";

				if (wxTheClipboard->Open()) {
					wxTextDataObject* obj = new wxTextDataObject();
					obj->SetText(wxstr(out.str()));
					wxTheClipboard->SetData(obj);
					wxTheClipboard->Close();
				}
			}
		}
	}
}

void MapCanvas::OnCreateCheckScript(wxCommandEvent& WXUNUSED(event)) {
	if (editor.selection.size() == 1) {
		Tile* tile = editor.selection.getSelectedTile();
		if (tile) {
			Item* item = tile->getTopSelectedItem();
			if (item) {
				Position pos = tile->getPosition();
				std::ostringstream out;
				out << "checkitem(" << item->getID() << ", Position(" << pos.x << ", " << pos.y << ", " << pos.z << "))";

				if (wxTheClipboard->Open()) {
					wxTextDataObject* obj = new wxTextDataObject();
					obj->SetText(wxstr(out.str()));
					wxTheClipboard->SetData(obj);
					wxTheClipboard->Close();
				}
			}
		}
	}
}
