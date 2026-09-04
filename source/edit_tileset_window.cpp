//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "edit_tileset_window.h"
#include "find_item_window.h"
#include "gui.h"
#include "graphics.h"
#include "items.h"
#include "materials.h"
#include "settings.h"
#include "tileset_edit.h"
#include "theme.h"

namespace {
enum {
	EDIT_TILESET_LIST = 41001,
	EDIT_TILESET_ITEM_ID,
	EDIT_TILESET_PICK_ITEM,
	EDIT_TILESET_ADD_ITEM,
	EDIT_TILESET_ADD_SEPARATOR,
	EDIT_TILESET_REMOVE_ENTRY,
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

uint16_t GetEntryPreviewClientId(const TilesetEditEntry& entry) {
	if (entry.kind == TilesetEditEntry::SEPARATOR) {
		return 0;
	}
	if (entry.kind == TilesetEditEntry::ITEM) {
		const ItemType& it = g_items.getItemType(entry.item_id);
		return it.id != 0 ? it.clientID : 0;
	}
	Brush* brush = g_brushes.getBrush(entry.brush_name);
	return brush ? brush->getLookID() : 0;
}

} // namespace

EditTilesetListBox::EditTilesetListBox(wxWindow* parent, wxWindowID id, EditTilesetWindow* owner) :
	wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	owner(owner) {
	////
}

void EditTilesetListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const {
	if (!owner || n >= owner->entries.size()) {
		return;
	}

	const TilesetEditEntry& entry = owner->entries[n];
	if (entry.kind != TilesetEditEntry::SEPARATOR) {
		DrawItemSprite(dc, GetEntryPreviewClientId(entry), rect.GetX() + 2, rect.GetY() + 2, 32, 32);
	} else {
		dc.SetPen(wxPen(ThemeManager::Get().GetPalette().mutedText));
		dc.DrawLine(rect.GetX() + 4, rect.GetY() + rect.GetHeight() / 2, rect.GetRight() - 4, rect.GetY() + rect.GetHeight() / 2);
	}

	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);

	dc.DrawText(owner->FormatEntryLabel(entry), rect.GetX() + 40, rect.GetY() + 10);
}

wxCoord EditTilesetListBox::OnMeasureItem(size_t WXUNUSED(n)) const {
	return 36;
}

BEGIN_EVENT_TABLE(EditTilesetWindow, wxDialog)
EVT_BUTTON(EDIT_TILESET_ADD_ITEM, EditTilesetWindow::OnClickAddItem)
EVT_BUTTON(EDIT_TILESET_ADD_SEPARATOR, EditTilesetWindow::OnClickAddSeparator)
EVT_BUTTON(EDIT_TILESET_REMOVE_ENTRY, EditTilesetWindow::OnClickRemoveEntry)
EVT_BUTTON(wxID_SAVE, EditTilesetWindow::OnClickSave)
EVT_BUTTON(wxID_CANCEL, EditTilesetWindow::OnClickCancel)
EVT_LISTBOX(EDIT_TILESET_LIST, EditTilesetWindow::OnSelectionChanged)
EVT_SPINCTRL(EDIT_TILESET_ITEM_ID, EditTilesetWindow::OnItemIdChanged)
EVT_BUTTON(EDIT_TILESET_PICK_ITEM, EditTilesetWindow::OnClickPickItem)
END_EVENT_TABLE()

void OpenTilesetEditor(Tileset* tileset, TilesetCategoryType categoryType) {
	if (!g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
		return;
	}
	if (!tileset) {
		return;
	}

	EditTilesetWindow* window = newd EditTilesetWindow(g_gui.root, tileset, categoryType);
	window->ShowModal();
	window->Destroy();
}

EditTilesetWindow::EditTilesetWindow(wxWindow* parent, Tileset* tileset, TilesetCategoryType categoryType) :
	ObjectPropertiesWindowBase(parent, "Edit Tileset: " + wxstr(tileset->name)),
	edit_tileset(tileset),
	category_type(categoryType),
	entry_list(nullptr),
	item_preview_button(nullptr),
	item_id_spin(nullptr),
	pick_item_button(nullptr),
	item_name_label(nullptr) {
	const TilesetCategory* category = tileset->getCategory(categoryType);
	wxString extractWarnings;
	TilesetExtractEditEntries(category, entries, extractWarnings);

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* list_box = newd wxStaticBoxSizer(wxVERTICAL, this, "Tileset entries");
	entry_list = newd EditTilesetListBox(this, EDIT_TILESET_LIST, this);
	entry_list->SetMinSize(FromDIP(wxSize(360, 240)));
	list_box->Add(entry_list, 1, wxEXPAND | wxALL, 5);
	topsizer->Add(list_box, 1, wxEXPAND | wxALL, 10);

	wxStaticBoxSizer* edit_box = newd wxStaticBoxSizer(wxVERTICAL, this, "Selected item");
	wxFlexGridSizer* edit_grid = newd wxFlexGridSizer(2, 10, 10);
	edit_grid->AddGrowableCol(1);

	item_preview_button = newd DCButton(this, wxID_ANY, wxDefaultPosition, DC_BTN_TOGGLE, RENDER_SIZE_32x32, 0);
	edit_grid->Add(item_preview_button, 0, wxALIGN_CENTER_VERTICAL);

	item_name_label = newd wxStaticText(this, wxID_ANY, "\"None\"");
	edit_grid->Add(item_name_label, 0, wxALIGN_CENTER_VERTICAL);

	edit_grid->Add(newd wxStaticText(this, wxID_ANY, "Item ID"), 0, wxALIGN_CENTER_VERTICAL);
	item_id_spin = newd wxSpinCtrl(this, EDIT_TILESET_ITEM_ID, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 100, 65535, 0);
	edit_grid->Add(item_id_spin, 1, wxEXPAND);

	edit_grid->Add(newd wxStaticText(this, wxID_ANY, ""), 0);
	pick_item_button = newd wxButton(this, EDIT_TILESET_PICK_ITEM, "Pick item...");
	edit_grid->Add(pick_item_button, 1, wxEXPAND);

	edit_box->Add(edit_grid, 0, wxEXPAND | wxALL, 5);
	topsizer->Add(edit_box, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

	wxSizer* button_row = newd wxBoxSizer(wxHORIZONTAL);
	button_row->Add(newd wxButton(this, EDIT_TILESET_ADD_ITEM, "Add item"), 0, wxALL, 5);
	button_row->Add(newd wxButton(this, EDIT_TILESET_ADD_SEPARATOR, "Add separator"), 0, wxALL, 5);
	button_row->Add(newd wxButton(this, EDIT_TILESET_REMOVE_ENTRY, "Remove"), 0, wxALL, 5);
	button_row->AddStretchSpacer();
	button_row->Add(newd wxButton(this, wxID_SAVE, "Save"), 0, wxALL, 5);
	button_row->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), 0, wxALL, 5);
	topsizer->Add(button_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	SetSizerAndFit(topsizer);
	Centre(wxBOTH);

	RefreshList();
}

wxString EditTilesetWindow::FormatEntryLabel(const TilesetEditEntry& entry) const {
	if (entry.kind == TilesetEditEntry::SEPARATOR) {
		return "Separator";
	}
	if (entry.kind == TilesetEditEntry::ITEM) {
		const ItemType& it = g_items.getItemType(entry.item_id);
		if (it.id != 0) {
			return wxString::Format("Item %u - %s", entry.item_id, wxstr(it.name));
		}
		return wxString::Format("Item %u", entry.item_id);
	}
	return wxString::Format("Brush %s", wxstr(entry.brush_name));
}

void EditTilesetWindow::RefreshList() {
	entry_list->SetItemCount(entries.size());
	if (!entries.empty()) {
		if (entry_list->GetSelection() == wxNOT_FOUND) {
			entry_list->SetSelection(0);
		}
	} else {
		entry_list->SetSelection(wxNOT_FOUND);
	}
	entry_list->Refresh();
	UpdateEditFields();
}

void EditTilesetWindow::UpdateEditFields() {
	const long selected = entry_list->GetSelection();
	const bool hasSelection = selected != wxNOT_FOUND && static_cast<size_t>(selected) < entries.size();
	const TilesetEditEntry* entry = hasSelection ? &entries[selected] : nullptr;
	const bool isItem = entry && entry->kind == TilesetEditEntry::ITEM;
	const bool isSeparator = entry && entry->kind == TilesetEditEntry::SEPARATOR;

	item_id_spin->Enable(isItem);
	pick_item_button->Enable(isItem);

	if (isSeparator) {
		item_id_spin->SetValue(0);
		item_name_label->SetLabelText("\"Separator\"");
		item_preview_button->SetSprite(0);
	} else if (isItem) {
		item_id_spin->SetValue(entry->item_id);
		const ItemType& it = g_items.getItemType(entry->item_id);
		if (it.id != 0) {
			item_name_label->SetLabelText("\"" + wxstr(it.name) + "\"");
			item_preview_button->SetSprite(it.clientID);
		} else {
			item_name_label->SetLabelText("\"Unknown item\"");
			item_preview_button->SetSprite(0);
		}
	} else if (entry) {
		item_id_spin->SetValue(0);
		Brush* brush = g_brushes.getBrush(entry->brush_name);
		if (brush) {
			item_name_label->SetLabelText("Brush \"" + wxstr(entry->brush_name) + "\"");
			item_preview_button->SetSprite(brush->getLookID());
		} else {
			item_name_label->SetLabelText("Brush \"" + wxstr(entry->brush_name) + "\"");
			item_preview_button->SetSprite(0);
		}
	} else {
		item_id_spin->SetValue(0);
		item_name_label->SetLabelText("\"None\"");
		item_preview_button->SetSprite(0);
	}
}

void EditTilesetWindow::SetSelectedItemId(uint16_t itemId) {
	const long selected = entry_list->GetSelection();
	if (selected == wxNOT_FOUND || static_cast<size_t>(selected) >= entries.size()) {
		return;
	}
	if (entries[selected].kind != TilesetEditEntry::ITEM) {
		return;
	}

	entries[selected].item_id = itemId;
	RefreshList();
	entry_list->SetSelection(selected);
}

void EditTilesetWindow::OnSelectionChanged(wxCommandEvent& WXUNUSED(event)) {
	UpdateEditFields();
}

void EditTilesetWindow::OnItemIdChanged(wxSpinEvent& event) {
	SetSelectedItemId(static_cast<uint16_t>(event.GetValue()));
}

void EditTilesetWindow::OnClickPickItem(wxCommandEvent& WXUNUSED(event)) {
	FindItemDialog dialog(this, "Item");
	if (dialog.ShowModal() != wxID_OK) {
		dialog.Destroy();
		return;
	}

	const uint16_t itemId = dialog.getResultID();
	dialog.Destroy();
	if (itemId != 0) {
		SetSelectedItemId(itemId);
	}
}

void EditTilesetWindow::OnClickAddItem(wxCommandEvent& WXUNUSED(event)) {
	FindItemDialog dialog(this, "Item");
	if (dialog.ShowModal() != wxID_OK) {
		dialog.Destroy();
		return;
	}

	const uint16_t itemId = dialog.getResultID();
	dialog.Destroy();
	if (itemId == 0 || g_items.getItemType(itemId).id == 0) {
		return;
	}

	for (const TilesetEditEntry& entry : entries) {
		if (entry.kind == TilesetEditEntry::ITEM && entry.item_id == itemId) {
			g_gui.PopupDialog("Duplicate item", "That item is already in this tileset.", wxOK);
			return;
		}
	}

	TilesetEditEntry entry;
	entry.kind = TilesetEditEntry::ITEM;
	entry.item_id = itemId;
	entries.push_back(entry);
	RefreshList();
	entry_list->SetSelection(entries.size() - 1);
	UpdateEditFields();
}

void EditTilesetWindow::OnClickAddSeparator(wxCommandEvent& WXUNUSED(event)) {
	TilesetEditEntry entry;
	entry.kind = TilesetEditEntry::SEPARATOR;
	entries.push_back(entry);
	RefreshList();
	entry_list->SetSelection(entries.size() - 1);
	UpdateEditFields();
}

void EditTilesetWindow::OnClickRemoveEntry(wxCommandEvent& WXUNUSED(event)) {
	const long selected = entry_list->GetSelection();
	if (selected == wxNOT_FOUND || static_cast<size_t>(selected) >= entries.size()) {
		return;
	}

	entries.erase(entries.begin() + selected);
	RefreshList();
}

void EditTilesetWindow::OnClickSave(wxCommandEvent& WXUNUSED(event)) {
	wxString error;
	if (!TilesetApplyEditEntries(edit_tileset, category_type, entries, error)) {
		g_gui.PopupDialog("Could not save tileset", error, wxOK);
		return;
	}

	if (!TilesetSaveToXml(edit_tileset, category_type, error)) {
		g_gui.PopupDialog("Could not save tileset", error, wxOK);
		return;
	}

	g_materials.modify();
	g_gui.PopupDialog("Tileset saved", wxString::Format("Saved tileset '%s' to XML.", wxstr(edit_tileset->name)), wxOK);
	g_gui.RebuildPalettes();
	EndModal(wxID_OK);
}

void EditTilesetWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}
