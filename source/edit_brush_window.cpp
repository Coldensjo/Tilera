//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "edit_brush_window.h"
#include "brush.h"
#include "items.h"
#include "find_item_window.h"
#include "find_border_window.h"
#include "gui.h"
#include "materials.h"
#include "dcbutton.h"
#include "graphics.h"
#include "theme.h"

namespace {
enum {
	EDIT_BRUSH_LIST = 20001,
	EDIT_BRUSH_COMPOSITE_TILE_LIST,
	EDIT_BRUSH_ITEM_ID,
	EDIT_BRUSH_TILE_X,
	EDIT_BRUSH_TILE_Y,
	EDIT_BRUSH_TILE_Z,
	EDIT_BRUSH_CHANCE,
	EDIT_BRUSH_PICK_ITEM,
	EDIT_BRUSH_PICK_BORDER,
	EDIT_BRUSH_ADD_ITEM,
	EDIT_BRUSH_ADD_COMPOSITE,
	EDIT_BRUSH_ADD_BORDER,
	EDIT_BRUSH_ADD_OPTIONAL,
	EDIT_BRUSH_ADD_TILE,
	EDIT_BRUSH_REMOVE_TILE,
	EDIT_BRUSH_REMOVE_ENTRY,
	EDIT_BRUSH_BORDER_ALIGN,
	EDIT_BRUSH_BORDER_TO,
	EDIT_BRUSH_BORDER_ID,
	EDIT_BRUSH_BORDER_SUPER,
	EDIT_BRUSH_GROUND_EQUIV,
};

void DrawItemSprite(wxDC& dc, int clientId, int x, int y, int width, int height) {
	if (clientId == 0) {
		return;
	}
	Sprite* sprite = g_gui.gfx.getSprite(clientId);
	if (sprite) {
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, width, height);
	}
}

void DrawCompositePreview(wxDC& dc, const wxRect& rect, const std::vector<std::pair<Position, uint16_t>>& tiles) {
	if (tiles.empty()) {
		return;
	}

	int minX = tiles.front().first.x;
	int minY = tiles.front().first.y;
	int maxX = minX;
	int maxY = minY;
	for (const auto& tileEntry : tiles) {
		minX = std::min(minX, tileEntry.first.x);
		minY = std::min(minY, tileEntry.first.y);
		maxX = std::max(maxX, tileEntry.first.x);
		maxY = std::max(maxY, tileEntry.first.y);
	}

	const int spanX = std::max(1, maxX - minX + 1);
	const int spanY = std::max(1, maxY - minY + 1);
	const int cell = std::max(8, std::min({rect.GetHeight() - 4, 32 / spanX, 32 / spanY}));
	const int previewWidth = spanX * cell;
	const int previewHeight = spanY * cell;
	const int originX = rect.GetX() + std::max(0, (36 - previewWidth) / 2);
	const int originY = rect.GetY() + std::max(0, (rect.GetHeight() - previewHeight) / 2);

	for (const auto& tileEntry : tiles) {
		const ItemType& it = g_items.getItemType(tileEntry.second);
		if (it.id == 0) {
			continue;
		}
		Sprite* sprite = g_gui.gfx.getSprite(it.clientID);
		if (!sprite) {
			continue;
		}
		const int px = originX + (tileEntry.first.x - minX) * cell;
		const int py = originY + (tileEntry.first.y - minY) * cell;
		sprite->DrawTo(&dc, SPRITE_SIZE_16x16, px, py, cell, cell);
	}
}

uint16_t GetTilePreviewClientId(const std::pair<Position, uint16_t>& tile) {
	const ItemType& it = g_items.getItemType(tile.second);
	return it.id != 0 ? it.clientID : 0;
}

uint16_t GetEntryPreviewClientId(const BrushEditEntry& entry) {
	if (entry.kind == BRUSH_EDIT_ITEM) {
		const ItemType& it = g_items.getItemType(entry.item_id);
		return it.id != 0 ? it.clientID : 0;
	}
	if (entry.kind == BRUSH_EDIT_GROUND_BORDER || entry.kind == BRUSH_EDIT_GROUND_OPTIONAL) {
		return BrushEditBorderPreviewId(entry.border_id);
	}
	if (!entry.composite_tiles.empty()) {
		return GetTilePreviewClientId(entry.composite_tiles.front());
	}
	return 0;
}

} // namespace

EditBrushListBox::EditBrushListBox(wxWindow* parent, wxWindowID id, EditBrushWindow* owner) :
	wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	owner(owner) {
	////
}

void EditBrushListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const {
	if (!owner || n >= owner->entries.size()) {
		return;
	}

	const BrushEditEntry& entry = owner->entries[n];
	if (entry.kind == BRUSH_EDIT_ITEM) {
		const ItemType& it = g_items.getItemType(entry.item_id);
		DrawItemSprite(dc, it.id != 0 ? it.clientID : 0, rect.GetX() + 2, rect.GetY() + 2, 32, 32);
	} else if (entry.kind == BRUSH_EDIT_GROUND_BORDER || entry.kind == BRUSH_EDIT_GROUND_OPTIONAL) {
		const wxRect previewRect(rect.GetX() + 2, rect.GetY() + 2, 32, 32);
		DrawAutoBorderPreview(dc, previewRect, g_brushes.getAutoBorder(entry.border_id));
	} else {
		DrawCompositePreview(dc, rect, entry.composite_tiles);
	}

	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);

	dc.DrawText(owner->FormatEntryLabel(entry), rect.GetX() + 40, rect.GetY() + 10);
}

wxCoord EditBrushListBox::OnMeasureItem(size_t WXUNUSED(n)) const {
	return 36;
}

EditCompositeTileListBox::EditCompositeTileListBox(wxWindow* parent, wxWindowID id, EditBrushWindow* owner) :
	wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	owner(owner) {
	////
}

void EditCompositeTileListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const {
	std::vector<std::pair<Position, uint16_t>>* tiles = owner ? owner->GetSelectedCompositeTiles() : nullptr;
	if (!tiles || n >= tiles->size()) {
		return;
	}

	const auto& tile = tiles->at(n);
	DrawItemSprite(dc, GetTilePreviewClientId(tile), rect.GetX() + 2, rect.GetY() + 2, 32, 32);

	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);

	dc.DrawText(owner->FormatCompositeTileLabel(tile), rect.GetX() + 40, rect.GetY() + 10);
}

wxCoord EditCompositeTileListBox::OnMeasureItem(size_t WXUNUSED(n)) const {
	return 36;
}

BEGIN_EVENT_TABLE(EditBrushWindow, wxDialog)
EVT_BUTTON(EDIT_BRUSH_ADD_ITEM, EditBrushWindow::OnClickAddItem)
EVT_BUTTON(EDIT_BRUSH_ADD_COMPOSITE, EditBrushWindow::OnClickAddComposite)
EVT_BUTTON(EDIT_BRUSH_ADD_BORDER, EditBrushWindow::OnClickAddBorder)
EVT_BUTTON(EDIT_BRUSH_ADD_OPTIONAL, EditBrushWindow::OnClickAddOptional)
EVT_BUTTON(EDIT_BRUSH_ADD_TILE, EditBrushWindow::OnClickAddTile)
EVT_BUTTON(EDIT_BRUSH_REMOVE_TILE, EditBrushWindow::OnClickRemoveTile)
EVT_BUTTON(EDIT_BRUSH_REMOVE_ENTRY, EditBrushWindow::OnClickRemoveEntry)
EVT_BUTTON(wxID_SAVE, EditBrushWindow::OnClickSave)
EVT_BUTTON(wxID_CANCEL, EditBrushWindow::OnClickCancel)
EVT_LISTBOX(EDIT_BRUSH_LIST, EditBrushWindow::OnSelectionChanged)
EVT_LISTBOX(EDIT_BRUSH_COMPOSITE_TILE_LIST, EditBrushWindow::OnCompositeTileSelectionChanged)
EVT_SPINCTRL(EDIT_BRUSH_ITEM_ID, EditBrushWindow::OnItemIdChanged)
EVT_SPINCTRL(EDIT_BRUSH_TILE_X, EditBrushWindow::OnTilePositionChanged)
EVT_SPINCTRL(EDIT_BRUSH_TILE_Y, EditBrushWindow::OnTilePositionChanged)
EVT_SPINCTRL(EDIT_BRUSH_TILE_Z, EditBrushWindow::OnTilePositionChanged)
EVT_SPINCTRL(EDIT_BRUSH_CHANCE, EditBrushWindow::OnChanceChanged)
EVT_SPINCTRL(EDIT_BRUSH_BORDER_ID, EditBrushWindow::OnBorderSpinChanged)
EVT_SPINCTRL(EDIT_BRUSH_GROUND_EQUIV, EditBrushWindow::OnBorderSpinChanged)
EVT_BUTTON(EDIT_BRUSH_PICK_ITEM, EditBrushWindow::OnClickPickItem)
EVT_BUTTON(EDIT_BRUSH_PICK_BORDER, EditBrushWindow::OnClickPickBorder)
EVT_CHOICE(EDIT_BRUSH_BORDER_ALIGN, EditBrushWindow::OnBorderFieldChanged)
EVT_TEXT(EDIT_BRUSH_BORDER_TO, EditBrushWindow::OnBorderFieldChanged)
EVT_CHECKBOX(EDIT_BRUSH_BORDER_SUPER, EditBrushWindow::OnBorderFieldChanged)
END_EVENT_TABLE()

void OpenBrushEditor(Brush* brush) {
	if (!brush || !BrushCanBeEdited(brush)) {
		g_gui.PopupDialog("Cannot edit brush", "This brush type cannot be edited.", wxOK);
		return;
	}

	wxString error;
	std::vector<BrushEditEntry> entries;
	if (!BrushExtractEditEntries(brush, entries, error)) {
		g_gui.PopupDialog("Cannot edit brush", error, wxOK);
		return;
	}

	EditBrushWindow* window = newd EditBrushWindow(g_gui.root, brush);
	window->ShowModal();
	window->Destroy();
}

EditBrushWindow::EditBrushWindow(wxWindow* parent, Brush* brush) :
	ObjectPropertiesWindowBase(parent, "Edit Brush: " + wxstr(brush->getName())),
	edit_brush(brush),
	entry_list(nullptr),
	composite_panel(nullptr),
	composite_tile_list(nullptr),
	item_preview_button(nullptr),
	tile_x_spin(nullptr),
	tile_y_spin(nullptr),
	tile_z_spin(nullptr),
	tile_pos_label(nullptr),
	border_align_choice(nullptr),
	border_to_text(nullptr),
	border_id_spin(nullptr),
	border_super_check(nullptr),
	ground_equivalent_spin(nullptr),
	border_align_label(nullptr),
	border_to_label(nullptr),
	border_id_label(nullptr),
	ground_equivalent_label(nullptr),
	item_id_spin(nullptr),
	chance_spin(nullptr),
	pick_item_button(nullptr),
	pick_border_button(nullptr),
	item_name_label(nullptr),
	add_composite_button(nullptr),
	add_border_button(nullptr),
	add_optional_button(nullptr),
	add_tile_button(nullptr),
	remove_tile_button(nullptr) {
	wxString error;
	if (!BrushExtractEditEntries(edit_brush, entries, error)) {
		entries.clear();
	}

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	entry_list = newd EditBrushListBox(this, EDIT_BRUSH_LIST, this);
	entry_list->SetMinSize(FromDIP(wxSize(640, 220)));
	topsizer->Add(entry_list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 10));

	composite_panel = newd wxStaticBoxSizer(wxVERTICAL, this, "Composite Tiles");
	composite_tile_list = newd EditCompositeTileListBox(this, EDIT_BRUSH_COMPOSITE_TILE_LIST, this);
	composite_tile_list->SetMinSize(FromDIP(wxSize(620, 120)));
	composite_panel->Add(composite_tile_list, wxSizerFlags(1).Expand().Border(wxALL, 5));

	wxSizer* compositeButtonSizer = newd wxBoxSizer(wxHORIZONTAL);
	add_tile_button = newd wxButton(this, EDIT_BRUSH_ADD_TILE, "Add Tile");
	remove_tile_button = newd wxButton(this, EDIT_BRUSH_REMOVE_TILE, "Remove Tile");
	compositeButtonSizer->Add(add_tile_button, wxSizerFlags(0).Border(wxRIGHT, 5));
	compositeButtonSizer->Add(remove_tile_button, wxSizerFlags(0));
	composite_panel->Add(compositeButtonSizer, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 5));
	topsizer->Add(composite_panel, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));

	wxFlexGridSizer* editSizer = newd wxFlexGridSizer(3, 10, 10);
	editSizer->AddGrowableCol(1);
	editSizer->Add(newd wxStaticText(this, wxID_ANY, "Preview"), wxSizerFlags(1).CenterVertical());
	item_preview_button = newd DCButton(this, wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_32x32, 0);
	editSizer->Add(item_preview_button, wxSizerFlags(0).Center());
	editSizer->AddSpacer(0);

	tile_pos_label = newd wxStaticText(this, wxID_ANY, "Tile X/Y/Z");
	editSizer->Add(tile_pos_label, wxSizerFlags(1).CenterVertical());
	wxSizer* tilePosSizer = newd wxBoxSizer(wxHORIZONTAL);
	tile_x_spin = newd wxSpinCtrl(this, EDIT_BRUSH_TILE_X, "0", wxDefaultPosition, FromDIP(wxSize(55, -1)), wxSP_ARROW_KEYS, -32767, 32767, 0);
	tile_y_spin = newd wxSpinCtrl(this, EDIT_BRUSH_TILE_Y, "0", wxDefaultPosition, FromDIP(wxSize(55, -1)), wxSP_ARROW_KEYS, -32767, 32767, 0);
	tile_z_spin = newd wxSpinCtrl(this, EDIT_BRUSH_TILE_Z, "0", wxDefaultPosition, FromDIP(wxSize(45, -1)), wxSP_ARROW_KEYS, -7, 7, 0);
	tilePosSizer->Add(tile_x_spin, wxSizerFlags(0).Border(wxRIGHT, 4));
	tilePosSizer->Add(tile_y_spin, wxSizerFlags(0).Border(wxRIGHT, 4));
	tilePosSizer->Add(tile_z_spin, wxSizerFlags(0));
	editSizer->Add(tilePosSizer, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);

	border_align_label = newd wxStaticText(this, wxID_ANY, "Align");
	editSizer->Add(border_align_label, wxSizerFlags(1).CenterVertical());
	border_align_choice = newd wxChoice(this, EDIT_BRUSH_BORDER_ALIGN);
	border_align_choice->Append("outer");
	border_align_choice->Append("inner");
	editSizer->Add(border_align_choice, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);

	border_to_label = newd wxStaticText(this, wxID_ANY, "To");
	editSizer->Add(border_to_label, wxSizerFlags(1).CenterVertical());
	border_to_text = newd wxTextCtrl(this, EDIT_BRUSH_BORDER_TO, "all");
	editSizer->Add(border_to_text, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);

	border_id_label = newd wxStaticText(this, wxID_ANY, "Border ID");
	editSizer->Add(border_id_label, wxSizerFlags(1).CenterVertical());
	wxSizer* borderIdSizer = newd wxBoxSizer(wxHORIZONTAL);
	border_id_spin = newd wxSpinCtrl(this, EDIT_BRUSH_BORDER_ID, "0", wxDefaultPosition, FromDIP(wxSize(90, -1)), wxSP_ARROW_KEYS, 0, 10000, 0);
	borderIdSizer->Add(border_id_spin, wxSizerFlags(1).Expand());
	pick_border_button = newd wxButton(this, EDIT_BRUSH_PICK_BORDER, "Pick...", wxDefaultPosition, FromDIP(wxSize(60, -1)));
	borderIdSizer->Add(pick_border_button, wxSizerFlags(0).Left().Border(wxLEFT, 5));
	editSizer->Add(borderIdSizer, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);

	ground_equivalent_label = newd wxStaticText(this, wxID_ANY, "Ground equiv.");
	editSizer->Add(ground_equivalent_label, wxSizerFlags(1).CenterVertical());
	ground_equivalent_spin = newd wxSpinCtrl(this, EDIT_BRUSH_GROUND_EQUIV, "0", wxDefaultPosition, FromDIP(wxSize(90, -1)), wxSP_ARROW_KEYS, 0, 65535, 0);
	editSizer->Add(ground_equivalent_spin, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);

	border_super_check = newd wxCheckBox(this, EDIT_BRUSH_BORDER_SUPER, "Super border");
	editSizer->AddSpacer(0);
	editSizer->Add(border_super_check, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);

	editSizer->Add(newd wxStaticText(this, wxID_ANY, "Item ID"), wxSizerFlags(1).CenterVertical());
	wxSizer* itemIdSizer = newd wxBoxSizer(wxHORIZONTAL);
	item_id_spin = newd wxSpinCtrl(this, EDIT_BRUSH_ITEM_ID, "0", wxDefaultPosition, FromDIP(wxSize(90, -1)), wxSP_ARROW_KEYS, 1, 65535, 100);
	itemIdSizer->Add(item_id_spin, wxSizerFlags(1).Expand());
	pick_item_button = newd wxButton(this, EDIT_BRUSH_PICK_ITEM, "Pick...", wxDefaultPosition, FromDIP(wxSize(60, -1)));
	itemIdSizer->Add(pick_item_button, wxSizerFlags(0).Left().Border(wxLEFT, 5));
	editSizer->Add(itemIdSizer, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);

	editSizer->Add(newd wxStaticText(this, wxID_ANY, "Chance"), wxSizerFlags(1).CenterVertical());
	chance_spin = newd wxSpinCtrl(this, EDIT_BRUSH_CHANCE, "1", wxDefaultPosition, FromDIP(wxSize(80, -1)), wxSP_ARROW_KEYS, 0, 100000, 1);
	editSizer->Add(chance_spin, wxSizerFlags(1).Expand());
	editSizer->AddSpacer(0);
	topsizer->Add(editSizer, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT, 10));
	item_name_label = newd wxStaticText(this, wxID_ANY, "Select an entry to edit.");
	topsizer->Add(item_name_label, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 10));

	wxSizer* buttonSizer = newd wxBoxSizer(wxHORIZONTAL);
	buttonSizer->Add(newd wxButton(this, EDIT_BRUSH_ADD_ITEM, "Add Item"), wxSizerFlags(0).Border(wxALL, 5));
	add_composite_button = newd wxButton(this, EDIT_BRUSH_ADD_COMPOSITE, "Add Composite");
	buttonSizer->Add(add_composite_button, wxSizerFlags(0).Border(wxALL, 5));
	add_border_button = newd wxButton(this, EDIT_BRUSH_ADD_BORDER, "Add Border");
	buttonSizer->Add(add_border_button, wxSizerFlags(0).Border(wxALL, 5));
	add_optional_button = newd wxButton(this, EDIT_BRUSH_ADD_OPTIONAL, "Add Optional");
	buttonSizer->Add(add_optional_button, wxSizerFlags(0).Border(wxALL, 5));
	buttonSizer->Add(newd wxButton(this, EDIT_BRUSH_REMOVE_ENTRY, "Remove Entry"), wxSizerFlags(0).Border(wxALL, 5));
	buttonSizer->AddStretchSpacer();
	buttonSizer->Add(newd wxButton(this, wxID_SAVE, "Save"), wxSizerFlags(0).Border(wxALL, 5));
	buttonSizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), wxSizerFlags(0).Border(wxALL, 5));
	topsizer->Add(buttonSizer, wxSizerFlags(0).Expand().Border(wxALL, 10));

	if (!edit_brush->isDoodad()) {
		add_composite_button->Hide();
		composite_panel->Show(false);
	}
	if (!edit_brush->isGround()) {
		add_border_button->Hide();
		add_optional_button->Hide();
	}

	SetSizerAndFit(topsizer);
	Centre(wxBOTH);
	RefreshList();
}

BrushEditEntry* EditBrushWindow::GetSelectedEntry() {
	const long selected = entry_list->GetSelection();
	if (selected == wxNOT_FOUND || static_cast<size_t>(selected) >= entries.size()) {
		return nullptr;
	}
	return &entries[selected];
}

const BrushEditEntry* EditBrushWindow::GetSelectedEntry() const {
	const long selected = entry_list->GetSelection();
	if (selected == wxNOT_FOUND || static_cast<size_t>(selected) >= entries.size()) {
		return nullptr;
	}
	return &entries[selected];
}

std::vector<std::pair<Position, uint16_t>>* EditBrushWindow::GetSelectedCompositeTiles() {
	BrushEditEntry* entry = GetSelectedEntry();
	if (!entry || entry->kind != BRUSH_EDIT_COMPOSITE) {
		return nullptr;
	}
	return &entry->composite_tiles;
}

wxString EditBrushWindow::FormatEntryLabel(const BrushEditEntry& entry) const {
	if (entry.kind == BRUSH_EDIT_COMPOSITE) {
		return wxString::Format("Composite  |  chance %d  |  %zu tile(s)", entry.chance, entry.composite_tiles.size());
	}
	if (entry.kind == BRUSH_EDIT_GROUND_BORDER) {
		return wxString::Format("Border  |  %s  |  to: %s  |  id: %u%s",
			entry.border_outer ? "outer" : "inner",
			wxstr(entry.border_to),
			entry.border_id,
			entry.border_super ? "  |  super" : "");
	}
	if (entry.kind == BRUSH_EDIT_GROUND_OPTIONAL) {
		return wxString::Format("Optional border  |  id: %u", entry.border_id);
	}

	const ItemType& it = g_items.getItemType(entry.item_id);
	wxString name = it.id != 0 ? wxstr(it.name) : wxString("Unknown");
	if (edit_brush->isWall()) {
		return wxString::Format("%s  |  ID %u  |  %s  |  chance %d", wxstr(entry.wall_alignment), entry.item_id, name, entry.chance);
	}
	return wxString::Format("Item  |  ID %u  |  %s  |  chance %d", entry.item_id, name, entry.chance);
}

wxString EditBrushWindow::FormatCompositeTileLabel(const std::pair<Position, uint16_t>& tile) const {
	const ItemType& it = g_items.getItemType(tile.second);
	const wxString name = it.id != 0 ? wxstr(it.name) : wxString("Unknown");
	return wxString::Format("(%d, %d, %d)  |  ID %u  |  %s", tile.first.x, tile.first.y, tile.first.z, tile.second, name);
}

void EditBrushWindow::RefreshList() {
	const long selected = entry_list->GetSelection();
	entry_list->SetItemCount(entries.size());
	if (selected != wxNOT_FOUND && static_cast<size_t>(selected) < entries.size()) {
		entry_list->SetSelection(selected);
	}
	entry_list->Refresh();
	RefreshCompositeTileList();
	UpdateEditFields();
}

void EditBrushWindow::RefreshCompositeTileList() {
	if (!composite_tile_list) {
		return;
	}

	const long tileSelection = composite_tile_list->GetSelection();
	std::vector<std::pair<Position, uint16_t>>* tiles = GetSelectedCompositeTiles();
	const size_t tileCount = tiles ? tiles->size() : 0;
	composite_tile_list->SetItemCount(tileCount);
	if (tileSelection != wxNOT_FOUND && static_cast<size_t>(tileSelection) < tileCount) {
		composite_tile_list->SetSelection(tileSelection);
	}
	composite_tile_list->Refresh();
	UpdateCompositePanelVisibility();
}

void EditBrushWindow::UpdateCompositePanelVisibility() {
	const BrushEditEntry* entry = GetSelectedEntry();
	const bool compositeSelected = entry && entry->kind == BRUSH_EDIT_COMPOSITE;
	if (edit_brush->isDoodad()) {
		composite_panel->Show(compositeSelected);
		add_tile_button->Enable(compositeSelected);
		remove_tile_button->Enable(compositeSelected && composite_tile_list->GetSelection() != wxNOT_FOUND);
		Layout();
	}
}

void EditBrushWindow::UpdatePreviewSprite(const BrushEditEntry* entry, const std::pair<Position, uint16_t>* tile) {
	if (!item_preview_button) {
		return;
	}
	if (tile) {
		item_preview_button->SetSprite(GetTilePreviewClientId(*tile));
		return;
	}
	if (!entry) {
		item_preview_button->SetSprite(0);
		return;
	}
	item_preview_button->SetSprite(GetEntryPreviewClientId(*entry));
}

void EditBrushWindow::UpdateEditFields() {
	const BrushEditEntry* entry = GetSelectedEntry();
	const bool showBorderFields = entry && entry->kind == BRUSH_EDIT_GROUND_BORDER;
	const bool showOptionalFields = entry && entry->kind == BRUSH_EDIT_GROUND_OPTIONAL;

	border_align_label->Enable(showBorderFields);
	border_align_choice->Enable(showBorderFields);
	border_to_label->Enable(showBorderFields);
	border_to_text->Enable(showBorderFields);
	border_id_label->Enable(showBorderFields || showOptionalFields);
	border_id_spin->Enable(showBorderFields || showOptionalFields);
	pick_border_button->Enable(showBorderFields || showOptionalFields);
	border_super_check->Enable(showBorderFields);
	ground_equivalent_label->Enable(showBorderFields);
	ground_equivalent_spin->Enable(showBorderFields);

	if (!entry) {
		item_id_spin->Disable();
		pick_item_button->Disable();
		pick_border_button->Disable();
		chance_spin->Disable();
		tile_x_spin->Disable();
		tile_y_spin->Disable();
		tile_z_spin->Disable();
		tile_pos_label->Disable();
		UpdatePreviewSprite(nullptr);
		item_name_label->SetLabel("Select an entry to edit.");
		UpdateCompositePanelVisibility();
		return;
	}

	const bool showItemFields = entry->kind == BRUSH_EDIT_ITEM;
	const bool showChance = entry->kind == BRUSH_EDIT_ITEM || entry->kind == BRUSH_EDIT_COMPOSITE;
	chance_spin->Enable(showChance);
	if (showChance) {
		chance_spin->SetValue(entry->chance);
	}

	std::vector<std::pair<Position, uint16_t>>* tiles = GetSelectedCompositeTiles();
	const long tileIndex = composite_tile_list ? composite_tile_list->GetSelection() : wxNOT_FOUND;
	const bool editingCompositeTile = entry->kind == BRUSH_EDIT_COMPOSITE && tiles && tileIndex != wxNOT_FOUND && static_cast<size_t>(tileIndex) < tiles->size();

	if (showBorderFields) {
		border_align_choice->SetSelection(entry->border_outer ? 0 : 1);
		border_to_text->ChangeValue(wxstr(entry->border_to));
		border_id_spin->SetValue(entry->border_id);
		border_super_check->SetValue(entry->border_super);
		ground_equivalent_spin->SetValue(entry->ground_equivalent);
		item_id_spin->Disable();
		pick_item_button->Disable();
		pick_border_button->Enable();
		tile_x_spin->Disable();
		tile_y_spin->Disable();
		tile_z_spin->Disable();
		tile_pos_label->Disable();
		item_name_label->SetLabel(wxString::Format("Editing %s border to '%s' (border id %u).",
			entry->border_outer ? "outer" : "inner", wxstr(entry->border_to), entry->border_id));
		UpdatePreviewSprite(entry);
	} else if (showOptionalFields) {
		border_id_spin->SetValue(entry->border_id);
		item_id_spin->Disable();
		pick_item_button->Disable();
		pick_border_button->Enable();
		tile_x_spin->Disable();
		tile_y_spin->Disable();
		tile_z_spin->Disable();
		tile_pos_label->Disable();
		item_name_label->SetLabel(wxString::Format("Editing optional border id %u.", entry->border_id));
		UpdatePreviewSprite(entry);
	} else if (showItemFields) {
		item_id_spin->Enable();
		pick_item_button->Enable();
		pick_border_button->Disable();
		item_id_spin->SetValue(entry->item_id);
		tile_x_spin->Disable();
		tile_y_spin->Disable();
		tile_z_spin->Disable();
		tile_pos_label->Disable();
		const ItemType& it = g_items.getItemType(entry->item_id);
		item_name_label->SetLabel(it.id != 0 ? wxString::Format("Editing item %u: %s", entry->item_id, wxstr(it.name)) : wxString::Format("Editing item %u", entry->item_id));
		UpdatePreviewSprite(entry);
	} else if (editingCompositeTile) {
		auto& tile = tiles->at(tileIndex);
		item_id_spin->Enable();
		pick_item_button->Enable();
		pick_border_button->Disable();
		item_id_spin->SetValue(tile.second);
		tile_x_spin->Enable();
		tile_y_spin->Enable();
		tile_z_spin->Enable();
		tile_pos_label->Enable();
		tile_x_spin->SetValue(tile.first.x);
		tile_y_spin->SetValue(tile.first.y);
		tile_z_spin->SetValue(tile.first.z);
		const ItemType& it = g_items.getItemType(tile.second);
		item_name_label->SetLabel(it.id != 0 ?
			wxString::Format("Editing composite tile (%d, %d, %d): item %u (%s)", tile.first.x, tile.first.y, tile.first.z, tile.second, wxstr(it.name)) :
			wxString::Format("Editing composite tile (%d, %d, %d): item %u", tile.first.x, tile.first.y, tile.first.z, tile.second));
		UpdatePreviewSprite(entry, &tile);
	} else {
		item_id_spin->Disable();
		pick_item_button->Disable();
		pick_border_button->Disable();
		tile_x_spin->Disable();
		tile_y_spin->Disable();
		tile_z_spin->Disable();
		tile_pos_label->Disable();
		item_name_label->SetLabel(wxString::Format("Editing composite with %zu tile(s). Select a tile below to edit it.", entry->composite_tiles.size()));
		UpdatePreviewSprite(entry);
	}

	UpdateCompositePanelVisibility();
}

void EditBrushWindow::ApplyBorderFieldsFromControls() {
	BrushEditEntry* entry = GetSelectedEntry();
	if (!entry) {
		return;
	}

	if (entry->kind == BRUSH_EDIT_GROUND_BORDER) {
		entry->border_outer = border_align_choice->GetSelection() == 0;
		entry->border_to = border_to_text->GetValue().ToStdString();
		if (entry->border_to.empty()) {
			entry->border_to = "all";
		}
		entry->border_id = border_id_spin->GetValue();
		entry->border_super = border_super_check->GetValue();
		entry->ground_equivalent = static_cast<uint16_t>(ground_equivalent_spin->GetValue());
		RefreshList();
	} else if (entry->kind == BRUSH_EDIT_GROUND_OPTIONAL) {
		entry->border_id = border_id_spin->GetValue();
		RefreshList();
	}
}

void EditBrushWindow::OnBorderFieldChanged(wxCommandEvent& WXUNUSED(event)) {
	ApplyBorderFieldsFromControls();
}

void EditBrushWindow::OnBorderSpinChanged(wxSpinEvent& WXUNUSED(event)) {
	ApplyBorderFieldsFromControls();
}

void EditBrushWindow::SetSelectedItemId(uint16_t itemId) {
	BrushEditEntry* entry = GetSelectedEntry();
	if (!entry) {
		return;
	}

	if (itemId == 0 || g_items.getItemType(itemId).id == 0) {
		g_gui.PopupDialog("Invalid item", "Please choose a valid item ID.", wxOK);
		UpdateEditFields();
		return;
	}

	if (entry->kind == BRUSH_EDIT_ITEM) {
		entry->item_id = itemId;
		RefreshList();
		return;
	}

	std::vector<std::pair<Position, uint16_t>>* tiles = GetSelectedCompositeTiles();
	const long tileIndex = composite_tile_list->GetSelection();
	if (!tiles || tileIndex == wxNOT_FOUND || static_cast<size_t>(tileIndex) >= tiles->size()) {
		return;
	}

	tiles->at(tileIndex).second = itemId;
	RefreshList();
	composite_tile_list->SetSelection(tileIndex);
}

void EditBrushWindow::OnSelectionChanged(wxCommandEvent& WXUNUSED(event)) {
	if (composite_tile_list) {
		composite_tile_list->SetSelection(wxNOT_FOUND);
	}
	RefreshCompositeTileList();
	UpdateEditFields();
}

void EditBrushWindow::OnCompositeTileSelectionChanged(wxCommandEvent& WXUNUSED(event)) {
	UpdateEditFields();
}

void EditBrushWindow::OnItemIdChanged(wxSpinEvent& WXUNUSED(event)) {
	BrushEditEntry* entry = GetSelectedEntry();
	if (!entry) {
		return;
	}

	const uint16_t itemId = static_cast<uint16_t>(item_id_spin->GetValue());

	if (entry->kind == BRUSH_EDIT_ITEM) {
		if (entry->item_id == itemId) {
			return;
		}
		SetSelectedItemId(itemId);
		return;
	}

	std::vector<std::pair<Position, uint16_t>>* tiles = GetSelectedCompositeTiles();
	const long tileIndex = composite_tile_list->GetSelection();
	if (!tiles || tileIndex == wxNOT_FOUND || static_cast<size_t>(tileIndex) >= tiles->size()) {
		return;
	}
	if (tiles->at(tileIndex).second == itemId) {
		return;
	}
	SetSelectedItemId(itemId);
}

void EditBrushWindow::OnTilePositionChanged(wxSpinEvent& WXUNUSED(event)) {
	std::vector<std::pair<Position, uint16_t>>* tiles = GetSelectedCompositeTiles();
	const long tileIndex = composite_tile_list->GetSelection();
	if (!tiles || tileIndex == wxNOT_FOUND || static_cast<size_t>(tileIndex) >= tiles->size()) {
		return;
	}

	auto& tile = tiles->at(tileIndex);
	const Position newPos(tile_x_spin->GetValue(), tile_y_spin->GetValue(), tile_z_spin->GetValue());
	if (tile.first == newPos) {
		return;
	}

	tile.first = newPos;
	RefreshList();
	composite_tile_list->SetSelection(tileIndex);
}

void EditBrushWindow::OnClickPickItem(wxCommandEvent& WXUNUSED(event)) {
	BrushEditEntry* entry = GetSelectedEntry();
	if (!entry) {
		return;
	}
	if (entry->kind == BRUSH_EDIT_COMPOSITE && composite_tile_list->GetSelection() == wxNOT_FOUND) {
		return;
	}

	FindItemDialog dialog(this, "Item");
	if (dialog.ShowModal() == wxID_OK) {
		SetSelectedItemId(dialog.getResultID());
	}
	dialog.Destroy();
}

void EditBrushWindow::OnClickPickBorder(wxCommandEvent& WXUNUSED(event)) {
	BrushEditEntry* entry = GetSelectedEntry();
	if (!entry) {
		return;
	}
	if (entry->kind != BRUSH_EDIT_GROUND_BORDER && entry->kind != BRUSH_EDIT_GROUND_OPTIONAL) {
		return;
	}

	FindBorderDialog dialog(this, entry->border_id);
	if (dialog.ShowModal() == wxID_OK) {
		border_id_spin->SetValue(static_cast<int>(dialog.getResultId()));
		ApplyBorderFieldsFromControls();
		UpdateEditFields();
	}
	dialog.Destroy();
}

void EditBrushWindow::OnChanceChanged(wxSpinEvent& WXUNUSED(event)) {
	BrushEditEntry* entry = GetSelectedEntry();
	if (!entry) {
		return;
	}

	const int chance = chance_spin->GetValue();
	if (entry->chance == chance) {
		return;
	}

	entry->chance = chance;
	RefreshList();
}

void EditBrushWindow::OnClickAddItem(wxCommandEvent& WXUNUSED(event)) {
	if (!edit_brush->isDoodad() && !edit_brush->isGround() && !edit_brush->isWall()) {
		return;
	}

	FindItemDialog dialog(this, "Item");
	if (dialog.ShowModal() != wxID_OK) {
		dialog.Destroy();
		return;
	}

	const uint16_t itemId = dialog.getResultID();
	dialog.Destroy();
	if (itemId == 0) {
		return;
	}

	BrushEditEntry entry;
	entry.kind = BRUSH_EDIT_ITEM;
	entry.item_id = itemId;
	entry.chance = 1;
	if (edit_brush->isWall()) {
		const BrushEditEntry* selected = GetSelectedEntry();
		if (selected && !selected->wall_alignment.empty()) {
			entry.wall_alignment = selected->wall_alignment;
		} else if (!entries.empty()) {
			entry.wall_alignment = entries.back().wall_alignment;
		} else {
			entry.wall_alignment = "horizontal";
		}
	}

	entries.push_back(entry);
	RefreshList();
	entry_list->SetSelection(entry_list->GetItemCount() - 1);
}

void EditBrushWindow::OnClickAddComposite(wxCommandEvent& WXUNUSED(event)) {
	if (!edit_brush->isDoodad()) {
		return;
	}

	FindItemDialog dialog(this, "Item");
	if (dialog.ShowModal() != wxID_OK) {
		dialog.Destroy();
		return;
	}

	const uint16_t itemId = dialog.getResultID();
	dialog.Destroy();
	if (itemId == 0) {
		return;
	}

	BrushEditEntry entry;
	entry.kind = BRUSH_EDIT_COMPOSITE;
	entry.chance = 1;
	entry.composite_tiles.push_back(std::make_pair(Position(0, 0, 0), itemId));
	entries.push_back(entry);
	RefreshList();
	entry_list->SetSelection(entry_list->GetItemCount() - 1);
	if (composite_tile_list) {
		composite_tile_list->SetSelection(0);
	}
	UpdateEditFields();
}

void EditBrushWindow::OnClickAddBorder(wxCommandEvent& WXUNUSED(event)) {
	if (!edit_brush->isGround()) {
		return;
	}

	BrushEditEntry entry;
	entry.kind = BRUSH_EDIT_GROUND_BORDER;
	entry.border_outer = true;
	entry.border_to = "none";
	entry.border_id = 7;
	entries.push_back(entry);
	RefreshList();
	entry_list->SetSelection(entry_list->GetItemCount() - 1);
}

void EditBrushWindow::OnClickAddOptional(wxCommandEvent& WXUNUSED(event)) {
	if (!edit_brush->isGround()) {
		return;
	}

	for (const BrushEditEntry& existing : entries) {
		if (existing.kind == BRUSH_EDIT_GROUND_OPTIONAL) {
			g_gui.PopupDialog("Optional border exists", "This brush already has an optional border entry. Edit or remove it first.", wxOK);
			return;
		}
	}

	BrushEditEntry entry;
	entry.kind = BRUSH_EDIT_GROUND_OPTIONAL;
	entry.border_id = 7;
	entries.push_back(entry);
	RefreshList();
	entry_list->SetSelection(entry_list->GetItemCount() - 1);
}

void EditBrushWindow::OnClickAddTile(wxCommandEvent& WXUNUSED(event)) {
	std::vector<std::pair<Position, uint16_t>>* tiles = GetSelectedCompositeTiles();
	if (!tiles) {
		return;
	}

	int nextX = 0;
	int nextY = 0;
	if (!tiles->empty()) {
		for (const auto& tile : *tiles) {
			nextX = std::max(nextX, tile.first.x + 1);
			nextY = std::max(nextY, tile.first.y);
		}
	}

	FindItemDialog dialog(this, "Item");
	if (dialog.ShowModal() != wxID_OK) {
		dialog.Destroy();
		return;
	}

	const uint16_t itemId = dialog.getResultID();
	dialog.Destroy();
	if (itemId == 0) {
		return;
	}

	tiles->push_back(std::make_pair(Position(nextX, nextY, 0), itemId));
	RefreshList();
	composite_tile_list->SetSelection(tiles->size() - 1);
	UpdateEditFields();
}

void EditBrushWindow::OnClickRemoveTile(wxCommandEvent& WXUNUSED(event)) {
	std::vector<std::pair<Position, uint16_t>>* tiles = GetSelectedCompositeTiles();
	const long tileIndex = composite_tile_list->GetSelection();
	if (!tiles || tileIndex == wxNOT_FOUND || static_cast<size_t>(tileIndex) >= tiles->size()) {
		return;
	}

	tiles->erase(tiles->begin() + tileIndex);
	if (tiles->empty()) {
		g_gui.PopupDialog("Composite empty", "A composite must contain at least one tile.", wxOK);
		const long entryIndex = entry_list->GetSelection();
		if (entryIndex != wxNOT_FOUND) {
			entries.erase(entries.begin() + entryIndex);
		}
	}
	RefreshList();
}

void EditBrushWindow::OnClickRemoveEntry(wxCommandEvent& WXUNUSED(event)) {
	const long selected = entry_list->GetSelection();
	if (selected == wxNOT_FOUND || static_cast<size_t>(selected) >= entries.size()) {
		return;
	}

	entries.erase(entries.begin() + selected);
	RefreshList();
}

void EditBrushWindow::OnClickSave(wxCommandEvent& WXUNUSED(event)) {
	wxString error;
	if (!BrushApplyEditEntries(edit_brush, entries, error)) {
		g_gui.PopupDialog("Could not save brush", error, wxOK);
		return;
	}

	if (!BrushSaveToXml(edit_brush, error)) {
		g_gui.PopupDialog("Could not save brush", error, wxOK);
		return;
	}

	g_materials.modify();
	g_gui.PopupDialog("Brush saved", wxString::Format("Saved brush '%s' to XML.", wxstr(edit_brush->getName())), wxOK);
	EndModal(wxID_OK);
}

void EditBrushWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}
