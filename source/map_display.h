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

#ifndef RME_DISPLAY_WINDOW_H_
#define RME_DISPLAY_WINDOW_H_

#include "action.h"
#include "tile.h"
#include "creature.h"

#include <vector>

class ObjectPropertiesWindowBase;
class Item;
class Creature;
class MapWindow;
class MapPopupMenu;
class AnimationTimer;
class MapDrawer;

/**
 * Computes the tiles covered by a one-tile-wide straight line between two map
 * coordinates, using Bresenham's algorithm, so arbitrary angles (not just
 * axis-aligned ones) come out as an unbroken run of tiles.
 *
 * @param start_x X coordinate the line starts at.
 * @param start_y Y coordinate the line starts at.
 * @param end_x X coordinate the line ends at (inclusive).
 * @param end_y Y coordinate the line ends at (inclusive).
 * @param z Floor all produced positions live on.
 * @param tilestodraw Receives the line tiles, appended in order from start to end.
 * @param tilestoborder When non-null, receives the line tiles plus their
 * 8-neighbourhood, de-duplicated (Editor::draw must never see the same border
 * tile twice).
 */
void GetLineTiles(int start_x, int start_y, int end_x, int end_y, int z, PositionVector& tilestodraw, PositionVector* tilestoborder = nullptr);

/**
 * Resolves a freehand lasso trace into the set of tile columns it encloses.
 *
 * Consecutive trace samples are bridged with straight lines and the loop is
 * closed from the last sample back to the first, giving a watertight
 * 8-connected boundary. Everything a 4-connected flood fill cannot reach from
 * outside that boundary counts as enclosed, so a self-intersecting trace — easy
 * to produce by accident when drawing quickly — behaves sensibly instead of
 * punching even-odd holes. The traced tiles themselves are always included.
 *
 * @param trace Tile positions the cursor passed through, in order. All are
 * expected to be on the same floor; the first one's z is used for the output.
 * @param tiles Receives the enclosed tiles, on the trace's floor. The caller
 * decides which floors to actually apply them to.
 * @return False if the trace's bounding box is too large to resolve, in which
 * case tiles is left untouched.
 */
bool GetLassoTiles(const PositionVector& trace, PositionVector& tiles);

class MapCanvas : public wxGLCanvas {
public:
	MapCanvas(MapWindow* parent, Editor& editor, int* attriblist);
	virtual ~MapCanvas();
	void Reset();

	// All events
	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent& event) { }

	void OnMouseMove(wxMouseEvent& event);
	void OnMouseLeftRelease(wxMouseEvent& event);
	void OnMouseLeftClick(wxMouseEvent& event);
	void OnMouseLeftDoubleClick(wxMouseEvent& event);
	void OnMouseCenterClick(wxMouseEvent& event);
	void OnMouseCenterRelease(wxMouseEvent& event);
	void OnMouseRightClick(wxMouseEvent& event);
	void OnMouseRightRelease(wxMouseEvent& event);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);
	void OnWheel(wxMouseEvent& event);
	void OnGainMouse(wxMouseEvent& event);
	void OnLoseMouse(wxMouseEvent& event);

	// Mouse events handlers (called by the above)
	void OnMouseActionRelease(wxMouseEvent& event);
	void OnMouseActionClick(wxMouseEvent& event);
	void OnMouseCameraClick(wxMouseEvent& event);
	void OnMouseCameraRelease(wxMouseEvent& event);
	void OnMousePropertiesClick(wxMouseEvent& event);
	void OnMousePropertiesRelease(wxMouseEvent& event);

	//
	void OnCut(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);
	void OnCopyPosition(wxCommandEvent& event);
	void OnCopyRaidArea(wxCommandEvent& event);
	void OnExportSpritesheet(wxCommandEvent& event);
	void OnCopyServerId(wxCommandEvent& event);
	void OnCopyClientId(wxCommandEvent& event);
	void OnCopyName(wxCommandEvent& event);
	void OnBrowseTile(wxCommandEvent& event);
	void OnPaste(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void ApplyTransform(MapTransform transform);
	void OnRotateSelectionClockwise(wxCommandEvent& event);
	void OnRotateSelectionCounterClockwise(wxCommandEvent& event);
	void OnFlipSelectionHorizontal(wxCommandEvent& event);
	void OnFlipSelectionVertical(wxCommandEvent& event);
	void OnAddComment(wxCommandEvent& event);
	void OnRemoveComment(wxCommandEvent& event);
	void OnPingHere(wxCommandEvent& event);
	void OnCreateGenerateScript(wxCommandEvent& event);
	void OnCreateRemoveScript(wxCommandEvent& event);
	void OnCreateCreateScript(wxCommandEvent& event);
	void OnCreateCheckScript(wxCommandEvent& event);
	// ----
	void OnGotoDestination(wxCommandEvent& event);
	void OnRotateItem(wxCommandEvent& event);
	void OnSwitchDoor(wxCommandEvent& event);
	// ----
	void OnSelectRAWBrush(wxCommandEvent& event);
	void OnSelectGroundBrush(wxCommandEvent& event);
	void OnSelectDoodadBrush(wxCommandEvent& event);
	void OnSelectDoorBrush(wxCommandEvent& event);
	void OnSelectWallBrush(wxCommandEvent& event);
	void OnSelectCarpetBrush(wxCommandEvent& event);
	void OnSelectTableBrush(wxCommandEvent& event);
	void OnSelectCreatureBrush(wxCommandEvent& event);
	void OnSelectSpawnBrush(wxCommandEvent& event);
	void OnSelectHouseBrush(wxCommandEvent& event);
	void OnSelectMoveTo(wxCommandEvent& event);
	void OnCreateWall(wxCommandEvent& event);
	void OnReplaceWall(wxCommandEvent& event);
	void OnReplaceGround(wxCommandEvent& event);
	void OnReplaceWithSearchItem(wxCommandEvent& event);
	// ---
	void OnProperties(wxCommandEvent& event);

	void Refresh();
	void CloseModelessProperties();

	void ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y);
	void MouseToMap(int* map_x, int* map_y) {
		ScreenToMap(cursor_x, cursor_y, map_x, map_y);
	}
	void GetScreenCenter(int* map_x, int* map_y);

	void StartPasting();
	void EndPasting();
	void EnterSelectionMode();
	void EnterDrawingMode();

	void UpdatePositionStatus(int x = -1, int y = -1);
	void UpdateZoomStatus();

	void ChangeFloor(int new_floor);
	int GetFloor() const {
		return floor;
	}
	double GetZoom() const {
		return zoom;
	}
	void SetZoom(double value);
	void GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const;

	Position GetCursorPosition() const;

	void TakeScreenshot(wxFileName path, wxString format);

protected:
	void getTilesToDraw(int mouse_map_x, int mouse_map_y, int floor, PositionVector* tilestodraw, PositionVector* tilestoborder, bool fill = false);
	void getLineTilesToDraw(int start_x, int start_y, int end_x, int end_y, int floor, PositionVector& tilestodraw, PositionVector* tilestoborder = nullptr);
	bool floodFill(Map* map, const Position& center, int x, int y, GroundBrush* brush, PositionVector* positions);
	void ShowObjectProperties(ObjectPropertiesWindowBase* window, Tile* edited_tile, Item* protected_item);

private:
	enum {
		BLOCK_SIZE = 100
	};

	inline int getFillIndex(int x, int y) const {
		return x + BLOCK_SIZE * y;
	}

	static bool processed[BLOCK_SIZE * BLOCK_SIZE];

	Editor& editor;
	MapDrawer* drawer;
	int keyCode;
	int countMaxFills = 0;

	// View related
	int floor;
	double zoom;
	int cursor_x;
	int cursor_y;

	void AppendLassoPoint(int map_x, int map_y);
	void ApplyLassoSelection(bool deselect);

	bool dragging;
	bool boundbox_selection;
	bool boundbox_deselection;
	// A lasso trace is in progress: boundbox_selection is also set, so the drag
	// bookkeeping is shared, but the traced path replaces the rectangle.
	bool lasso_selection;
	PositionVector lasso_trace;
	bool screendragging;
	bool isPasting() const;
	bool drawing;
	bool dragging_draw;
	// Alt was held during the current drag-draw: draw a straight line from the
	// click position to the cursor instead of the brush shape's square/circle.
	bool dragging_draw_line;
	bool replace_dragging;

	uint8_t* screenshot_buffer;

	int drag_start_x;
	int drag_start_y;
	int drag_start_z;

	int last_cursor_map_x;
	int last_cursor_map_y;
	int last_cursor_map_z;

	int last_click_map_x;
	int last_click_map_y;
	int last_click_map_z;
	int last_click_abs_x;
	int last_click_abs_y;
	int last_click_x;
	int last_click_y;

	int last_mmb_click_x;
	int last_mmb_click_y;

	int view_scroll_x;
	int view_scroll_y;

	uint32_t current_house_id;
	uint32_t options_revision;

	wxStopWatch refresh_watch;
	MapPopupMenu* popup_menu;
	AnimationTimer* animation_timer;
	std::vector<wxWeakRef<ObjectPropertiesWindowBase>> modeless_property_windows;

	friend class MapDrawer;

	DECLARE_EVENT_TABLE()
};

// Right-click popup menu
class MapPopupMenu : public wxMenu {
public:
	MapPopupMenu(Editor& editor);
	virtual ~MapPopupMenu();

	void Update(const Position& cursorTile);

protected:
	Editor& editor;
};

class AnimationTimer : public wxTimer {
public:
	AnimationTimer(MapCanvas* canvas);
	~AnimationTimer();

	void Notify();
	void Start();
	void Stop();

private:
	MapCanvas* map_canvas;
	bool started;
};

#endif
