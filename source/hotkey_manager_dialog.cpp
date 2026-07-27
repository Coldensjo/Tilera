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

#include "hotkey_manager_dialog.h"
#include "application.h"
#include "gui.h"
#include "main_menubar.h"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

//=============================================================================
// HotkeyManagerDialog

HotkeyManagerDialog::HotkeyManagerDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Hotkey Manager", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	wxBoxSizer* main_sizer = newd wxBoxSizer(wxVERTICAL);

	// Search row
	wxBoxSizer* search_sizer = newd wxBoxSizer(wxHORIZONTAL);
	search_sizer->Add(newd wxStaticText(this, wxID_ANY, "Search:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
	search_field = newd wxTextCtrl(this, wxID_ANY);
	search_field->SetToolTip("Filter by action name, category or shortcut");
	search_sizer->Add(search_field, 1, wxEXPAND);
	main_sizer->Add(search_sizer, 0, wxEXPAND | wxALL, FromDIP(8));

	// Hotkey list
	list = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	list->InsertColumn(0, "Category", wxLIST_FORMAT_LEFT, FromDIP(110));
	list->InsertColumn(1, "Action", wxLIST_FORMAT_LEFT, FromDIP(240));
	list->InsertColumn(2, "Shortcut", wxLIST_FORMAT_LEFT, FromDIP(140));
	list->InsertColumn(3, "Default", wxLIST_FORMAT_LEFT, FromDIP(140));
	list->SetMinSize(FromDIP(wxSize(660, 380)));
	main_sizer->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

	// Description of the selected action
	description_text = newd wxStaticText(this, wxID_ANY, "Select a hotkey to see its description. Double-click a rebindable hotkey to change it.");
	main_sizer->Add(description_text, 0, wxEXPAND | wxALL, FromDIP(8));

	main_sizer->Add(newd wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

	// Buttons
	wxBoxSizer* button_sizer = newd wxBoxSizer(wxHORIZONTAL);
	change_button = newd wxButton(this, wxID_ANY, "Change...");
	change_button->SetToolTip("Assign a new key combination to the selected action (Enter)");
	clear_button = newd wxButton(this, wxID_ANY, "Clear");
	clear_button->SetToolTip("Remove the binding from the selected action");
	reset_button = newd wxButton(this, wxID_ANY, "Reset");
	reset_button->SetToolTip("Restore the default binding of the selected action");
	reset_all_button = newd wxButton(this, wxID_ANY, "Reset All");
	reset_all_button->SetToolTip("Restore the default bindings of all actions");
	wxButton* close_button = newd wxButton(this, wxID_OK, "Close");

	button_sizer->Add(change_button, 0, wxRIGHT, FromDIP(5));
	button_sizer->Add(clear_button, 0, wxRIGHT, FromDIP(5));
	button_sizer->Add(reset_button, 0, wxRIGHT, FromDIP(5));
	button_sizer->Add(reset_all_button, 0, wxRIGHT, FromDIP(5));
	button_sizer->AddStretchSpacer();
	button_sizer->Add(close_button);
	main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, FromDIP(8));

	SetSizerAndFit(main_sizer);
	Centre(wxBOTH);

	search_field->Bind(wxEVT_TEXT, &HotkeyManagerDialog::OnSearchText, this);
	list->Bind(wxEVT_LIST_ITEM_SELECTED, &HotkeyManagerDialog::OnItemSelected, this);
	list->Bind(wxEVT_LIST_ITEM_DESELECTED, &HotkeyManagerDialog::OnItemSelected, this);
	list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &HotkeyManagerDialog::OnItemActivated, this);
	change_button->Bind(wxEVT_BUTTON, &HotkeyManagerDialog::OnChangeButton, this);
	clear_button->Bind(wxEVT_BUTTON, &HotkeyManagerDialog::OnClearButton, this);
	reset_button->Bind(wxEVT_BUTTON, &HotkeyManagerDialog::OnResetButton, this);
	reset_all_button->Bind(wxEVT_BUTTON, &HotkeyManagerDialog::OnResetAllButton, this);

	BuildDisplayList();
	RefreshList();
	UpdateSelection();
}

void HotkeyManagerDialog::BuildDisplayList() {
	displayed.clear();

	const wxString filter = search_field->GetValue().Lower();
	const std::vector<HotkeyAction*>& all = g_hotkeys.getActions();

	// Menu actions first (they register in menu order), then the canvas /
	// global keys, then the read-only mouse gestures.
	auto matches_filter = [&](const HotkeyAction* action) {
		if (filter.empty()) {
			return true;
		}
		return action->name.Lower().Contains(filter)
			|| action->category.Lower().Contains(filter)
			|| action->shortcutString().Lower().Contains(filter)
			|| action->defaultString().Lower().Contains(filter);
	};

	for (HotkeyAction* action : all) {
		if (action->isMenuAction() && matches_filter(action)) {
			displayed.push_back(action);
		}
	}
	for (HotkeyAction* action : all) {
		if (!action->isMenuAction() && action->fixedDisplay.empty() && matches_filter(action)) {
			displayed.push_back(action);
		}
	}
	for (HotkeyAction* action : all) {
		if (!action->fixedDisplay.empty() && action->canvasAction == CanvasHotkey::None && matches_filter(action)) {
			displayed.push_back(action);
		}
	}
}

void HotkeyManagerDialog::RefreshList() {
	list->Freeze();
	list->DeleteAllItems();
	for (size_t i = 0; i < displayed.size(); ++i) {
		list->InsertItem(long(i), "");
		RefreshRow(long(i));
	}
	list->Thaw();
}

void HotkeyManagerDialog::RefreshRow(long row) {
	const HotkeyAction* action = displayed[size_t(row)];

	wxString shortcut = action->shortcutString();
	if (shortcut.empty()) {
		shortcut = "(none)";
	}
	wxString defaults = action->defaultString();
	if (defaults.empty()) {
		defaults = "(none)";
	}

	list->SetItem(row, 0, action->category);
	list->SetItem(row, 1, action->name);
	list->SetItem(row, 2, shortcut);
	list->SetItem(row, 3, defaults);
	list->SetItemTextColour(row, action->rebindable ? wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT) : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
}

HotkeyAction* HotkeyManagerDialog::GetSelectedAction() const {
	const long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (row < 0 || size_t(row) >= displayed.size()) {
		return nullptr;
	}
	return displayed[size_t(row)];
}

void HotkeyManagerDialog::UpdateSelection() {
	const HotkeyAction* action = GetSelectedAction();
	if (action) {
		wxString text = action->description;
		if (!action->rebindable) {
			text << (text.empty() ? "" : " ") << "(This entry is fixed and cannot be rebound.)";
		}
		description_text->SetLabel(text);
	} else {
		description_text->SetLabel("Select a hotkey to see its description. Double-click a rebindable hotkey to change it.");
	}

	const bool rebindable = action && action->rebindable;
	change_button->Enable(rebindable);
	clear_button->Enable(rebindable && !action->chords().empty());
	reset_button->Enable(rebindable && action->overridden);
}

void HotkeyManagerDialog::ApplyChanges() {
	// Push the new bindings into the menubar (and, on wxGTK, the frame
	// accelerator table); the canvas reads the manager directly.
	if (g_gui.root && g_gui.root->GetMenuBarPtr()) {
		g_gui.root->GetMenuBarPtr()->ApplyHotkeyOverrides();
	}

	const long selected = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	BuildDisplayList();
	RefreshList();
	if (selected >= 0 && size_t(selected) < displayed.size()) {
		list->SetItemState(selected, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}
	UpdateSelection();
}

void HotkeyManagerDialog::OnSearchText(wxCommandEvent& WXUNUSED(event)) {
	BuildDisplayList();
	RefreshList();
	UpdateSelection();
}

void HotkeyManagerDialog::OnItemSelected(wxListEvent& WXUNUSED(event)) {
	UpdateSelection();
}

void HotkeyManagerDialog::OnItemActivated(wxListEvent& WXUNUSED(event)) {
	HotkeyAction* action = GetSelectedAction();
	if (action && action->rebindable) {
		ChangeBinding(action);
	}
}

void HotkeyManagerDialog::OnChangeButton(wxCommandEvent& WXUNUSED(event)) {
	HotkeyAction* action = GetSelectedAction();
	if (action && action->rebindable) {
		ChangeBinding(action);
	}
}

void HotkeyManagerDialog::ChangeBinding(HotkeyAction* action) {
	HotkeyCaptureDialog capture(this, action->name);
	if (capture.ShowModal() != wxID_OK) {
		return;
	}

	const HotkeyChord chord = capture.GetChord();
	if (!chord.valid()) {
		return;
	}

	const HotkeyAction* conflict = g_hotkeys.findConflict(chord, action->id);
	if (conflict) {
		if (!conflict->rebindable) {
			wxMessageBox(wxString::Format("\"%s\" is already used by \"%s\" (%s), which cannot be rebound.", chord.toString(), conflict->name, conflict->category), "Shortcut In Use", wxOK | wxICON_WARNING, this);
			return;
		}
		const int answer = wxMessageBox(wxString::Format("\"%s\" is already assigned to \"%s\" (%s).\n\nReassign it to \"%s\"? The old action will be left without a shortcut.", chord.toString(), conflict->name, conflict->category, action->name), "Shortcut In Use", wxYES_NO | wxICON_QUESTION, this);
		if (answer != wxYES) {
			return;
		}
		g_hotkeys.setOverride(conflict->id, HotkeyChord());
	}

	g_hotkeys.setOverride(action->id, chord);
	ApplyChanges();
}

void HotkeyManagerDialog::OnClearButton(wxCommandEvent& WXUNUSED(event)) {
	HotkeyAction* action = GetSelectedAction();
	if (!action || !action->rebindable) {
		return;
	}
	g_hotkeys.setOverride(action->id, HotkeyChord());
	ApplyChanges();
}

void HotkeyManagerDialog::OnResetButton(wxCommandEvent& WXUNUSED(event)) {
	HotkeyAction* action = GetSelectedAction();
	if (!action) {
		return;
	}
	g_hotkeys.resetOverride(action->id);
	ApplyChanges();
}

void HotkeyManagerDialog::OnResetAllButton(wxCommandEvent& WXUNUSED(event)) {
	const int answer = wxMessageBox("Restore the default bindings of ALL hotkeys?", "Reset All Hotkeys", wxYES_NO | wxICON_QUESTION, this);
	if (answer != wxYES) {
		return;
	}
	g_hotkeys.resetAll();
	ApplyChanges();
}

//=============================================================================
// HotkeyCaptureDialog

HotkeyCaptureDialog::HotkeyCaptureDialog(wxWindow* parent, const wxString& actionName) :
	wxDialog(parent, wxID_ANY, "Press New Shortcut", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE) {
	wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticText* prompt = newd wxStaticText(this, wxID_ANY, wxString::Format("Press the new key combination for\n\"%s\".\n\nEsc cancels.", actionName), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
	sizer->Add(prompt, 0, wxALIGN_CENTER | wxALL, FromDIP(15));

	wxButton* cancel = newd wxButton(this, wxID_CANCEL, "Cancel");
	sizer->Add(cancel, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(10));

	SetSizerAndFit(sizer);
	Centre(wxBOTH);

	Bind(wxEVT_CHAR_HOOK, &HotkeyCaptureDialog::OnCharHook, this);
}

void HotkeyCaptureDialog::OnCharHook(wxKeyEvent& event) {
	const int code = event.GetKeyCode();
	switch (code) {
		case WXK_ESCAPE: {
			EndModal(wxID_CANCEL);
			return;
		}
		// Bare modifiers do not finish a chord (WXK_RAW_CONTROL and
		// WXK_COMMAND are aliases of WXK_CONTROL on non-Apple platforms)
		case WXK_SHIFT:
		case WXK_ALT:
		case WXK_CONTROL:
		case WXK_WINDOWS_LEFT:
		case WXK_WINDOWS_RIGHT:
		case WXK_WINDOWS_MENU:
		case WXK_CAPITAL:
		case WXK_NUMLOCK:
		case WXK_SCROLL:
		case WXK_NONE: {
			event.Skip();
			return;
		}
		default: {
			break;
		}
	}

	chord = HotkeyChord::fromKeyEvent(event);
	EndModal(wxID_OK);
}
