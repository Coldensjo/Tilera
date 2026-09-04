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
#include "edit_item_type_window.h"
#include "items.h"
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
#include <wx/timer.h>

namespace {

	// Matches MapCanvas' AnimationTimer so palette and map frames stay in step.
	constexpr int PALETTE_ANIMATION_INTERVAL_MS = 100;

	// Palette sprites animate only when the map preview animates (View > Show preview).
	bool PaletteAnimationEnabled() {
		return g_settings.getBoolean(Config::SHOW_PREVIEW);
	}

	// Returns the frame to draw `sprite` with, and records whether it animates so
	// the owning box knows to keep repainting.
	int PaletteSpriteFrame(Sprite* sprite, bool& animated_visible) {
		if (!sprite || !sprite->isAnimated() || !PaletteAnimationEnabled()) {
			return 0;
		}
		animated_visible = true;
		return sprite->getCurrentFrame();
	}

	// Palette magnification is restricted to powers of two so sprites are always
	// scaled by a whole number of pixels (crisp nearest-neighbour: 32/64/128/256).
	constexpr int PALETTE_ZOOM_MIN = 1;
	constexpr int PALETTE_ZOOM_MAX = 8;

	int ClampPaletteZoom(int zoom) {
		int clamped = PALETTE_ZOOM_MIN;
		while (clamped * 2 <= zoom && clamped * 2 <= PALETTE_ZOOM_MAX) {
			clamped *= 2;
		}
		return clamped;
	}

	int PaletteZoom() {
		return ClampPaletteZoom(g_settings.getInteger(Config::PALETTE_ZOOM));
	}

	// Handles Ctrl+wheel on a palette view: steps the shared zoom setting one
	// power of two up or down. Returns true when the zoom actually changed.
	bool StepPaletteZoomFromWheel(const wxMouseEvent& event) {
		if (!event.ControlDown() || event.GetWheelRotation() == 0) {
			return false;
		}
		const int current = PaletteZoom();
		const int next = ClampPaletteZoom(event.GetWheelRotation() > 0 ? current * 2 : current / 2);
		if (next == current) {
			return false;
		}
		g_settings.setInteger(Config::PALETTE_ZOOM, next);
		return true;
	}

	// Draws `sprite` with its top-left corner at (x, y). `base_w`/`base_h` are the
	// unmagnified pixel dimensions to take from the sprite bitmap; they are then
	// blown up by `zoom` with nearest-neighbour sampling so pixel art stays sharp.
	void DrawPaletteSprite(wxDC& dc, Sprite* sprite, SpriteSize size, int x, int y, int base_w, int base_h, int frame, int zoom) {
		if (zoom <= 1) {
			sprite->DrawTo(&dc, size, x, y, base_w, base_h, frame);
			return;
		}
		wxBitmap* bitmap = sprite->getBitmap(size, frame);
		if (!bitmap || !bitmap->IsOk()) {
			const wxBrush& old = dc.GetBrush();
			dc.SetBrush(*wxRED_BRUSH);
			dc.DrawRectangle(x, y, base_w * zoom, base_h * zoom);
			dc.SetBrush(old);
			return;
		}
		wxImage image = bitmap->ConvertToImage();
		base_w = std::min(base_w, image.GetWidth());
		base_h = std::min(base_h, image.GetHeight());
		if (base_w <= 0 || base_h <= 0) {
			return;
		}
		if (base_w != image.GetWidth() || base_h != image.GetHeight()) {
			image = image.GetSubImage(wxRect(0, 0, base_w, base_h));
		}
		image.Rescale(base_w * zoom, base_h * zoom, wxIMAGE_QUALITY_NEAREST);
		dc.DrawBitmap(wxBitmap(image), x, y, true);
	}

} // namespace

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
		// Deferred: this can run from a brushbox event handler, and the
		// refresh destroys and recreates the brushboxes - rebuilding while
		// still inside one of their handlers would unwind into a deleted
		// window.
		g_gui.root->CallAfter([changedCategory, changedTileset]() {
			g_gui.RefreshTilesetAddition(changedCategory, changedTileset);
		});
	}
}

// Plain right-click on a palette item: context menu with the item-type
// actions, so items can be edited without placing them on the map first.
void ShowPaletteItemContextMenu(wxWindow* window, uint16_t itemId, const TilesetCategory* category) {
	const ItemType& it = g_items.getItemType(itemId);
	if (it.id == 0 || !category) {
		return;
	}
	const TilesetCategoryType categoryType = category->getType();
	const std::string tilesetName = category->tileset.name;

	wxMenu menu;
	wxMenuItem* editEntry = menu.Append(wxID_ANY, "Edit Item...", "Edit this item type's name, attributes, flags and sprite");
	wxMenuItem* duplicateEntry = menu.Append(wxID_ANY, "Duplicate Item", "Create an identical copy with its own sprite (items.xml, items.otb, .dat and .spr)");
	wxMenuItem* addEntry = menu.Append(wxID_ANY, "Add to Tileset...", "Add or move this item to a tileset");
	menu.Bind(wxEVT_MENU, [window, itemId](wxCommandEvent&) {
		EditItemTypeWindow dialog(g_gui.root, itemId);
		dialog.ShowModal();
		window->Refresh(); // pick up a changed name/sprite in the palette
	}, editEntry->GetId());
	menu.Bind(wxEVT_MENU, [itemId, categoryType, tilesetName](wxCommandEvent&) {
		wxString error;
		const uint16_t newId = DuplicateItemType(itemId, error);
		if (newId == 0) {
			g_gui.PopupDialog("Error", error, wxOK);
			return;
		}
		// Drop the copy at the bottom of the same tileset so it is visible
		// right away. Deferred: this handler runs on a palette widget that
		// the refresh below destroys and recreates - rebuilding here would
		// unwind into a deleted window.
		g_gui.root->CallAfter([newId, categoryType, tilesetName]() {
			wxString addError;
			if (AddItemToTilesetAt(newId, tilesetName, categoryType, { TilesetInsertSpec::BOTTOM, 0 }, addError)) {
				g_gui.RefreshTilesetAddition(categoryType, tilesetName);
			}
			const ItemType& copy = g_items.getItemType(newId);
			g_gui.PopupDialog("Item duplicated", wxString::Format("Created server id %u (client id %u) at the bottom of tileset '%s'.", static_cast<unsigned>(newId), static_cast<unsigned>(copy.clientID), wxstr(tilesetName)), wxOK);
		});
	}, duplicateEntry->GetId());
	menu.Bind(wxEVT_MENU, [itemId, categoryType](wxCommandEvent&) {
		TryOpenAddToTileset({ itemId }, categoryType);
	}, addEntry->GetId());
	window->PopupMenu(&menu);
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
	wxChoicebook* tmp_choicebook = newd wxChoicebook(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(180, 250)), wxCLIP_CHILDREN);
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

	// Give the new brushbox its size immediately. On a page switch the
	// choicebook lays the page out afterwards, but when content is rebuilt
	// in place (RefreshTilesetPage after adding/duplicating an item) no
	// layout pass follows - the box would stay zero-sized behind the old
	// content's stale pixels and swallow every click until the page is
	// re-entered.
	Layout();
	Refresh();
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
EVT_MOUSEWHEEL(BrushIconBox::OnMouseWheel)
EVT_TIMER(wxID_ANY, BrushIconBox::OnAnimationTimer)
END_EVENT_TABLE()

BrushIconBox::BrushIconBox(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz, bool useActualSize) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
	BrushBoxInterface(_tileset),
	icon_size(rsz),
	use_actual_size(useActualSize),
	base_icon_px(useActualSize || rsz == RENDER_SIZE_32x32 ? 32 : 16),
	zoom(PaletteZoom()),
	slot_size(base_icon_px * zoom + 2 * CELL_MARGIN),
	columns(1),
	virtual_height(0),
	selected_index(-1),
	animation_timer(this) {
	ASSERT(tileset->getType() >= TILESET_UNKNOWN && tileset->getType() <= TILESET_HOUSE);
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(ThemeManager::Get().GetPalette().control);
	SetScrollRate(slot_size, slot_size);

	RecalculateGrid();
}

BrushIconBox::~BrushIconBox() {
	animation_timer.Stop();
}

void BrushIconBox::OnAnimationTimer(wxTimerEvent& WXUNUSED(event)) {
	if (!animated_sprite_visible || !IsShownOnScreen() || g_gui.gfx.isUnloaded()) {
		animation_timer.Stop();
		return;
	}
	Refresh(false);
}

void BrushIconBox::OnMouseWheel(wxMouseEvent& event) {
	if (!StepPaletteZoomFromWheel(event)) {
		event.Skip(); // plain wheel keeps scrolling the grid
		return;
	}
	RecalculateGrid();
	Refresh();
	if (selected_index >= 0) {
		EnsureVisible(static_cast<size_t>(selected_index));
	}
}

void BrushIconBox::RecalculateGrid() {
	// Pick up the shared zoom setting (changed from any palette view) and size
	// the slots accordingly.
	zoom = PaletteZoom();
	slot_size = base_icon_px * zoom + 2 * CELL_MARGIN;
	SetScrollRate(slot_size, slot_size);

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
	// The configured minimum column count is meant for unmagnified icons; a
	// zoomed grid would otherwise be forced far wider than the (non-horizontally
	// scrolling) panel, so relax it in proportion.
	int min_columns = (use_actual_size || icon_size == RENDER_SIZE_32x32)
		? std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) / 2 + 1, 1)
		: std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) + 1, 1);
	min_columns = std::max(min_columns / zoom, 1);
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

	// Selection highlight: tint the cell *background* so the color shows
	// through the sprite's transparent pixels instead of covering the art,
	// and pair it with a distinct border (drawn after the sprite, in the
	// cell margin).
	const bool selection_shadow = selected && g_settings.getInteger(Config::USE_GUI_SELECTION_SHADOW);
	if (selection_shadow) {
		const wxColour& sel = palette.selection;
		fill = wxColour(
			(sel.Red() * 2 + fill.Red()) / 3,
			(sel.Green() * 2 + fill.Green()) / 3,
			(sel.Blue() * 2 + fill.Blue()) / 3
		);
	}
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
		// Blit the sprite at its native size (times the integer zoom), centred in
		// the cell. Exact-size cells spanning several slots also absorb the
		// inter-slot margins here. `base_*` are unmagnified sprite pixels.
		int base_w = inner.GetWidth() / zoom;
		int base_h = inner.GetHeight() / zoom;
		if (sprite_size == SPRITE_SIZE_ACTUAL) {
			base_w = base_h = SPRITE_PIXELS;
			if (GameSprite* game_sprite = dynamic_cast<GameSprite*>(spr)) {
				base_w = std::max(1, int(game_sprite->width)) * SPRITE_PIXELS;
				base_h = std::max(1, int(game_sprite->height)) * SPRITE_PIXELS;
			}
			base_w = std::min(base_w, inner.GetWidth() / zoom);
			base_h = std::min(base_h, inner.GetHeight() / zoom);
		}
		const int draw_w = base_w * zoom;
		const int draw_h = base_h * zoom;
		const int draw_x = inner.GetX() + (inner.GetWidth() - draw_w) / 2;
		const int draw_y = inner.GetY() + (inner.GetHeight() - draw_h) / 2;
		const int frame = PaletteSpriteFrame(spr, animated_sprite_visible);
		DrawPaletteSprite(dc, spr, sprite_size, draw_x, draw_y, base_w, base_h, frame, zoom);
	}

	if (selection_shadow) {
		// Distinct selection border, drawn in the cell margin so it never
		// covers the sprite itself. Brighter and bluer than the raw theme
		// selection color, which all but disappears as a thin dark outline
		// around fully opaque tiles (grounds). Two nested 1px rectangles
		// guarantee a full 2px of visible border.
		const wxColour& sel = palette.selection;
		const wxColour borderColor(std::min(255, sel.Red() + 30), std::min(255, sel.Green() + 60), std::min(255, sel.Blue() + 120));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(borderColor, 1));
		wxRect borderRect = r;
		dc.DrawRectangle(borderRect);
		borderRect.Deflate(1);
		dc.DrawRectangle(borderRect);
	} else if (use_actual_size && selected) {
		// Exact-size cells draw the sprite edge to edge inside the margin, so
		// mark selection with a highlight rectangle rather than an (absent)
		// bevel.
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

	// Zoom is shared between palette views; re-layout if another view changed it.
	if (zoom != PaletteZoom()) {
		RecalculateGrid();
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

	animated_sprite_visible = false;
	for (size_t i = 0; i < cells.size(); ++i) {
		const wxRect& r = cells[i].rect;
		if (r.GetBottom() < top || r.GetY() > bottom) {
			continue;
		}
		const bool sel = static_cast<int>(i) == selected_index || multi_selected.count(static_cast<int>(i)) > 0;
		DrawCell(dc, cells[i], sel);
	}

	// Keep repainting while animated sprites are in view; the timer stops itself otherwise.
	if (animated_sprite_visible && !animation_timer.IsRunning()) {
		animation_timer.Start(PALETTE_ANIMATION_INTERVAL_MS);
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
	int ux = 0, uy = 0;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &ux, &uy);
	const int index = CellIndexAt(wxPoint(ux, uy));
	if (index < 0) {
		event.Skip();
		return;
	}

	if (!wxGetKeyState(WXK_CONTROL)) {
		// Plain right-click: item context menu (Edit Item / Add to Tileset).
		Brush* brush = cells[index].brush;
		if (brush && !brush->isPaletteSeparator() && brush->isRaw()) {
			if (RAWBrush* raw = brush->asRaw()) {
				ShowPaletteItemContextMenu(this, raw->getItemID(), tileset);
				return;
			}
		}
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
EVT_MOUSEWHEEL(BrushListBox::OnMouseWheel)
EVT_TIMER(wxID_ANY, BrushListBox::OnAnimationTimer)
END_EVENT_TABLE()

BrushListBox::BrushListBox(wxWindow* parent, const TilesetCategory* tileset) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	BrushBoxInterface(tileset),
	animation_timer(this) {
	SetItemCount(tileset->size());
	// wxVListBox paints rows one at a time, so the "animated sprite visible"
	// flag is reset once per paint here, before the first row is drawn.
	Bind(wxEVT_PAINT, [this](wxPaintEvent& event) {
		animated_sprite_visible = false;
		event.Skip();
	});
}

BrushListBox::~BrushListBox() {
	animation_timer.Stop();
}

void BrushListBox::OnAnimationTimer(wxTimerEvent& WXUNUSED(event)) {
	if (!animated_sprite_visible || !IsShownOnScreen() || g_gui.gfx.isUnloaded()) {
		animation_timer.Stop();
		return;
	}
	RefreshAll();
}

void BrushListBox::OnMouseWheel(wxMouseEvent& event) {
	if (!StepPaletteZoomFromWheel(event)) {
		event.Skip(); // plain wheel keeps scrolling the list
		return;
	}
	// Row heights depend on the zoom: force wxVListBox to re-measure every row.
	const int selection = GetSelection();
	SetItemCount(tileset ? tileset->size() : 0);
	if (selection != wxNOT_FOUND) {
		SetSelection(selection);
		ScrollToRow(selection);
	}
	RefreshAll();
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
	if (!tileset) {
		event.Skip();
		return;
	}

	int n = VirtualHitTest(event.GetY());
	if (n == wxNOT_FOUND || n < 0 || static_cast<size_t>(n) >= tileset->size()) {
		event.Skip();
		return;
	}

	Brush* b = tileset->brushlist[n];
	if (!b || b->isPaletteSeparator() || !b->isRaw()) {
		event.Skip();
		return;
	}
	RAWBrush* raw = b->asRaw();
	if (!raw) {
		event.Skip();
		return;
	}

	if (wxGetKeyState(WXK_CONTROL)) {
		TryOpenAddToTileset({ raw->getItemID() }, tileset->getType());
	} else {
		// Plain right-click: item context menu (Edit Item / Add to Tileset).
		ShowPaletteItemContextMenu(this, raw->getItemID(), tileset);
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

	const int zoom = PaletteZoom();
	const int icon_px = 32 * zoom;
	Sprite* spr = g_gui.gfx.getSprite(brush->getLookID());
	if (spr) {
		const int frame = PaletteSpriteFrame(spr, animated_sprite_visible);
		DrawPaletteSprite(dc, spr, SPRITE_SIZE_32x32, rect.GetX(), rect.GetY(), 32, 32, frame, zoom);
		if (animated_sprite_visible && !animation_timer.IsRunning()) {
			animation_timer.Start(PALETTE_ANIMATION_INTERVAL_MS);
		}
	}
	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);
	const int text_y = (zoom > 1) ? rect.GetY() + (rect.GetHeight() - dc.GetCharHeight()) / 2 : rect.GetY() + 6;
	dc.DrawText(wxstr(brush->getName()), rect.GetX() + icon_px + 8, text_y);
}

wxCoord BrushListBox::OnMeasureItem(size_t n) const {
	if (tileset && n < tileset->size() && tileset->brushlist[n] && tileset->brushlist[n]->isPaletteSeparator()) {
		return 10;
	}
	return 32 * PaletteZoom();
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
