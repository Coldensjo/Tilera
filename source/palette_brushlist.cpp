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

#include "palette_brushlist.h"
#include "gui.h"
#include "edit_brush_window.h"
#include "edit_tileset_window.h"
#include "brush.h"
#include "brush_edit.h"
#include "add_tileset_window.h"
#include "add_item_window.h"
#include "add_to_tileset_window.h"
#include "raw_brush.h"
#include "materials.h"
#include "graphics.h"
#include "sprites.h"
#include "settings.h"
#include "theme.h"
#include "map_tab.h"

#include <algorithm>
#include <limits>
#include <wx/gbsizer.h>
#include <wx/statline.h>

// ============================================================================
// Brush Palette Panel
// A common class for terrain/doodad/item/raw palette

namespace {
void ForwardPaletteKeyToMap(wxKeyEvent& event) {
	if (g_settings.getInteger(Config::LISTBOX_EATS_ALL_EVENTS)) {
		switch (event.GetKeyCode()) {
			case WXK_UP:
			case WXK_DOWN:
			case WXK_LEFT:
			case WXK_RIGHT:
			case WXK_PAGEUP:
			case WXK_PAGEDOWN:
			case WXK_HOME:
			case WXK_END:
				event.Skip();
				return;
		}
	}

	if (event.ControlDown() || event.AltDown()) {
		event.Skip();
		return;
	}

	g_gui.AddPendingCanvasEvent(event);
}

void FocusMapCanvasFromPalette() {
	if (MapTab* tab = g_gui.GetCurrentMapTab()) {
		if (MapCanvas* canvas = tab->GetCanvas()) {
			canvas->SetFocus();
		}
	}
}

void OpenTilesetEditorForPage(BrushPalettePanel* panel, int pageIndex) {
	if (!panel || pageIndex < 0) {
		return;
	}

	wxChoicebook* choicebook = panel->GetChoicebook();
	if (!choicebook || pageIndex >= static_cast<int>(choicebook->GetPageCount())) {
		return;
	}

	const std::string tilesetName = choicebook->GetPageText(pageIndex).ToStdString();
	auto tilesetIter = g_materials.tilesets.find(tilesetName);
	if (tilesetIter == g_materials.tilesets.end()) {
		return;
	}

	OpenTilesetEditor(tilesetIter->second, panel->GetType());
}

Brush* FirstSelectableBrush(const TilesetCategory* category) {
	if (!category) {
		return nullptr;
	}
	for (Brush* brush : category->brushlist) {
		if (brush && !brush->isPaletteSeparator()) {
			return brush;
		}
	}
	return nullptr;
}

// Ctrl+right-click on one or more RAW items opens the "Add to Tileset" dialog.
void TryOpenAddToTileset(std::vector<uint16_t> ids, TilesetCategoryType currentType) {
	if (ids.empty()) {
		return;
	}

	AddToTilesetWindow* w = newd AddToTilesetWindow(g_gui.root, std::move(ids), currentType);
	int ret = w->ShowModal();
	const std::string changedTileset = w->GetResultTileset();
	const TilesetCategoryType changedCategory = w->GetResultCategory();
	w->Destroy();

	if (ret != 0) {
		g_gui.RefreshTilesetAddition(changedCategory, changedTileset);
	}
}
} // namespace

BEGIN_EVENT_TABLE(BrushPalettePanel, PalettePanel)
EVT_BUTTON(wxID_ADD, BrushPalettePanel::OnClickAddItemToTileset)
EVT_BUTTON(wxID_NEW, BrushPalettePanel::OnClickAddTileset)
EVT_CHOICEBOOK_PAGE_CHANGING(wxID_ANY, BrushPalettePanel::OnSwitchingPage)
EVT_CHOICEBOOK_PAGE_CHANGED(wxID_ANY, BrushPalettePanel::OnPageChanged)
END_EVENT_TABLE()

BrushPalettePanel::BrushPalettePanel(wxWindow* parent, const TilesetContainer& tilesets, TilesetCategoryType category, wxWindowID id) :
	PalettePanel(parent, id),
	palette_type(category),
	choicebook(nullptr),
	size_panel(nullptr) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Create the tileset panel
	wxSizer* ts_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Tileset");
	wxChoicebook* tmp_choicebook = newd wxChoicebook(this, wxID_ANY, wxDefaultPosition, wxSize(180, 250), wxCLIP_CHILDREN);
	ts_sizer->Add(tmp_choicebook, 1, wxEXPAND);
	topsizer->Add(ts_sizer, 1, wxEXPAND);

	if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
		wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
		wxButton* buttonAddTileset = newd wxButton(this, wxID_NEW, "Add new Tileset");
		tmpsizer->Add(buttonAddTileset, wxSizerFlags(0).Center());

		wxButton* buttonAddItemToTileset = newd wxButton(this, wxID_ADD, "Add new Item");
		tmpsizer->Add(buttonAddItemToTileset, wxSizerFlags(0).Center());

		topsizer->Add(tmpsizer, 0, wxCENTER, 10);
	}

	for (TilesetContainer::const_iterator iter = tilesets.begin(); iter != tilesets.end(); ++iter) {
		const TilesetCategory* tcg = iter->second->getCategory(category);
		if (tcg && tcg->size() > 0) {
			BrushPanel* panel = newd BrushPanel(tmp_choicebook);
			panel->AssignTileset(tcg);
			tmp_choicebook->AddPage(panel, wxstr(iter->second->name));
		}
	}

	SetSizerAndFit(topsizer);

	choicebook = tmp_choicebook;

	if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
		wxChoice* choice = choicebook->GetChoiceCtrl();
		if (choice) {
			choice->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
				if (event.ControlDown()) {
					OpenTilesetEditorForPage(this, choicebook->GetSelection());
					return;
				}
				event.Skip();
			});
		}
	}
}

BrushPalettePanel::~BrushPalettePanel() {
	////
}

void BrushPalettePanel::InvalidateContents() {
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->InvalidateContents();
	}
	PalettePanel::InvalidateContents();
}

void BrushPalettePanel::LoadCurrentContents() {
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->OnSwitchIn();
	}
	PalettePanel::LoadCurrentContents();
}

void BrushPalettePanel::LoadAllContents() {
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->LoadContents();
	}
	PalettePanel::LoadAllContents();
}

PaletteType BrushPalettePanel::GetType() const {
	return palette_type;
}

bool BrushPalettePanel::HasTileset(const std::string& tilesetName) const {
	if (!choicebook) {
		return false;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		if (choicebook->GetPageText(iz).ToStdString() == tilesetName) {
			return true;
		}
	}
	return false;
}

bool BrushPalettePanel::SelectTileset(const std::string& tilesetName) {
	if (!choicebook) {
		return false;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		if (choicebook->GetPageText(iz).ToStdString() == tilesetName) {
			choicebook->SetSelection(iz);
			return true;
		}
	}
	return false;
}

void BrushPalettePanel::SetListType(BrushListType ltype) {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->SetListType(ltype);
	}
}

void BrushPalettePanel::SetListType(wxString ltype) {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->SetListType(ltype);
	}
}

Brush* BrushPalettePanel::GetSelectedBrush() const {
	if (!choicebook) {
		return nullptr;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	Brush* res = nullptr;
	if (panel) {
		for (ToolBarList::const_iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			res = iter->panel->GetSelectedBrush();
			if (res) {
				return res;
			}
		}
		res = panel->GetSelectedBrush();
	}
	return res;
}

void BrushPalettePanel::SelectFirstBrush() {
	if (!choicebook) {
		return;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	panel->SelectFirstBrush();
}

bool BrushPalettePanel::SelectBrush(const Brush* whatbrush) {
	if (!choicebook) {
		return false;
	}

	BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (!panel) {
		return false;
	}

	for (const ToolBarEntry& toolBar : tool_bars) {
		if (toolBar.panel->SelectBrush(whatbrush)) {
			panel->SelectBrush(nullptr);
			return true;
		}
	}

	if (panel->SelectBrush(whatbrush)) {
		for (const ToolBarEntry& toolBar : tool_bars) {
			toolBar.panel->SelectBrush(nullptr);
		}
		return true;
	}

	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		if ((int)iz == choicebook->GetSelection()) {
			continue;
		}

		panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel && panel->SelectBrush(whatbrush)) {
			choicebook->ChangeSelection(iz);
			for (const ToolBarEntry& toolBar : tool_bars) {
				toolBar.panel->SelectBrush(nullptr);
			}
			return true;
		}
	}
	return false;
}

void BrushPalettePanel::OnSwitchingPage(wxChoicebookEvent& event) {
	if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR) && wxGetKeyState(WXK_CONTROL)) {
		OpenTilesetEditorForPage(this, event.GetSelection());
		event.Veto();
		return;
	}

	event.Skip();
	if (!choicebook) {
		return;
	}
	BrushPanel* old_panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (old_panel) {
		old_panel->OnSwitchOut();
		for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			Brush* tmp = iter->panel->GetSelectedBrush();
			if (tmp) {
				remembered_brushes[old_panel] = tmp;
			}
		}
	}

	wxWindow* page = choicebook->GetPage(event.GetSelection());
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->OnSwitchIn();
		for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			iter->panel->SelectBrush(remembered_brushes[panel]);
		}
	}
}

void BrushPalettePanel::OnPageChanged(wxChoicebookEvent& event) {
	if (!choicebook) {
		return;
	}
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void BrushPalettePanel::OnSwitchIn() {
	LoadCurrentContents();
	g_gui.ActivatePalette(GetParentPalette());
	const int selection_value = last_brush_even ? GUI::BRUSH_SIZE_2X2 : last_brush_size;
	g_gui.SetBrushSizeInternal(selection_value);
	OnUpdateBrushSize(g_gui.GetBrushShape(), selection_value);
}

void BrushPalettePanel::OnClickAddTileset(wxCommandEvent& WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}

	wxDialog* w = newd AddTilesetWindow(g_gui.root, palette_type);
	int ret = w->ShowModal();
	w->Destroy();

	if (ret != 0) {
		g_gui.DestroyPalettes();
		g_gui.NewPalette();
	}
}

bool BrushPalettePanel::RefreshTilesetPage(const std::string& tilesetName) {
	if (!choicebook) {
		return false;
	}

	for (size_t i = 0; i < choicebook->GetPageCount(); ++i) {
		if (choicebook->GetPageText(i).ToStdString() != tilesetName) {
			continue;
		}

		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(i));
		if (panel) {
			panel->InvalidateContents();
			// Only rebuild the box for the page that's actually on screen; the
			// rest reload lazily on their next OnSwitchIn.
			if (static_cast<int>(i) == choicebook->GetSelection()) {
				panel->LoadContents();
			}
		}
		return true;
	}
	return false;
}

void BrushPalettePanel::OnClickAddItemToTileset(wxCommandEvent& WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}
	std::string tilesetName = choicebook->GetPageText(choicebook->GetSelection()).ToStdString();

	auto _it = g_materials.tilesets.find(tilesetName);
	if (_it != g_materials.tilesets.end()) {
		wxDialog* w = newd AddItemWindow(g_gui.root, palette_type, _it->second);
		int ret = w->ShowModal();
		w->Destroy();

		if (ret != 0) {
			g_gui.RebuildPalettes();
		}
	}
}

// ============================================================================
// Brush Panel
// A container of brush buttons

BEGIN_EVENT_TABLE(BrushPanel, wxPanel)
// Listbox style
EVT_LISTBOX(wxID_ANY, BrushPanel::OnClickListBoxRow)
END_EVENT_TABLE()

BrushPanel::BrushPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN),
	tileset(nullptr),
	brushbox(nullptr),
	loaded(false),
	list_type(BRUSHLIST_LISTBOX) {
	sizer = newd wxBoxSizer(wxVERTICAL);
	SetSizerAndFit(sizer);
}

void BrushPanel::SetPickerMode(bool picker) {
	picker_mode = picker;
	if (brushbox) {
		brushbox->SetPickerMode(picker);
	}
}

BrushPanel::~BrushPanel() {
	////
}

void BrushPanel::AssignTileset(const TilesetCategory* _tileset) {
	if (_tileset != tileset) {
		InvalidateContents();
		tileset = _tileset;
	}
}

void BrushPanel::SetListType(BrushListType ltype) {
	if (list_type != ltype) {
		InvalidateContents();
		list_type = ltype;
	}
}

void BrushPanel::SetListType(wxString ltype) {
	if (ltype == "small icons") {
		SetListType(BRUSHLIST_SMALL_ICONS);
	} else if (ltype == "large icons") {
		SetListType(BRUSHLIST_LARGE_ICONS);
	} else if (ltype == "listbox") {
		SetListType(BRUSHLIST_LISTBOX);
	} else if (ltype == "textlistbox") {
		SetListType(BRUSHLIST_TEXT_LISTBOX);
	} else if (ltype == "actual icons") {
		SetListType(BRUSHLIST_ACTUAL_SIZE_ICONS);
	}
}

void BrushPanel::InvalidateContents() {
	sizer->Clear(true);
	loaded = false;
	brushbox = nullptr;
}

void BrushPanel::LoadContents() {
	if (loaded) {
		return;
	}
	loaded = true;
	ASSERT(tileset != nullptr);
	
	// Freeze to prevent repaints during content loading
	Freeze();
	
	switch (list_type) {
		case BRUSHLIST_LARGE_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_32x32);
			break;
		case BRUSHLIST_SMALL_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_16x16);
			break;
		case BRUSHLIST_LISTBOX:
			brushbox = newd BrushListBox(this, tileset);
			break;
		case BRUSHLIST_ACTUAL_SIZE_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_32x32, true);
			break;
		default:
			break;
	}
	ASSERT(brushbox != nullptr);
	brushbox->SetPickerMode(picker_mode);
	sizer->Add(brushbox->GetSelfWindow(), 1, wxEXPAND);
	Fit();
	brushbox->SelectFirstBrush();
	
	// Thaw to allow repainting
	Thaw();
}

void BrushPanel::SelectFirstBrush() {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		brushbox->SelectFirstBrush();
	}
}

Brush* BrushPanel::GetSelectedBrush() const {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		return brushbox->GetSelectedBrush();
	}

	if (tileset && tileset->size() > 0) {
		return FirstSelectableBrush(tileset);
	}
	return nullptr;
}

bool BrushPanel::SelectBrush(const Brush* whatbrush) {
	if (loaded) {
		// std::cout << loaded << std::endl;
		// std::cout << brushbox << std::endl;
		ASSERT(brushbox != nullptr);
		return brushbox->SelectBrush(whatbrush);
	}

	for (BrushVector::const_iterator iter = tileset->brushlist.begin(); iter != tileset->brushlist.end(); ++iter) {
		if (*iter == whatbrush) {
			LoadContents();
			return brushbox->SelectBrush(whatbrush);
		}
	}
	return false;
}

void BrushPanel::OnSwitchIn() {
	LoadContents();
}

void BrushPanel::OnSwitchOut() {
	HidePaletteBrushHoverTooltip();
}

void BrushPanel::OnClickListBoxRow(wxCommandEvent& event) {
	if (!tileset || !brushbox) {
		return;
	}
	if (picker_mode) {
		return;
	}
	
	ASSERT(tileset->getType() >= TILESET_UNKNOWN && tileset->getType() <= TILESET_HOUSE);
	size_t n = event.GetSelection();

	if (n >= tileset->size()) {
		return;
	}

	Brush* brush = tileset->brushlist[n];
	if (!brush || brush->isPaletteSeparator()) {
		return;
	}
	if (wxGetKeyState(WXK_CONTROL)) {
		if (BrushCanBeEdited(brush)) {
			OpenBrushEditor(brush);
		} else if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
			OpenTilesetEditor(const_cast<Tileset*>(&tileset->tileset), tileset->getType());
		} else {
			OpenBrushEditor(brush);
		}
		return;
	}

	wxWindow* w = this;
	while ((w = w->GetParent()) && dynamic_cast<PaletteWindow*>(w) == nullptr)
		;

	if (w) {
		g_gui.ActivatePalette(static_cast<PaletteWindow*>(w));
	}

	FocusMapCanvasFromPalette();
	g_gui.SelectBrush(tileset->brushlist[n], tileset->getType());
}

// ============================================================================
// BrushIconBox
//
// Virtualized icon grid. Earlier versions instantiated one native BrushButton
// widget per brush, which froze the editor when a tileset held thousands of
// items (e.g. RAW -> "Others"). This version owns no child widgets: it lays out
// lightweight cells and custom-draws only the ones currently scrolled into view.

BEGIN_EVENT_TABLE(BrushIconBox, wxScrolledWindow)
EVT_PAINT(BrushIconBox::OnPaint)
EVT_SIZE(BrushIconBox::OnSize)
EVT_LEFT_DOWN(BrushIconBox::OnMouseClick)
EVT_RIGHT_DOWN(BrushIconBox::OnMouseRightClick)
EVT_MOTION(BrushIconBox::OnMouseMotion)
EVT_LEAVE_WINDOW(BrushIconBox::OnMouseLeave)
EVT_KEY_DOWN(BrushIconBox::OnKey)
END_EVENT_TABLE()

BrushIconBox::BrushIconBox(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz, bool useActualSize) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
	BrushBoxInterface(_tileset),
	icon_size(rsz),
	use_actual_size(useActualSize),
	slot_size((useActualSize || rsz == RENDER_SIZE_32x32 ? 32 : 16) + 2 * CELL_MARGIN),
	columns(1),
	virtual_height(0),
	selected_index(-1) {
	ASSERT(tileset->getType() >= TILESET_UNKNOWN && tileset->getType() <= TILESET_HOUSE);
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(ThemeManager::Get().GetPalette().control);
	SetScrollRate(slot_size, slot_size);

	RecalculateGrid();
}

BrushIconBox::~BrushIconBox() {
	////
}

void BrushIconBox::RecalculateGrid() {
	// Determine how many columns fit in the currently available width. Use the
	// real cell size as the divisor so the grid fills the panel - otherwise a
	// gap grows between the icons and the scrollbar as the palette is widened.
	const int button_width = slot_size;

	int available_width = 0;
	int client_height = 0;
	GetClientSize(&available_width, &client_height);
	available_width -= 20; // account for the vertical scrollbar

	if (available_width <= 0) {
		// Not realised yet - fall back to the parent hierarchy, then a default.
		for (wxWindow* w = GetParent(); w; w = w->GetParent()) {
			int cw = 0, ch = 0;
			w->GetClientSize(&cw, &ch);
			if (cw > 0) {
				available_width = cw - 20;
				break;
			}
		}
	}
	available_width = std::max(available_width, 350);

	int column_count = std::max(available_width / button_width, 1);
	const int min_columns = (use_actual_size || icon_size == RENDER_SIZE_32x32)
		? std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) / 2 + 1, 1)
		: std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) + 1, 1);
	column_count = std::max(column_count, min_columns);
	columns = column_count;

	// Preserve the current selection across a reflow.
	Brush* previously_selected = (selected_index >= 0 && selected_index < static_cast<int>(cells.size()))
		? cells[selected_index].brush
		: nullptr;

	cells.clear();
	cells.reserve(tileset->size());
	selected_index = -1;
	multi_selected.clear();

	const int sep_height = 10;
	const int full_width = columns * slot_size;

	if (use_actual_size) {
		std::vector<int> column_fill(columns, 0); // measured in slot rows
		for (BrushVector::const_iterator iter = tileset->brushlist.begin(); iter != tileset->brushlist.end(); ++iter) {
			Brush* brush = *iter;
			if (!brush) {
				continue;
			}
			if (brush->isPaletteSeparator()) {
				int sepRow = 0;
				for (int c = 0; c < columns; ++c) {
					sepRow = std::max(sepRow, column_fill[c]);
				}
				cells.push_back({ nullptr, wxRect(0, sepRow * slot_size, full_width, sep_height) });
				const int next = sepRow + 1;
				for (int c = 0; c < columns; ++c) {
					column_fill[c] = next;
				}
				continue;
			}

			int span_cols = 1;
			int span_rows = 1;
			if (Sprite* sprite = g_gui.gfx.getSprite(brush->getLookID())) {
				if (GameSprite* game_sprite = dynamic_cast<GameSprite*>(sprite)) {
					span_cols = std::max(1, int(game_sprite->width));
					span_rows = std::max(1, int(game_sprite->height));
				}
			}
			span_cols = std::max(1, std::min(span_cols, columns));

			int best_col = 0;
			int best_row = std::numeric_limits<int>::max();
			for (int col = 0; col <= columns - span_cols; ++col) {
				int row = 0;
				for (int c = col; c < col + span_cols; ++c) {
					row = std::max(row, column_fill[c]);
				}
				if (row < best_row) {
					best_row = row;
					best_col = col;
				}
			}
			if (best_row == std::numeric_limits<int>::max()) {
				best_row = 0;
			}
			for (int c = best_col; c < best_col + span_cols; ++c) {
				column_fill[c] = best_row + span_rows;
			}
			cells.push_back({ brush, wxRect(best_col * slot_size, best_row * slot_size, span_cols * slot_size, span_rows * slot_size) });
		}
		int max_fill = 0;
		for (int c = 0; c < columns; ++c) {
			max_fill = std::max(max_fill, column_fill[c]);
		}
		virtual_height = max_fill * slot_size;
	} else {
		int col = 0;
		int y = 0;
		for (BrushVector::const_iterator iter = tileset->brushlist.begin(); iter != tileset->brushlist.end(); ++iter) {
			Brush* brush = *iter;
			if (!brush) {
				continue;
			}
			if (brush->isPaletteSeparator()) {
				if (col != 0) {
					y += slot_size;
					col = 0;
				}
				cells.push_back({ nullptr, wxRect(0, y, full_width, sep_height) });
				y += sep_height;
				continue;
			}
			cells.push_back({ brush, wxRect(col * slot_size, y, slot_size, slot_size) });
			if (++col >= columns) {
				col = 0;
				y += slot_size;
			}
		}
		if (col != 0) {
			y += slot_size;
		}
		virtual_height = y;
	}

	if (previously_selected) {
		for (size_t i = 0; i < cells.size(); ++i) {
			if (cells[i].brush == previously_selected) {
				selected_index = static_cast<int>(i);
				break;
			}
		}
	}

	SetVirtualSize(full_width, virtual_height);
	Refresh();
}

int BrushIconBox::CellIndexAt(const wxPoint& unscrolled) const {
	for (size_t i = 0; i < cells.size(); ++i) {
		if (cells[i].brush && cells[i].rect.Contains(unscrolled)) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void BrushIconBox::DrawCell(wxDC& dc, const Cell& cell, bool selected) const {
	const wxRect& r = cell.rect;
	const ThemePalette& palette = ThemeManager::Get().GetPalette();

	if (!cell.brush) {
		// Separator line
		dc.SetPen(wxPen(palette.mutedText));
		const int y = r.GetY() + r.GetHeight() / 2;
		dc.DrawLine(r.GetX() + 4, y, r.GetRight() - 4, y);
		return;
	}

	// Every cell is CELL_MARGIN px larger than its sprite on each side. The
	// margin is *outside* the sprite: the sprite is blitted at its native size
	// inside `inner`, so it is never clipped or squashed.
	wxRect inner = r;
	inner.Deflate(CELL_MARGIN);

	// Background fill (honours the configurable icon background shade)
	const int bgshade = g_settings.getInteger(Config::ICON_BACKGROUND);
	wxColour fill = (bgshade < 0) ? palette.control : wxColour(bgshade, bgshade, bgshade);
	dc.SetBrush(wxBrush(fill));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(inner);

	if (!use_actual_size) {
		dc.SetPen(selected ? wxPen(palette.window) : wxPen(palette.surface));
		dc.DrawLine(inner.GetX(), inner.GetY(), inner.GetRight(), inner.GetY());
		dc.DrawLine(inner.GetX(), inner.GetY(), inner.GetX(), inner.GetBottom());
		dc.SetPen(selected ? wxPen(palette.surface) : wxPen(palette.border));
		dc.DrawLine(inner.GetX(), inner.GetBottom(), inner.GetRight(), inner.GetBottom());
		dc.DrawLine(inner.GetRight(), inner.GetY(), inner.GetRight(), inner.GetBottom());
	}

	if (Sprite* spr = g_gui.gfx.getSprite(cell.brush->getLookID())) {
		SpriteSize sprite_size = SPRITE_SIZE_32x32;
		if (use_actual_size) {
			sprite_size = SPRITE_SIZE_ACTUAL;
		} else if (icon_size == RENDER_SIZE_16x16) {
			sprite_size = SPRITE_SIZE_16x16;
		}
		// Blit the sprite at its native size, centred in the cell. Exact-size
		// cells spanning several slots also absorb the inter-slot margins here.
		int draw_w = inner.GetWidth();
		int draw_h = inner.GetHeight();
		if (sprite_size == SPRITE_SIZE_ACTUAL) {
			draw_w = draw_h = SPRITE_PIXELS;
			if (GameSprite* game_sprite = dynamic_cast<GameSprite*>(spr)) {
				draw_w = std::max(1, int(game_sprite->width)) * SPRITE_PIXELS;
				draw_h = std::max(1, int(game_sprite->height)) * SPRITE_PIXELS;
			}
			draw_w = std::min(draw_w, inner.GetWidth());
			draw_h = std::min(draw_h, inner.GetHeight());
		}
		const int draw_x = inner.GetX() + (inner.GetWidth() - draw_w) / 2;
		const int draw_y = inner.GetY() + (inner.GetHeight() - draw_h) / 2;
		spr->DrawTo(&dc, sprite_size, draw_x, draw_y, draw_w, draw_h);

		if (selected && g_settings.getInteger(Config::USE_GUI_SELECTION_SHADOW)) {
			if (Sprite* marker = g_gui.gfx.getSprite(EDITOR_SPRITE_SELECTION_MARKER)) {
				SpriteSize overlay_size = (sprite_size == SPRITE_SIZE_ACTUAL) ? SPRITE_SIZE_32x32 : sprite_size;
				marker->DrawTo(&dc, overlay_size, draw_x, draw_y, draw_w, draw_h);
			}
		}
	}

	// Exact-size cells draw the sprite edge to edge inside the margin, so mark
	// selection with a highlight rectangle on top rather than an (absent) bevel.
	if (use_actual_size && selected) {
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(palette.selection, 2));
		dc.DrawRectangle(inner);
	}
}

void BrushIconBox::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	DoPrepareDC(dc); // applies the scroll offset

	dc.SetBackground(wxBrush(ThemeManager::Get().GetPalette().control));
	dc.Clear();

	if (g_gui.gfx.isUnloaded()) {
		return;
	}

	// Compute the visible band in unscrolled coordinates so we only draw cells
	// that are actually on screen.
	int view_start_y = 0;
	GetViewStart(nullptr, &view_start_y);
	int ppuX = 0, ppuY = 0;
	GetScrollPixelsPerUnit(&ppuX, &ppuY);
	int client_w = 0, client_h = 0;
	GetClientSize(&client_w, &client_h);

	const int top = view_start_y * ppuY;
	const int bottom = top + client_h;

	for (size_t i = 0; i < cells.size(); ++i) {
		const wxRect& r = cells[i].rect;
		if (r.GetBottom() < top || r.GetY() > bottom) {
			continue;
		}
		const bool sel = static_cast<int>(i) == selected_index || multi_selected.count(static_cast<int>(i)) > 0;
		DrawCell(dc, cells[i], sel);
	}
}

void BrushIconBox::OnSize(wxSizeEvent& event) {
	const int old_columns = columns;
	RecalculateGrid();
	// Keep the selected brush in view after a reflow.
	if (columns != old_columns && selected_index >= 0) {
		EnsureVisible(static_cast<size_t>(selected_index));
	}
	event.Skip();
}

void BrushIconBox::SelectFirstBrush() {
	for (size_t i = 0; i < cells.size(); ++i) {
		if (cells[i].brush) {
			selected_index = static_cast<int>(i);
			Refresh();
			EnsureVisible(i);
			return;
		}
	}
}

Brush* BrushIconBox::GetSelectedBrush() const {
	if (selected_index >= 0 && selected_index < static_cast<int>(cells.size())) {
		return cells[selected_index].brush;
	}
	return nullptr;
}

bool BrushIconBox::SelectBrush(const Brush* whatbrush) {
	for (size_t i = 0; i < cells.size(); ++i) {
		if (cells[i].brush == whatbrush) {
			selected_index = static_cast<int>(i);
			Refresh();
			EnsureVisible(i);
			return true;
		}
	}
	selected_index = -1;
	Refresh();
	return false;
}

void BrushIconBox::EnsureVisible(size_t n) {
	if (n >= cells.size()) {
		return;
	}
	int ppuX = 0, ppuY = 0;
	GetScrollPixelsPerUnit(&ppuX, &ppuY);
	if (ppuY <= 0) {
		return;
	}

	const wxRect& r = cells[n].rect;
	int view_start_y = 0;
	GetViewStart(nullptr, &view_start_y);
	int client_w = 0, client_h = 0;
	GetClientSize(&client_w, &client_h);

	const int top = view_start_y * ppuY;
	const int bottom = top + client_h;

	if (r.GetY() < top) {
		Scroll(-1, r.GetY() / ppuY);
	} else if (r.GetBottom() > bottom) {
		Scroll(-1, (r.GetBottom() - client_h) / ppuY + 1);
	}
}

void BrushIconBox::HandleBrushSelection(Brush* brush) {
	if (picker_mode) {
		return;
	}

	wxWindow* w = this;
	while ((w = w->GetParent()) && dynamic_cast<PaletteWindow*>(w) == nullptr)
		;
	if (w) {
		g_gui.ActivatePalette(static_cast<PaletteWindow*>(w));
	}
	g_gui.SelectBrush(brush, tileset->getType());
}

void BrushIconBox::OnMouseClick(wxMouseEvent& event) {
	int ux = 0, uy = 0;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
	const int index = CellIndexAt(wxPoint(ux, uy));
	if (index < 0) {
		event.Skip();
		return;
	}

	Brush* brush = cells[index].brush;
	if (!brush || brush->isPaletteSeparator()) {
		return;
	}

	if (wxGetKeyState(WXK_CONTROL)) {
		if (BrushCanBeEdited(brush)) {
			OpenBrushEditor(brush);
		} else if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
			OpenTilesetEditor(const_cast<Tileset*>(&tileset->tileset), tileset->getType());
		} else {
			OpenBrushEditor(brush);
		}
		return;
	}

	if (event.ShiftDown()) {
		// Shift+click: toggle this cell in the multi-select set.
		if (multi_selected.count(index)) {
			multi_selected.erase(index);
		} else {
			multi_selected.insert(index);
		}
		Refresh();
		return;
	}

	// Normal click: clear any multi-selection and select only this cell.
	multi_selected.clear();
	selected_index = index;
	Refresh();
	FocusMapCanvasFromPalette();
	HandleBrushSelection(brush);
}

void BrushIconBox::OnMouseRightClick(wxMouseEvent& event) {
	if (!wxGetKeyState(WXK_CONTROL)) {
		event.Skip();
		return;
	}

	int ux = 0, uy = 0;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
	const int index = CellIndexAt(wxPoint(ux, uy));
	if (index < 0) {
		event.Skip();
		return;
	}

	// Collect IDs from the clicked cell and every Shift-selected cell.
	std::vector<uint16_t> ids;
	auto collectRaw = [&](int idx) {
		if (idx < 0 || idx >= static_cast<int>(cells.size())) return;
		Brush* b = cells[idx].brush;
		if (b && !b->isPaletteSeparator() && b->isRaw()) {
			if (RAWBrush* raw = b->asRaw()) {
				ids.push_back(raw->getItemID());
			}
		}
	};

	collectRaw(index);
	for (int idx : multi_selected) {
		if (idx != index) {
			collectRaw(idx);
		}
	}

	TryOpenAddToTileset(std::move(ids), tileset->getType());
}

// ============================================================================
// BrushListBox

BEGIN_EVENT_TABLE(BrushListBox, wxVListBox)
EVT_KEY_DOWN(BrushListBox::OnKey)
EVT_CHAR(BrushListBox::OnChar)
EVT_MOTION(BrushListBox::OnMouseMotion)
EVT_RIGHT_DOWN(BrushListBox::OnMouseRightClick)
EVT_LEAVE_WINDOW(BrushListBox::OnMouseLeave)
END_EVENT_TABLE()

BrushListBox::BrushListBox(wxWindow* parent, const TilesetCategory* tileset) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	BrushBoxInterface(tileset) {
	SetItemCount(tileset->size());
}

BrushListBox::~BrushListBox() {
	////
}

void BrushListBox::SelectAll() {
	// No-op for single-selection listbox to prevent assertion
}

void BrushListBox::OnChar(wxKeyEvent& event) {
	// Intercept Ctrl+A to prevent SelectAll assertion for single-selection listbox
	if (isSelectAllShortcut(event)) {
		// Do nothing - prevent base class from calling SelectAll
		return;
	}
	event.Skip();
}

void BrushListBox::SelectFirstBrush() {
	if (!tileset || tileset->size() == 0) {
		return;
	}
	for (size_t n = 0; n < tileset->size(); ++n) {
		Brush* brush = tileset->brushlist[n];
		if (brush && !brush->isPaletteSeparator()) {
			SetSelection(n);
			wxWindow::ScrollLines(-1);
			return;
		}
	}
}

void BrushListBox::OnMouseRightClick(wxMouseEvent& event) {
	if (!wxGetKeyState(WXK_CONTROL) || !tileset) {
		event.Skip();
		return;
	}

	int n = VirtualHitTest(event.GetY());
	if (n == wxNOT_FOUND || n < 0 || static_cast<size_t>(n) >= tileset->size()) {
		event.Skip();
		return;
	}

	Brush* b = tileset->brushlist[n];
	if (b && !b->isPaletteSeparator() && b->isRaw()) {
		if (RAWBrush* raw = b->asRaw()) {
			TryOpenAddToTileset({ raw->getItemID() }, tileset->getType());
		}
	}
}

Brush* BrushListBox::GetSelectedBrush() const {
	if (!tileset) {
		return nullptr;
	}

	int n = GetSelection();
	if (n != wxNOT_FOUND && n >= 0 && static_cast<size_t>(n) < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if (brush && !brush->isPaletteSeparator()) {
			return brush;
		}
	}
	return FirstSelectableBrush(tileset);
}

bool BrushListBox::SelectBrush(const Brush* whatbrush) {
	if (!tileset) {
		return false;
	}
	
	for (size_t n = 0; n < tileset->size(); ++n) {
		Brush* brush = tileset->brushlist[n];
		if (brush && !brush->isPaletteSeparator() && brush == whatbrush) {
			SetSelection(n);
			return true;
		}
	}
	return false;
}

void BrushListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const {
	// Safety check: tileset might be invalid during shutdown
	if (!tileset || n >= tileset->size()) {
		return;
	}

	Brush* brush = tileset->brushlist[n];
	if (!brush) {
		return;
	}
	if (brush->isPaletteSeparator()) {
		dc.SetPen(wxPen(ThemeManager::Get().GetPalette().mutedText));
		dc.DrawLine(rect.GetX() + 4, rect.GetY() + rect.GetHeight() / 2, rect.GetRight() - 4, rect.GetY() + rect.GetHeight() / 2);
		return;
	}

	Sprite* spr = g_gui.gfx.getSprite(brush->getLookID());
	if (spr) {
		spr->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
	}
	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);
	dc.DrawText(wxstr(brush->getName()), rect.GetX() + 40, rect.GetY() + 6);
}

wxCoord BrushListBox::OnMeasureItem(size_t n) const {
	if (tileset && n < tileset->size() && tileset->brushlist[n] && tileset->brushlist[n]->isPaletteSeparator()) {
		return 10;
	}
	return 32;
}

void BrushListBox::OnMouseMotion(wxMouseEvent& event) {
	if (!tileset) {
		HidePaletteBrushHoverTooltip();
		event.Skip();
		return;
	}

	const ssize_t item = VirtualHitTest(event.GetY());
	if (item != wxNOT_FOUND && static_cast<size_t>(item) < tileset->size()) {
		Brush* brush = tileset->brushlist[item];
		if (brush && !brush->isPaletteSeparator()) {
			ShowPaletteBrushHoverTooltip(brush, wxGetMousePosition());
			event.Skip();
			return;
		}
	}

	HidePaletteBrushHoverTooltip();
	event.Skip();
}

void BrushListBox::OnMouseLeave(wxMouseEvent& event) {
	HidePaletteBrushHoverTooltip();
	event.Skip();
}

void BrushIconBox::OnMouseMotion(wxMouseEvent& event) {
	int ux = 0;
	int uy = 0;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
	const int index = CellIndexAt(wxPoint(ux, uy));
	if (index >= 0) {
		ShowPaletteBrushHoverTooltip(cells[index].brush, wxGetMousePosition());
		event.Skip();
		return;
	}

	HidePaletteBrushHoverTooltip();
	event.Skip();
}

void BrushIconBox::OnMouseLeave(wxMouseEvent& event) {
	HidePaletteBrushHoverTooltip();
	event.Skip();
}

void BrushIconBox::OnKey(wxKeyEvent& event) {
	ForwardPaletteKeyToMap(event);
}

void BrushListBox::OnKey(wxKeyEvent& event) {
	ForwardPaletteKeyToMap(event);
}
