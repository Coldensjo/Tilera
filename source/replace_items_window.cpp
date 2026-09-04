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
#include "replace_items_window.h"
#include "find_item_window.h"
#include "graphics.h"
#include "gui.h"
#include "artprovider.h"
#include "items.h"
#include "item.h"
#include "theme.h"
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

// ============================================================================
// ReplaceItemsButton

ReplaceItemsButton::ReplaceItemsButton(wxWindow* parent) :
	DCButton(parent, wxID_ANY, wxDefaultPosition, DC_BTN_TOGGLE, RENDER_SIZE_32x32, 0),
	m_id(0) {
	////
}

ItemGroup_t ReplaceItemsButton::GetGroup() const {
	if (m_id != 0) {
		const ItemType& it = g_items.getItemType(m_id);
		if (it.id != 0) {
			return it.group;
		}
	}
	return ITEM_GROUP_NONE;
}

void ReplaceItemsButton::SetItemId(uint16_t id) {
	if (m_id == id) {
		return;
	}

	m_id = id;

	if (m_id != 0) {
		const ItemType& it = g_items.getItemType(m_id);
		if (it.id != 0) {
			SetSprite(it.clientID);
			return;
		}
	}

	SetSprite(0);
}

// ============================================================================
// ReplaceItemsListBox

ReplaceItemsListBox::ReplaceItemsListBox(wxWindow* parent) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE) {
	m_arrow_bitmap = wxArtProvider::GetBitmapBundle(ART_POSITION_GO, wxART_TOOLBAR).GetBitmapFor(this);
	m_flag_bitmap = wxArtProvider::GetBitmapBundle(ART_PZ_BRUSH, wxART_TOOLBAR).GetBitmapFor(this);
	Bind(wxEVT_CHAR, &ReplaceItemsListBox::OnChar, this);
}

void ReplaceItemsListBox::SelectAll() {
	// No-op for single-selection listbox to prevent assertion
}

void ReplaceItemsListBox::OnChar(wxKeyEvent& event) {
	// Intercept Ctrl+A to prevent SelectAll assertion for single-selection listbox
	if (isSelectAllShortcut(event)) {
		// Do nothing - prevent base class from calling SelectAll
		return;
	}
	event.Skip();
}

bool ReplaceItemsListBox::AddItem(const ReplacingItem& item) {
	if (item.replaceId == 0 || item.withId == 0 || item.replaceId == item.withId) {
		return false;
	}

	m_items.push_back(item);
	SetItemCount(m_items.size());
	Refresh();

	return true;
}

void ReplaceItemsListBox::MarkAsComplete(const ReplacingItem& item, uint32_t total) {
	auto it = std::find(m_items.begin(), m_items.end(), item);
	if (it != m_items.end()) {
		it->total = total;
		it->complete = true;
		Refresh();
	}
}

void ReplaceItemsListBox::RemoveSelected() {
	if (m_items.empty()) {
		return;
	}

	const int index = GetSelection();
	if (index == wxNOT_FOUND) {
		return;
	}

	m_items.erase(m_items.begin() + index);
	SetItemCount(m_items.size());
	Refresh();
}

bool ReplaceItemsListBox::CanAdd(uint16_t replaceId, uint16_t replaceIdEnd, uint16_t withId) const {
	if (replaceId == 0 || withId == 0 || replaceId == withId) {
		return false;
	}

	uint16_t newStart = replaceId;
	uint16_t newEnd = (replaceIdEnd > 0 && replaceIdEnd >= replaceId) ? replaceIdEnd : replaceId;

	for (const ReplacingItem& item : m_items) {
		uint16_t itemStart = item.replaceId;
		uint16_t itemEnd = (item.isRange && item.replaceIdEnd > 0) ? item.replaceIdEnd : item.replaceId;

		// Check if ranges overlap
		if ((newStart >= itemStart && newStart <= itemEnd) ||
			(newEnd >= itemStart && newEnd <= itemEnd) ||
			(itemStart >= newStart && itemStart <= newEnd) ||
			(itemEnd >= newStart && itemEnd <= newEnd)) {
			return false;
		}
	}
	return true;
}

void ReplaceItemsListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const {
	ASSERT(index < m_items.size());
	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);

	const ReplacingItem& item = m_items.at(index);
	const ItemType& type1 = g_items.getItemType(item.replaceId);
	Sprite* sprite1 = g_gui.gfx.getSprite(type1.clientID);
	const ItemType& type2 = g_items.getItemType(item.withId);
	Sprite* sprite2 = g_gui.gfx.getSprite(type2.clientID);

	if (sprite1 && sprite2) {
		int x = rect.GetX();
		int y = rect.GetY();
		sprite1->DrawTo(&dc, SPRITE_SIZE_32x32, x + 4, y + 4, rect.GetWidth(), rect.GetHeight());
		dc.DrawBitmap(m_arrow_bitmap, x + 38, y + 10, true);
		sprite2->DrawTo(&dc, SPRITE_SIZE_32x32, x + 56, y + 4, rect.GetWidth(), rect.GetHeight());
		
		// Draw secondary item if present
		int textX = x + 104;
		if (item.secondaryId != 0) {
			const ItemType& type3 = g_items.getItemType(item.secondaryId);
			Sprite* sprite3 = g_gui.gfx.getSprite(type3.clientID);
			if (sprite3) {
				sprite3->DrawTo(&dc, SPRITE_SIZE_32x32, x + 90, y + 4, rect.GetWidth(), rect.GetHeight());
				textX = x + 118;
			}
		}
		
		wxString replaceText;
		if (item.isRange && item.replaceIdEnd > 0) {
			replaceText = wxString::Format("Replace: %d-%d With: %d", item.replaceId, item.replaceIdEnd, item.withId);
		} else {
			replaceText = wxString::Format("Replace: %d With: %d", item.replaceId, item.withId);
		}
		if (item.secondaryId != 0) {
			replaceText += wxString::Format(" %s %d", item.secondaryPosition ? wxT("+above") : wxT("+below"), item.secondaryId);
		}
		dc.DrawText(replaceText, textX, y + 10);

		if (item.complete) {
			x = rect.GetWidth() - 100;
			dc.DrawBitmap(m_flag_bitmap, x + 70, y + 10, true);
			dc.DrawText(wxString::Format("Total: %d", item.total), x, y + 10);
		}
	}

}

wxCoord ReplaceItemsListBox::OnMeasureItem(size_t WXUNUSED(index)) const {
	return 40;
}

// ============================================================================
// ReplaceItemsDialog

ReplaceItemsDialog::ReplaceItemsDialog(wxWindow* parent, bool selectionOnly) :
	wxDialog(parent, wxID_ANY, (selectionOnly ? "Replace Items on Selection" : "Replace Items"), wxDefaultPosition, wxWindow::FromDIP(wxSize(500, 510), parent), wxDEFAULT_DIALOG_STYLE),
	selectionOnly(selectionOnly),
	replaceIdStart(0),
	replaceIdEnd(0),
	isRangeSelection(false) {
	SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* list_sizer = new wxFlexGridSizer(0, 2, 0, 0);
	list_sizer->SetFlexibleDirection(wxBOTH);
	list_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);
	list_sizer->SetMinSize(FromDIP(wxSize(-1, 200)));

	list = new ReplaceItemsListBox(this);
	list->SetMinSize(FromDIP(wxSize(480, 320)));

	list_sizer->Add(list, 0, wxALL | wxEXPAND, 5);
	sizer->Add(list_sizer, 1, wxALL | wxEXPAND, 5);

	wxBoxSizer* items_sizer = new wxBoxSizer(wxHORIZONTAL);
	items_sizer->SetMinSize(FromDIP(wxSize(-1, 40)));

	replace_button = new ReplaceItemsButton(this);
	items_sizer->Add(replace_button, 0, wxALL, 5);

	wxBitmapBundle bitmap = wxArtProvider::GetBitmapBundle(ART_POSITION_GO, wxART_TOOLBAR);
	arrow_bitmap = new wxStaticBitmap(this, wxID_ANY, bitmap);
	items_sizer->Add(arrow_bitmap, 0, wxTOP, 15);

	with_button = new ReplaceItemsButton(this);
	items_sizer->Add(with_button, 0, wxALL, 5);

	secondary_button = new ReplaceItemsButton(this);
	items_sizer->Add(secondary_button, 0, wxALL, 5);

	secondary_above_checkbox = new wxCheckBox(this, wxID_ANY, wxT("Above"));
	secondary_above_checkbox->SetValue(false);
	items_sizer->Add(secondary_above_checkbox, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	items_sizer->Add(0, 0, 1, wxEXPAND, 5);

	progress = new wxGauge(this, wxID_ANY, 100);
	progress->SetValue(0);
	items_sizer->Add(progress, 0, wxALL, 5);

	sizer->Add(items_sizer, 1, wxALL | wxEXPAND, 5);

	wxBoxSizer* options_sizer = new wxBoxSizer(wxHORIZONTAL);

	selection_only_checkbox = new wxCheckBox(this, wxID_ANY, wxT("Replace within selection"));
	selection_only_checkbox->SetValue(selectionOnly);
	selection_only_checkbox->SetToolTip(wxT("Only replace items on tiles that are part of the current selection"));
	options_sizer->Add(selection_only_checkbox, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	sizer->Add(options_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 5);

	wxBoxSizer* buttons_sizer = new wxBoxSizer(wxHORIZONTAL);

	add_button = new wxButton(this, wxID_ANY, wxT("Add"));
	add_button->Enable(false);
	buttons_sizer->Add(add_button, 0, wxALL, 5);

	edit_button = new wxButton(this, wxID_ANY, wxT("Edit"));
	edit_button->Enable(false);
	buttons_sizer->Add(edit_button, 0, wxALL, 5);

	duplicate_button = new wxButton(this, wxID_ANY, wxT("Duplicate"));
	duplicate_button->Enable(false);
	buttons_sizer->Add(duplicate_button, 0, wxALL, 5);

	remove_button = new wxButton(this, wxID_ANY, wxT("Remove"));
	remove_button->Enable(false);
	buttons_sizer->Add(remove_button, 0, wxALL, 5);

	import_button = new wxButton(this, wxID_ANY, wxT("Import"));
	buttons_sizer->Add(import_button, 0, wxALL, 5);

	buttons_sizer->Add(0, 0, 1, wxEXPAND, 5);

	execute_button = new wxButton(this, wxID_ANY, wxT("Execute"));
	execute_button->Enable(false);
	buttons_sizer->Add(execute_button, 0, wxALL, 5);

	close_button = new wxButton(this, wxID_ANY, wxT("Close"));
	buttons_sizer->Add(close_button, 0, wxALL, 5);

	sizer->Add(buttons_sizer, 0, wxALL | wxEXPAND, 5);

	SetSizer(sizer);
	Layout();
	Centre(wxBOTH);

	// Connect Events
	list->Connect(wxEVT_COMMAND_LISTBOX_SELECTED, wxCommandEventHandler(ReplaceItemsDialog::OnListSelected), NULL, this);
	replace_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnReplaceItemClicked), NULL, this);
	with_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnWithItemClicked), NULL, this);
	secondary_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnSecondaryItemClicked), NULL, this);
	add_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnAddButtonClicked), NULL, this);
	edit_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnEditButtonClicked), NULL, this);
	duplicate_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnDuplicateButtonClicked), NULL, this);
	remove_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnRemoveButtonClicked), NULL, this);
	import_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnImportButtonClicked), NULL, this);
	execute_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnExecuteButtonClicked), NULL, this);
	close_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnCancelButtonClicked), NULL, this);
}

ReplaceItemsDialog::~ReplaceItemsDialog() {
	// Disconnect Events
	list->Disconnect(wxEVT_COMMAND_LISTBOX_SELECTED, wxCommandEventHandler(ReplaceItemsDialog::OnListSelected), NULL, this);
	replace_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnReplaceItemClicked), NULL, this);
	with_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnWithItemClicked), NULL, this);
	secondary_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnSecondaryItemClicked), NULL, this);
	add_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnAddButtonClicked), NULL, this);
	edit_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnEditButtonClicked), NULL, this);
	duplicate_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnDuplicateButtonClicked), NULL, this);
	remove_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnRemoveButtonClicked), NULL, this);
	import_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnImportButtonClicked), NULL, this);
	execute_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnExecuteButtonClicked), NULL, this);
	close_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnCancelButtonClicked), NULL, this);
}

void ReplaceItemsDialog::UpdateWidgets() {
	const uint16_t replaceId = replaceIdStart;
	const uint16_t replaceIdEndVal = replaceIdEnd;
	const uint16_t withId = with_button->GetItemId();
	const uint16_t secondaryId = secondary_button->GetItemId();
	const bool hasSelection = list->GetSelection() != wxNOT_FOUND;
	add_button->Enable(replaceId != 0 && withId != 0 && replaceId != withId && list->CanAdd(replaceId, replaceIdEndVal, withId));
	edit_button->Enable(hasSelection);
	duplicate_button->Enable(hasSelection);
	remove_button->Enable(list->GetCount() != 0 && hasSelection);
	execute_button->Enable(list->GetCount() != 0);
	secondary_button->Enable(replaceId != 0 && withId != 0);
	secondary_above_checkbox->Enable(secondaryId != 0);
}

void ReplaceItemsDialog::OnListSelected(wxCommandEvent& WXUNUSED(event)) {
	UpdateWidgets();
}

bool ReplaceItemsDialog::GetReplaceItemRange(uint16_t& startId, uint16_t& endId) {
	wxDialog rangeDialog(this, wxID_ANY, "Select Item Range", wxDefaultPosition, FromDIP(wxSize(300, 225)), wxDEFAULT_DIALOG_STYLE);
	
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	
	wxStaticBoxSizer* rangeSizer = new wxStaticBoxSizer(new wxStaticBox(&rangeDialog, wxID_ANY, "Item ID Range"), wxVERTICAL);
	
	wxBoxSizer* fromSizer = new wxBoxSizer(wxHORIZONTAL);
	fromSizer->Add(new wxStaticText(&rangeDialog, wxID_ANY, "From:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	wxSpinCtrl* fromSpin = new wxSpinCtrl(&rangeDialog, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 100, g_items.getMaxID(), 100);
	fromSizer->Add(fromSpin, 1, wxALL | wxEXPAND, 5);
	rangeSizer->Add(fromSizer, 0, wxALL | wxEXPAND, 5);
	
	wxBoxSizer* toSizer = new wxBoxSizer(wxHORIZONTAL);
	toSizer->Add(new wxStaticText(&rangeDialog, wxID_ANY, "To (optional):"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	wxSpinCtrl* toSpin = new wxSpinCtrl(&rangeDialog, wxID_ANY, wxT("0"), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, g_items.getMaxID(), 0);
	toSizer->Add(toSpin, 1, wxALL | wxEXPAND, 5);
	rangeSizer->Add(toSizer, 0, wxALL | wxEXPAND, 5);
	
	wxStaticText* hintText = new wxStaticText(&rangeDialog, wxID_ANY, "Leave 'To' empty or set to 0 for single item");
	hintText->SetForegroundColour(ThemeManager::Get().GetPalette().mutedText);
	rangeSizer->Add(hintText, 0, wxALL, 5);
	
	mainSizer->Add(rangeSizer, 1, wxALL | wxEXPAND, 5);
	
	wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* okBtn = new wxButton(&rangeDialog, wxID_OK, "OK");
	wxButton* cancelBtn = new wxButton(&rangeDialog, wxID_CANCEL, "Cancel");
	buttonSizer->Add(okBtn, 0, wxALL, 5);
	buttonSizer->Add(cancelBtn, 0, wxALL, 5);
	mainSizer->Add(buttonSizer, 0, wxALL | wxALIGN_CENTER, 5);
	
	rangeDialog.SetSizer(mainSizer);
	rangeDialog.Layout();
	rangeDialog.Centre(wxBOTH);
	
	if (rangeDialog.ShowModal() == wxID_OK) {
		startId = static_cast<uint16_t>(fromSpin->GetValue());
		uint16_t toValue = static_cast<uint16_t>(toSpin->GetValue());
		if (toValue > startId) {
			endId = toValue;
		} else {
			endId = 0;
		}
		return true;
	}
	return false;
}

void ReplaceItemsDialog::OnReplaceItemClicked(wxMouseEvent& WXUNUSED(event)) {
	uint16_t startId = 0, endId = 0;
	if (GetReplaceItemRange(startId, endId)) {
		if (startId != 0 && startId != with_button->GetItemId()) {
			replaceIdStart = startId;
			replaceIdEnd = endId;
			isRangeSelection = (endId > 0);
			
			// Display the selected range on the button
			if (isRangeSelection) {
				replace_button->SetItemId(startId); // Show first item in range
			} else {
				replace_button->SetItemId(startId);
			}
			UpdateWidgets();
		}
	}
}

void ReplaceItemsDialog::OnWithItemClicked(wxMouseEvent& WXUNUSED(event)) {
	if (replace_button->GetItemId() == 0) {
		return;
	}

	FindItemDialog dialog(this, "With Item");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t id = dialog.getResultID();
		if (id != replace_button->GetItemId()) {
			with_button->SetItemId(id);
			UpdateWidgets();
		}
	}
	dialog.Destroy();
}

void ReplaceItemsDialog::OnSecondaryItemClicked(wxMouseEvent& WXUNUSED(event)) {
	if (replace_button->GetItemId() == 0 || with_button->GetItemId() == 0) {
		return;
	}

	FindItemDialog dialog(this, "Secondary Item");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t id = dialog.getResultID();
		if (id != 0) {
			secondary_button->SetItemId(id);
			UpdateWidgets();
		}
	}
	dialog.Destroy();
}

void ReplaceItemsDialog::OnAddButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	const uint16_t replaceId = replaceIdStart;
	const uint16_t replaceIdEndVal = replaceIdEnd;
	const uint16_t withId = with_button->GetItemId();
	if (replaceId != 0 && withId != 0 && list->CanAdd(replaceId, replaceIdEndVal, withId)) {
		ReplacingItem item;
		item.replaceId = replaceId;
		item.replaceIdEnd = replaceIdEndVal;
		item.isRange = (replaceIdEndVal > 0);
		item.withId = withId;
		item.secondaryId = secondary_button->GetItemId();
		item.secondaryPosition = secondary_above_checkbox->GetValue();
		if (list->AddItem(item)) {
			replace_button->SetItemId(0);
			with_button->SetItemId(0);
			secondary_button->SetItemId(0);
			secondary_above_checkbox->SetValue(false);
			replaceIdStart = 0;
			replaceIdEnd = 0;
			isRangeSelection = false;
			UpdateWidgets();
		}
	}
}

void ReplaceItemsDialog::OnEditButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	const int index = list->GetSelection();
	if (index == wxNOT_FOUND) {
		return;
	}

	const ReplacingItem* item = list->GetItemAt(index);
	if (!item) {
		return;
	}

	// Populate the input fields with the selected item's data
	replaceIdStart = item->replaceId;
	replaceIdEnd = item->replaceIdEnd;
	isRangeSelection = item->isRange;
	replace_button->SetItemId(item->replaceId);
	with_button->SetItemId(item->withId);
	secondary_button->SetItemId(item->secondaryId);
	secondary_above_checkbox->SetValue(item->secondaryPosition);

	// Remove the item from the list so user can modify and re-add
	list->RemoveSelected();
	UpdateWidgets();
}

void ReplaceItemsDialog::OnDuplicateButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	const int index = list->GetSelection();
	if (index == wxNOT_FOUND) {
		return;
	}

	const ReplacingItem* item = list->GetItemAt(index);
	if (!item) {
		return;
	}

	// Create a copy and add it to the list
	ReplacingItem newItem = *item;
	newItem.complete = false;
	newItem.total = 0;
	list->AddItem(newItem);
	UpdateWidgets();
}

void ReplaceItemsDialog::OnRemoveButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	list->RemoveSelected();
	UpdateWidgets();
}

void ReplaceItemsDialog::OnImportButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog fileDialog(
		this,
		"Import Replace Items",
		wxEmptyString,
		wxEmptyString,
		"Text files (*.txt)|*.txt|All files (*.*)|*.*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST
	);

	if (fileDialog.ShowModal() != wxID_OK) {
		return;
	}

	wxTextFile file;
	if (!file.Open(fileDialog.GetPath())) {
		wxMessageBox("Failed to open file.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	int imported = 0;
	int skipped = 0;

	for (size_t i = 0; i < file.GetLineCount(); ++i) {
		wxString line = file.GetLine(i).Trim().Trim(false);

		// Skip empty lines and comments
		if (line.IsEmpty() || line.StartsWith("#") || line.StartsWith("//")) {
			continue;
		}

		wxStringTokenizer tokenizer(line, ";");
		if (tokenizer.CountTokens() < 2) {
			skipped++;
			continue;
		}

		long replaceId = 0, withId = 0, secondaryId = 0;

		// Parse replaceId (first)
		wxString token = tokenizer.GetNextToken().Trim().Trim(false);
		if (!token.ToLong(&replaceId) || replaceId <= 0 || replaceId > 0xFFFF) {
			skipped++;
			continue;
		}

		// Parse withId (second)
		token = tokenizer.GetNextToken().Trim().Trim(false);
		if (!token.ToLong(&withId) || withId <= 0 || withId > 0xFFFF) {
			skipped++;
			continue;
		}

		// Parse secondaryId (optional third)
		if (tokenizer.HasMoreTokens()) {
			token = tokenizer.GetNextToken().Trim().Trim(false);
			if (!token.IsEmpty()) {
				token.ToLong(&secondaryId);
				if (secondaryId < 0 || secondaryId > 0xFFFF) {
					secondaryId = 0;
				}
			}
		}

		// Check if we can add this item
		if (!list->CanAdd(static_cast<uint16_t>(replaceId), 0, static_cast<uint16_t>(withId))) {
			skipped++;
			continue;
		}

		// Create and add the item
		ReplacingItem item;
		item.replaceId = static_cast<uint16_t>(replaceId);
		item.replaceIdEnd = 0;
		item.isRange = false;
		item.withId = static_cast<uint16_t>(withId);
		item.secondaryId = static_cast<uint16_t>(secondaryId);
		item.secondaryPosition = false; // Default to "below"

		if (list->AddItem(item)) {
			imported++;
		} else {
			skipped++;
		}
	}

	file.Close();
	UpdateWidgets();

	wxMessageBox(
		wxString::Format("Imported %d items.\nSkipped %d lines.", imported, skipped),
		"Import Complete",
		wxOK | wxICON_INFORMATION,
		this
	);
}

void ReplaceItemsDialog::OnExecuteButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	const auto& items = list->GetItems();
	if (items.empty()) {
		return;
	}

	replace_button->Enable(false);
	with_button->Enable(false);
	secondary_button->Enable(false);
	secondary_above_checkbox->Enable(false);
	selection_only_checkbox->Enable(false);
	add_button->Enable(false);
	remove_button->Enable(false);
	execute_button->Enable(false);
	close_button->Enable(false);
	progress->SetValue(0);

	MapTab* tab = dynamic_cast<MapTab*>(GetParent());
	if (!tab) {
		return;
	}

	Editor* editor = tab->GetEditor();
	const bool onSelection = selection_only_checkbox->GetValue();

	int done = 0;
	for (const ReplacingItem& info : items) {
		ItemFinder finder(info.replaceId, info.replaceIdEnd, (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));

		// search on map
		foreach_ItemOnMap(editor->map, finder, onSelection);

		uint32_t total = 0;
		std::vector<std::pair<Tile*, Item*>>& result = finder.result;

		if (!result.empty()) {
			Action* action = editor->actionQueue->createAction(ACTION_REPLACE_ITEMS);
			for (std::vector<std::pair<Tile*, Item*>>::const_iterator rit = result.begin(); rit != result.end(); ++rit) {
				Tile* new_tile = rit->first->deepCopy(editor->map);
				int index = rit->first->getIndexOf(rit->second);
				ASSERT(index != wxNOT_FOUND);
				Item* item = new_tile->getItemAt(index);
				ASSERT(item && item->getID() == rit->second->getID());
				Item* transformed_item = transformItem(item, info.withId, new_tile);
				
				// Add secondary item if specified
				if (info.secondaryId != 0 && transformed_item) {
					// Find the transformed item in the items vector (or check if it's ground)
					ItemVector::iterator item_it = std::find(new_tile->items.begin(), new_tile->items.end(), transformed_item);
					
					// Create the secondary item
					Item* secondary_item = Item::Create(info.secondaryId);
					if (secondary_item) {
						if (item_it != new_tile->items.end()) {
							// Item is in the items vector
							if (info.secondaryPosition) {
								// Insert above (after) the transformed item - drawn on top
								new_tile->items.insert(item_it + 1, secondary_item);
							} else {
								// Insert below (before) the transformed item - drawn underneath
								new_tile->items.insert(item_it, secondary_item);
							}
						} else if (transformed_item == new_tile->ground) {
							// Item is ground, insert at the beginning of items vector
							if (info.secondaryPosition) {
								// "Above" ground means at the beginning of items
								new_tile->items.insert(new_tile->items.begin(), secondary_item);
							} else {
								// "Below" ground means also at the beginning (ground is always first)
								new_tile->items.insert(new_tile->items.begin(), secondary_item);
							}
						} else {
							// Fallback: use addItem which will place it correctly
							new_tile->addItem(secondary_item);
						}
					}
				}
				
				action->addChange(new Change(new_tile));
				total++;
			}
			editor->actionQueue->addAction(action);
		}

		done++;
		const int value = static_cast<int>((done / items.size()) * 100);
		progress->SetValue(std::min(std::max(value, 0), 100));
		list->MarkAsComplete(info, total);
	}

	tab->Refresh();
	close_button->Enable(true);
	selection_only_checkbox->Enable(true);
	UpdateWidgets();
}

void ReplaceItemsDialog::OnCancelButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	Close();
}
