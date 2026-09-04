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

#ifndef RME_PALETTE_BRUSHLIST_
#define RME_PALETTE_BRUSHLIST_

#include "main.h"
#include "palette_common.h"

enum BrushListType {
	BRUSHLIST_LARGE_ICONS,
	BRUSHLIST_SMALL_ICONS,
	BRUSHLIST_LISTBOX,
	BRUSHLIST_TEXT_LISTBOX,
	BRUSHLIST_ACTUAL_SIZE_ICONS,
};

class BrushBoxInterface {
public:
	BrushBoxInterface(const TilesetCategory* _tileset) : tileset(_tileset), loaded(false) {
		ASSERT(tileset);
	}
	virtual ~BrushBoxInterface() { }

	virtual wxWindow* GetSelfWindow() = 0;

	// Select the first brush
	virtual void SelectFirstBrush() = 0;
	// Returns the currently selected brush (First brush if panel is not loaded)
	virtual Brush* GetSelectedBrush() const = 0;
	// Select the brush in the parameter, this only changes the look of the panel
	virtual bool SelectBrush(const Brush* brush) = 0;

	void SetPickerMode(bool picker) {
		picker_mode = picker;
	}

protected:
	const TilesetCategory* const tileset;
	bool loaded;
	bool picker_mode = false;
};

class BrushListBox : public wxVListBox, public BrushBoxInterface {
public:
	BrushListBox(wxWindow* parent, const TilesetCategory* _tileset);
	~BrushListBox();

	wxWindow* GetSelfWindow() {
		return this;
	}

	// Select the first brush
	void SelectFirstBrush();
	// Returns the currently selected brush (First brush if panel is not loaded)
	Brush* GetSelectedBrush() const;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* brush);
	void SelectAll(); // Override to prevent assertion for single-selection listbox

	// Event handlers
	virtual void OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const;
	virtual wxCoord OnMeasureItem(size_t n) const;

	void OnKey(wxKeyEvent& event);
	void OnChar(wxKeyEvent& event); // Intercept Ctrl+A to prevent SelectAll assertion
	void OnMouseMotion(wxMouseEvent& event);
	void OnMouseRightClick(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnAnimationTimer(wxTimerEvent& event);

protected:
	// Redraws the list while animated sprites are visible so they cycle
	// through their frames like on the map canvas. OnDrawItem flags whether an
	// animated sprite was painted; the timer stops itself once none are.
	mutable wxTimer animation_timer; // started from the const OnDrawItem
	mutable bool animated_sprite_visible = false;

	DECLARE_EVENT_TABLE();
};

// A virtualized icon grid. Instead of creating one native widget per brush
// (which freezes the editor for tilesets with thousands of items, e.g. the
// RAW "Others" category), it custom-draws only the icons currently visible
// inside a single scrolled window - the same strategy the listbox uses.
class BrushIconBox : public wxScrolledWindow, public BrushBoxInterface {
public:
	BrushIconBox(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz, bool useActualSize = false);
	~BrushIconBox();

	wxWindow* GetSelfWindow() {
		return this;
	}

	// Scrolls the window so the brush at the given cell index is visible
	void EnsureVisible(size_t n);

	// Select the first brush
	void SelectFirstBrush();
	// Returns the currently selected brush (First brush if panel is not loaded)
	Brush* GetSelectedBrush() const;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* brush);

	// Event handling...
	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnMouseRightClick(wxMouseEvent& event);
	void OnMouseMotion(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnKey(wxKeyEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnAnimationTimer(wxTimerEvent& event);

protected:
	struct Cell {
		Brush* brush; // nullptr means this cell is a separator line
		wxRect rect; // position in unscrolled (virtual) coordinates
	};

	// (Re)computes cell positions based on the current client width.
	void RecalculateGrid();
	// Returns the index of the cell at the given unscrolled point, or -1.
	int CellIndexAt(const wxPoint& unscrolled) const;
	void DrawCell(wxDC& dc, const Cell& cell, bool selected) const;
	void HandleBrushSelection(Brush* brush);

	std::vector<Cell> cells;
	RenderSize icon_size;
	bool use_actual_size;
	static constexpr int CELL_MARGIN = 2; // gap kept around every sprite so it is never clipped
	int base_icon_px; // unmagnified sprite size of one slot (16 or 32)
	int zoom; // integer magnification the layout was computed for (Config::PALETTE_ZOOM)
	int slot_size; // pixel size of one grid cell (base_icon_px * zoom + 2 * CELL_MARGIN)
	int columns; // number of columns the layout was computed for
	int virtual_height; // total height of the laid-out grid
	int selected_index; // index into cells of the selected brush, or -1
	std::set<int> multi_selected; // additional cells toggled via Shift+click

	// Redraws the grid while animated sprites are on screen so they cycle
	// through their frames like on the map canvas. DrawCell flags whether an
	// animated sprite was painted; the timer stops itself once none are.
	wxTimer animation_timer;
	mutable bool animated_sprite_visible = false;

	DECLARE_EVENT_TABLE();
};

// A panel capapable of displaying a collection of brushes
// Brushes can be arranged in either list or icon fashion
// Contents are *not* created when the panel is created,
// but on the first call to LoadContents(), this is to
// allow procedural loading (faster)

class BrushPanel : public wxPanel {
public:
	BrushPanel(wxWindow* parent);
	~BrushPanel();

	// Interface
	// Flushes this panel and consequent views will feature reloaded data
	void InvalidateContents();
	// Loads the content (This must be called before the panel is displayed, else it will appear empty
	void LoadContents();

	// Sets the display type (list or icons)
	void SetListType(BrushListType ltype);
	void SetListType(wxString ltype);
	// Assigns a tileset to this list
	void AssignTileset(const TilesetCategory* tileset);

	// Select the first brush
	void SelectFirstBrush();
	// Returns the currently selected brush (First brush if panel is not loaded)
	Brush* GetSelectedBrush() const;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* whatbrush);
	void SetPickerMode(bool picker);

	// Called when the window is about to be displayed
	void OnSwitchIn();
	// Called when this page is hidden
	void OnSwitchOut();

	// wxWidgets event handlers
	void OnClickListBoxRow(wxCommandEvent& event);

protected:
	const TilesetCategory* tileset;
	wxSizer* sizer;
	BrushBoxInterface* brushbox;
	bool loaded;
	bool picker_mode = false;
	BrushListType list_type;

	DECLARE_EVENT_TABLE();
};

class BrushPalettePanel : public PalettePanel {
public:
	BrushPalettePanel(wxWindow* parent, const TilesetContainer& tilesets, TilesetCategoryType category, wxWindowID id = wxID_ANY);
	~BrushPalettePanel();

	// Interface
	// Flushes this panel and consequent views will feature reloaded data
	void InvalidateContents();
	// Loads the currently displayed page
	void LoadCurrentContents();
	// Loads all content in this panel
	void LoadAllContents();

	PaletteType GetType() const;

	// Sets the display type (list or icons)
	void SetListType(BrushListType ltype);
	void SetListType(wxString ltype);

	// Select the first brush
	void SelectFirstBrush();
	// Returns the currently selected brush (first brush if panel is not loaded)
	Brush* GetSelectedBrush() const;
	// Select the brush in the parameter, this only changes the look of the panel
	bool SelectBrush(const Brush* whatbrush);

	wxChoicebook* GetChoicebook() const {
		return choicebook;
	}

	// Returns true if this palette has a tileset page with the given name.
	bool HasTileset(const std::string& tilesetName) const;
	// Selects the tileset page with the given name. Returns false if absent.
	bool SelectTileset(const std::string& tilesetName);

	// Refreshes the page belonging to the given tileset so a freshly added item
	// shows up, without rebuilding the whole palette. Only the currently visible
	// page is reloaded immediately; others are flagged to reload when next shown.
	// Returns true if a page for the tileset exists in this panel.
	bool RefreshTilesetPage(const std::string& tilesetName);

	// Called when this page is displayed
	void OnSwitchIn();

	// Event handler for child window
	void OnSwitchingPage(wxChoicebookEvent& event);
	void OnPageChanged(wxChoicebookEvent& event);
	void OnClickAddTileset(wxCommandEvent& WXUNUSED(event));
	void OnClickAddItemToTileset(wxCommandEvent& WXUNUSED(event));

protected:
	PaletteType palette_type;
	wxChoicebook* choicebook;
	BrushSizePanel* size_panel;
	std::map<wxWindow*, Brush*> remembered_brushes;

	DECLARE_EVENT_TABLE();
};

#endif
