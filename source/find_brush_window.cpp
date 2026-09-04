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

#include "find_brush_window.h"
#include "common_windows.h"
#include "palette_brushlist.h"
#include "brush.h"
#include "raw_brush.h"
#include "items.h"
#include "gui.h"
#include "theme.h"

FindBrushWindow::FindBrushWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	search_field(nullptr),
	status_label(nullptr),
	results_panel(nullptr),
	results_sizer(nullptr),
	icon_box(nullptr),
	idle_input_timer(this),
	hotkeys_disabled_by_us(false),
	result_tileset(g_brushes, "Find Brush Results"),
	result_category(result_tileset.getCategory(TILESET_UNKNOWN)) {
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	search_field = newd KeyForwardingTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	sizer->Add(search_field, 0, wxEXPAND | wxALL, 4);

	status_label = newd wxStaticText(this, wxID_ANY, "Type at least 2 characters to search.");
	status_label->SetForegroundColour(ThemeManager::Get().GetPalette().mutedText);
	sizer->Add(status_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

	results_panel = newd wxPanel(this, wxID_ANY);
	results_sizer = newd wxBoxSizer(wxVERTICAL);
	results_panel->SetSizer(results_sizer);
	sizer->Add(results_panel, 1, wxEXPAND);

	SetSizerAndFit(sizer);
	SetMinSize(FromDIP(wxSize(300, 300)));

	search_field->Bind(wxEVT_TEXT, &FindBrushWindow::OnTextChange, this);
	search_field->Bind(wxEVT_TEXT_ENTER, &FindBrushWindow::OnSearchEnter, this);
	search_field->Bind(wxEVT_SET_FOCUS, &FindBrushWindow::OnSearchSetFocus, this);
	search_field->Bind(wxEVT_KILL_FOCUS, &FindBrushWindow::OnSearchKillFocus, this);
	Bind(wxEVT_TIMER, &FindBrushWindow::OnTextIdle, this, idle_input_timer.GetId());
}

FindBrushWindow::~FindBrushWindow() {
	if (hotkeys_disabled_by_us) {
		g_gui.EnableHotkeys();
	}
}

void FindBrushWindow::OnSearchSetFocus(wxFocusEvent& event) {
	g_gui.DisableHotkeys();
	hotkeys_disabled_by_us = true;
	event.Skip();
}

void FindBrushWindow::OnSearchKillFocus(wxFocusEvent& event) {
	g_gui.EnableHotkeys();
	hotkeys_disabled_by_us = false;
	event.Skip();
}

void FindBrushWindow::FocusSearch() {
	search_field->SetFocus();
	search_field->SelectAll();
}

void FindBrushWindow::ResetSearch() {
	search_field->ChangeValue("");
	RefreshResults();
	FocusSearch();
}

void FindBrushWindow::InvalidateResults() {
	search_field->ChangeValue("");
	RefreshResults();
}

void FindBrushWindow::OnTextChange(wxCommandEvent& WXUNUSED(event)) {
	idle_input_timer.Start(300, true);
}

void FindBrushWindow::OnTextIdle(wxTimerEvent& WXUNUSED(event)) {
	RefreshResults();
}

void FindBrushWindow::OnSearchEnter(wxCommandEvent& WXUNUSED(event)) {
	idle_input_timer.Stop();
	RefreshResults();

	for (Brush* brush : result_category->brushlist) {
		if (brush && !brush->isPaletteSeparator()) {
			PickBrush(brush);
			break;
		}
	}
}

void FindBrushWindow::PickBrush(Brush* brush) {
	if (!brush) {
		return;
	}
	if (icon_box) {
		icon_box->SelectBrush(brush);
	}
	g_gui.SelectBrush(brush, TILESET_UNKNOWN);
}

void FindBrushWindow::RefreshResults() {
	result_category->brushlist.clear();

	const std::string search_string = as_lower_str(nstr(search_field->GetValue()));
	const bool do_search = search_string.size() >= 2;

	if (do_search) {
		const BrushMap& brushes_map = g_brushes.getMap();
		for (BrushMap::const_iterator iter = brushes_map.begin(); iter != brushes_map.end(); ++iter) {
			Brush* brush = iter->second;
			if (!brush || brush->isRaw()) {
				continue;
			}
			if (as_lower_str(brush->getName()).find(search_string) == std::string::npos) {
				continue;
			}
			result_category->brushlist.push_back(brush);
		}

		for (int id = 0; id <= g_items.getMaxID(); ++id) {
			ItemType& it = g_items[id];
			if (it.id == 0) {
				continue;
			}

			RAWBrush* raw_brush = it.raw_brush;
			if (!raw_brush) {
				continue;
			}
			if (as_lower_str(raw_brush->getName()).find(search_string) == std::string::npos) {
				continue;
			}
			result_category->brushlist.push_back(raw_brush);
		}
	}

	RebuildIconBox();

	if (!do_search) {
		status_label->SetLabel("Type at least 2 characters to search.");
	} else if (result_category->brushlist.empty()) {
		status_label->SetLabel("No matches found.");
	} else {
		const unsigned long count = static_cast<unsigned long>(result_category->brushlist.size());
		status_label->SetLabel(wxString::Format("%lu match%s", count, count == 1 ? "" : "es"));
	}
}

void FindBrushWindow::RebuildIconBox() {
	if (icon_box) {
		results_sizer->Detach(icon_box);
		icon_box->Destroy();
		icon_box = nullptr;
	}

	if (!result_category->brushlist.empty()) {
		icon_box = newd BrushIconBox(results_panel, result_category, RENDER_SIZE_32x32, true);
		results_sizer->Add(icon_box, 1, wxEXPAND);
		icon_box->SelectFirstBrush();
	}

	results_panel->Layout();
}
