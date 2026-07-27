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

#ifndef RME_HOTKEY_MANAGER_DIALOG_H_
#define RME_HOTKEY_MANAGER_DIALOG_H_

#include "hotkey_manager.h"

#include <wx/dialog.h>

#include <vector>

class wxButton;
class wxListCtrl;
class wxListEvent;
class wxStaticText;
class wxTextCtrl;

// Modal dialog listing every hotkey known to the HotkeyManager - menu
// accelerators, the hardcoded canvas/frame keys and the mouse gestures -
// with search, rebinding, unbinding and reset-to-default.
class HotkeyManagerDialog : public wxDialog {
public:
	HotkeyManagerDialog(wxWindow* parent);

private:
	void BuildDisplayList();
	void RefreshList();
	void RefreshRow(long row);
	void UpdateSelection();
	HotkeyAction* GetSelectedAction() const;
	void ApplyChanges();

	void OnSearchText(wxCommandEvent& event);
	void OnItemSelected(wxListEvent& event);
	void OnItemActivated(wxListEvent& event);
	void OnChangeButton(wxCommandEvent& event);
	void OnClearButton(wxCommandEvent& event);
	void OnResetButton(wxCommandEvent& event);
	void OnResetAllButton(wxCommandEvent& event);

	void ChangeBinding(HotkeyAction* action);

	wxTextCtrl* search_field;
	wxListCtrl* list;
	wxStaticText* description_text;
	wxButton* change_button;
	wxButton* clear_button;
	wxButton* reset_button;
	wxButton* reset_all_button;

	std::vector<HotkeyAction*> displayed; // list row -> action
};

// Tiny helper dialog that waits for the user to press the new chord.
class HotkeyCaptureDialog : public wxDialog {
public:
	HotkeyCaptureDialog(wxWindow* parent, const wxString& actionName);

	const HotkeyChord& GetChord() const {
		return chord;
	}

private:
	void OnCharHook(wxKeyEvent& event);

	HotkeyChord chord;
};

#endif
