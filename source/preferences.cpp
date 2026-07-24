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

#include "settings.h"
#include "client_version.h"
#include "editor.h"
#include "theme.h"
#include "gui.h"
#include "preferences.h"

namespace {

wxBoxSizer* CreateBrowsePathRow(wxWindow* parent, wxTextCtrl** pathOut, const wxString& initialPath, const wxString& tooltip) {
	auto* rowSizer = newd wxBoxSizer(wxHORIZONTAL);
	*pathOut = newd wxTextCtrl(parent, wxID_ANY, initialPath);
	auto* browseBtn = newd wxButton(parent, wxID_ANY, "Browse...", wxDefaultPosition, wxDefaultSize);
	rowSizer->Add(*pathOut, 1, wxEXPAND);
	rowSizer->Add(browseBtn, 0, wxLEFT, 6);
	(*pathOut)->SetToolTip(tooltip);

	wxTextCtrl* pathCtrl = *pathOut;
	browseBtn->Bind(wxEVT_BUTTON, [parent, pathCtrl](wxCommandEvent&) {
		wxDirDialog dlg(parent, "Select directory", pathCtrl->GetValue());
		if (dlg.ShowModal() == wxID_OK) {
			pathCtrl->SetValue(dlg.GetPath());
		}
	});

	return rowSizer;
}

wxBoxSizer* CreateBrowseFileRow(wxWindow* parent, wxTextCtrl** pathOut, const wxString& initialPath, const wxString& wildcard, const wxString& tooltip) {
	auto* rowSizer = newd wxBoxSizer(wxHORIZONTAL);
	*pathOut = newd wxTextCtrl(parent, wxID_ANY, initialPath);
	auto* browseBtn = newd wxButton(parent, wxID_ANY, "Browse...", wxDefaultPosition, wxDefaultSize);
	rowSizer->Add(*pathOut, 1, wxEXPAND);
	rowSizer->Add(browseBtn, 0, wxLEFT, 6);
	(*pathOut)->SetToolTip(tooltip);

	wxTextCtrl* pathCtrl = *pathOut;
	browseBtn->Bind(wxEVT_BUTTON, [parent, pathCtrl, wildcard](wxCommandEvent&) {
		wxFileDialog dlg(parent, "Select file", "", pathCtrl->GetValue(), wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlg.ShowModal() == wxID_OK) {
			pathCtrl->SetValue(dlg.GetPath());
		}
	});

	return rowSizer;
}

wxString NormalizeDirectoryPath(wxString dir) {
	if (dir.Length() > 0 && dir.Last() != '/' && dir.Last() != '\\') {
		dir.Append(FileName::GetPathSeparator());
	}
	return dir;
}

void AddCheckbox(wxSizer* sizer, wxCheckBox** checkbox, wxWindow* parent, const wxString& label, bool value, const wxString& tooltip = wxEmptyString) {
	*checkbox = newd wxCheckBox(parent, wxID_ANY, label);
	(*checkbox)->SetValue(value);
	if (!tooltip.empty()) {
		(*checkbox)->SetToolTip(tooltip);
	}
	sizer->Add(*checkbox, 0, wxTOP | wxLEFT | wxRIGHT, 4);
}

void AddFormLabel(wxFlexGridSizer* grid, wxWindow* parent, const wxString& label) {
	grid->Add(newd wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
}

} // namespace

BEGIN_EVENT_TABLE(PreferencesWindow, wxDialog)
EVT_BUTTON(wxID_OK, PreferencesWindow::OnClickOK)
EVT_BUTTON(wxID_CANCEL, PreferencesWindow::OnClickCancel)
EVT_BUTTON(wxID_APPLY, PreferencesWindow::OnClickApply)
EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, PreferencesWindow::OnNotebookPageChanged)
END_EVENT_TABLE()

PreferencesWindow::ScrollableTab PreferencesWindow::CreateScrollableTab() {
	ScrollableTab tab;
	tab.page = newd wxPanel(book, wxID_ANY);
	auto* pageSizer = newd wxBoxSizer(wxVERTICAL);
	tab.scroll = newd wxScrolledWindow(tab.page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	tab.scroll->SetScrollRate(10, 10);
	tab.content = newd wxBoxSizer(wxVERTICAL);
	tab.scroll->SetSizer(tab.content);
	pageSizer->Add(tab.scroll, 1, wxEXPAND);
	tab.page->SetSizer(pageSizer);
	return tab;
}

wxStaticBoxSizer* PreferencesWindow::AddSection(wxSizer* parent, wxWindow* window, const wxString& title) {
	auto* section = newd wxStaticBoxSizer(wxVERTICAL, window, title);
	parent->Add(section, 0, wxEXPAND | wxALL, 8);
	return section;
}

wxFlexGridSizer* PreferencesWindow::CreateFormGrid(wxWindow* parent) {
	auto* grid = newd wxFlexGridSizer(2, FROM_DIP(parent, 10), FROM_DIP(parent, 8));
	grid->AddGrowableCol(1);
	return grid;
}

PreferencesWindow::PreferencesWindow(wxWindow* parent, bool clientVersionSelected) :
	wxDialog(parent, wxID_ANY, "Preferences", wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER) {
	Freeze();

	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	book = newd wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_TOP);
	book->AddPage(CreateGeneralPage(), "General", true);
	book->AddPage(CreateEditorPage(), "Editor");
	book->AddPage(CreateGraphicsPage(), "Graphics");
	book->AddPage(CreateUIPage(), "Interface");
	book->AddPage(CreateClientPage(), "Client Version", clientVersionSelected);

	sizer->Add(book, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

	auto* buttonSizer = newd wxBoxSizer(wxHORIZONTAL);
	buttonSizer->AddStretchSpacer();
	buttonSizer->Add(newd wxButton(this, wxID_OK, "OK"), 0, wxRIGHT, 6);
	buttonSizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), 0, wxRIGHT, 6);
	buttonSizer->Add(newd wxButton(this, wxID_APPLY, "Apply"), 0);
	buttonSizer->AddStretchSpacer();
	sizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 12);

	SetSizer(sizer);
	SetMinSize(FROM_DIP(this, wxSize(560, 620)));
	SetSize(FROM_DIP(this, wxSize(580, 640)));
	Centre(wxBOTH);

	if (clientVersionSelected) {
		EnsureClientVersionListBuilt();
	}

	Thaw();
}

void PreferencesWindow::OnNotebookPageChanged(wxBookCtrlEvent& event) {
	if (event.GetSelection() == CLIENT_PAGE_INDEX) {
		EnsureClientVersionListBuilt();
	}
	event.Skip();
}

wxNotebookPage* PreferencesWindow::CreateGeneralPage() {
	const ScrollableTab tab = CreateScrollableTab();
	wxWindow* const panel = tab.scroll;
	wxSizer* const root = tab.content;

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Startup");
		AddCheckbox(section, &show_welcome_dialog_chkbox, panel, "Show welcome dialog on startup", g_settings.getInteger(Config::WELCOME_DIALOG) == 1,
			"Show welcome dialog when starting the editor.");
		AddCheckbox(section, &open_map_on_startup_chkbox, panel, "Always open this map on startup", g_settings.getInteger(Config::OPEN_MAP_ON_STARTUP) == 1,
			"Automatically open the selected map when starting the editor.");

		const wxString map_wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ?
			"(*.otbm;*.otgz)|*.otbm;*.otgz" :
			"(*.otbm)|*.otbm|Compressed OpenTibia Binary Map (*.otgz)|*.otgz";
		auto* mapGrid = CreateFormGrid(panel);
		AddFormLabel(mapGrid, panel, "Startup map:");
		mapGrid->Add(CreateBrowseFileRow(panel, &open_map_on_startup_path, wxstr(g_settings.getString(Config::OPEN_MAP_ON_STARTUP_PATH)), map_wildcard,
			"Map file to open automatically when the editor starts."), 1, wxEXPAND);
		section->Add(mapGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

		const bool open_map_on_startup_enabled = open_map_on_startup_chkbox->GetValue();
		open_map_on_startup_path->Enable(open_map_on_startup_enabled);
		if (open_map_on_startup_enabled) {
			show_welcome_dialog_chkbox->SetValue(false);
			show_welcome_dialog_chkbox->Enable(false);
		}

		open_map_on_startup_chkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
			const bool enabled = event.IsChecked();
			open_map_on_startup_path->Enable(enabled);
			if (enabled) {
				show_welcome_dialog_chkbox->SetValue(false);
				show_welcome_dialog_chkbox->Enable(false);
			} else {
				show_welcome_dialog_chkbox->Enable(true);
			}
		});

		AddCheckbox(section, &enable_tileset_editing_chkbox, panel, "Enable tileset editing", g_settings.getInteger(Config::SHOW_TILESET_EDITOR) == 1,
			"Show tileset editing options.");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Saving and instances");
		AddCheckbox(section, &always_make_backup_chkbox, panel, "Always make map backup", g_settings.getInteger(Config::ALWAYS_MAKE_BACKUP) == 1);
		AddCheckbox(section, &ignore_save_prompt_chkbox, panel, "Skip save confirmation when closing", g_settings.getBoolean(Config::IGNORE_SAVE_PROMPT),
			"When checked, the editor will not prompt to save changes when closing maps.");
		AddCheckbox(section, &auto_save_on_close_chkbox, panel, "Automatically save maps when closing", g_settings.getBoolean(Config::AUTO_SAVE_ON_CLOSE),
			"When checked, the editor will save changes automatically when closing maps.");
		AddCheckbox(section, &only_one_instance_chkbox, panel, "Open all maps in the same instance", g_settings.getInteger(Config::ONLY_ONE_INSTANCE) == 1,
			"When checked, maps opened using the shell will all be opened in the same instance.");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Performance");
		wxStaticText* tmptext = nullptr;
		auto* grid = CreateFormGrid(panel);

		grid->Add(tmptext = newd wxStaticText(panel, wxID_ANY, "Undo queue size:"), 0, wxALIGN_CENTER_VERTICAL);
		undo_size_spin = newd wxSpinCtrl(panel, wxID_ANY, i2ws(g_settings.getInteger(Config::UNDO_SIZE)), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 0x10000000);
		grid->Add(undo_size_spin, 0, wxEXPAND);
		SetWindowToolTip(tmptext, undo_size_spin, "How many actions you can undo. Higher values use more memory.");

		grid->Add(tmptext = newd wxStaticText(panel, wxID_ANY, "Undo memory limit (MB):"), 0, wxALIGN_CENTER_VERTICAL);
		undo_mem_size_spin = newd wxSpinCtrl(panel, wxID_ANY, i2ws(g_settings.getInteger(Config::UNDO_MEM_SIZE)), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 4096);
		grid->Add(undo_mem_size_spin, 0, wxEXPAND);
		SetWindowToolTip(tmptext, undo_mem_size_spin, "Approximate memory limit for the undo queue.");

		grid->Add(tmptext = newd wxStaticText(panel, wxID_ANY, "Worker threads:"), 0, wxALIGN_CENTER_VERTICAL);
		worker_threads_spin = newd wxSpinCtrl(panel, wxID_ANY, i2ws(g_settings.getInteger(Config::WORKER_THREADS)), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 64);
		grid->Add(worker_threads_spin, 0, wxEXPAND);
		SetWindowToolTip(tmptext, worker_threads_spin, "Threads used for intensive operations. Usually match your logical CPU count.");

		grid->Add(tmptext = newd wxStaticText(panel, wxID_ANY, "Replace item limit:"), 0, wxALIGN_CENTER_VERTICAL);
		replace_size_spin = newd wxSpinCtrl(panel, wxID_ANY, i2ws(g_settings.getInteger(Config::REPLACE_SIZE)), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 10000000);
		grid->Add(replace_size_spin, 0, wxEXPAND);
		SetWindowToolTip(tmptext, replace_size_spin, "How many items you can replace using the Replace Item tool.");

		section->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Clipboard");
		auto* grid = CreateFormGrid(panel);
		AddFormLabel(grid, panel, "Copy position format:");
		position_format = newd wxChoice(panel, wxID_ANY);
		position_format->Append("{x = 0, y = 0, z = 0}");
		position_format->Append(R"({"x":0,"y":0,"z":0})");
		position_format->Append("x, y, z");
		position_format->Append("(x, y, z)");
		position_format->Append("Position(x, y, z)");
		position_format->Append("x=\"x\" y=\"y\" z=\"z\"");
		position_format->Append("centerx=\"x\" centery=\"y\" centerz=\"z\"");
		position_format->SetSelection(g_settings.getInteger(Config::COPY_POSITION_FORMAT));
		grid->Add(position_format, 0, wxEXPAND);
		SetWindowToolTip(position_format, "The position format when copying from the map.");
		section->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	}

	tab.scroll->FitInside();
	return tab.page;
}

wxNotebookPage* PreferencesWindow::CreateEditorPage() {
	const ScrollableTab tab = CreateScrollableTab();
	wxWindow* const panel = tab.scroll;
	wxSizer* const root = tab.content;

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Undo");
		AddCheckbox(section, &group_actions_chkbox, panel, "Group same-type actions", g_settings.getBoolean(Config::GROUP_ACTIONS),
			"Group consecutive actions of the same type (drawing, selection, etc.).");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Warnings");
		AddCheckbox(section, &duplicate_id_warn_chkbox, panel, "Warn for duplicate IDs", g_settings.getBoolean(Config::WARN_FOR_DUPLICATE_ID),
			"Warns for most kinds of duplicate IDs.");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Brush behavior");
		AddCheckbox(section, &house_remove_chkbox, panel, "House brush removes items", g_settings.getBoolean(Config::HOUSE_BRUSH_REMOVE_ITEMS),
			"House brush removes items that respawn every time the map is loaded.");
		AddCheckbox(section, &auto_assign_doors_chkbox, panel, "Auto-assign door ids", g_settings.getBoolean(Config::AUTO_ASSIGN_DOORID),
			"Auto-assign unique door ids for doors placed with the door or house brush.\nDoes not affect RAW palette doors.");
		AddCheckbox(section, &doodad_erase_same_chkbox, panel, "Doodad brush only erases same", g_settings.getBoolean(Config::DOODAD_BRUSH_ERASE_LIKE),
			"Doodad brush only erases items belonging to the current brush.");
		AddCheckbox(section, &eraser_leave_unique_chkbox, panel, "Eraser leaves unique items", g_settings.getBoolean(Config::ERASER_LEAVE_UNIQUE),
			"Eraser leaves containers with items, unique/action id items, and special items.");
		AddCheckbox(section, &auto_create_spawn_chkbox, panel, "Auto create spawn when placing creature", g_settings.getBoolean(Config::AUTO_CREATE_SPAWN),
			"Place creatures without manually placing a spawn first.");
		AddCheckbox(section, &allow_multiple_orderitems_chkbox, panel, "Prevent toporder conflict", g_settings.getBoolean(Config::RAW_LIKE_SIMONE),
			"Prevent multiple items with the same toporder on one tile using a RAW brush.");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Selection and live editing");
		AddCheckbox(section, &merge_move_chkbox, panel, "Use merge move", g_settings.getBoolean(Config::MERGE_MOVE),
			"Moved tiles won't replace already placed tiles.");
		AddCheckbox(section, &merge_paste_chkbox, panel, "Use merge paste", g_settings.getBoolean(Config::MERGE_PASTE),
			"Pasted tiles won't replace already placed tiles.");
		AddCheckbox(section, &live_allow_clipboard_chkbox, panel, "Allow copy and paste on live maps", g_settings.getBoolean(Config::LIVE_ALLOW_CLIPBOARD),
			"Enable cut, copy, and paste while connected to a live map server.\nCan also be set as LIVE_ALLOW_CLIPBOARD=1 in editor.cfg.");
	}

	tab.scroll->FitInside();
	return tab.page;
}

wxNotebookPage* PreferencesWindow::CreateGraphicsPage() {
	const ScrollableTab tab = CreateScrollableTab();
	wxWindow* const panel = tab.scroll;
	wxSizer* const root = tab.content;
	wxStaticText* tmp = nullptr;

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Map view");
		AddCheckbox(section, &hide_items_when_zoomed_chkbox, panel, "Hide items when zoomed out", g_settings.getBoolean(Config::HIDE_ITEMS_WHEN_ZOOMED),
			"Hide loose items when zoomed far out.");
		AddCheckbox(section, &show_chunk_boundaries_chkbox, panel, "Show chunk boundaries overlay", g_settings.getBoolean(Config::SHOW_CHUNK_BOUNDARIES),
			"Highlight every 256 tiles to visualize chunk boundaries.");
		AddCheckbox(section, &show_mouse_crosshair_chkbox, panel, "Show mouse crosshair overlay", g_settings.getBoolean(Config::SHOW_MOUSE_CROSSHAIR),
			"Draw guide lines following the cursor to help align tiles.");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Colors");
		auto* grid = CreateFormGrid(panel);

		icon_background_choice = newd wxChoice(panel, wxID_ANY);
		icon_background_choice->Append("Transparent background");
		icon_background_choice->Append("Black background");
		icon_background_choice->Append("Gray background");
		icon_background_choice->Append("White background");
		SetIconBackgroundSelection(IconBackgroundSelection());

		grid->Add(tmp = newd wxStaticText(panel, wxID_ANY, "Icon background:"), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(icon_background_choice, 0, wxEXPAND);
		SetWindowToolTip(icon_background_choice, tmp, "Background color for icons in palette and picker windows.");

		grid->Add(tmp = newd wxStaticText(panel, wxID_ANY, "Cursor color:"), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(cursor_color_pick = newd wxColourPickerCtrl(panel, wxID_ANY, wxColor(g_settings.getInteger(Config::CURSOR_RED), g_settings.getInteger(Config::CURSOR_GREEN), g_settings.getInteger(Config::CURSOR_BLUE), g_settings.getInteger(Config::CURSOR_ALPHA))), 0);
		SetWindowToolTip(cursor_color_pick, tmp, "Main cursor color on the map while drawing.");

		grid->Add(tmp = newd wxStaticText(panel, wxID_ANY, "Secondary cursor color:"), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(cursor_alt_color_pick = newd wxColourPickerCtrl(panel, wxID_ANY, wxColor(g_settings.getInteger(Config::CURSOR_ALT_RED), g_settings.getInteger(Config::CURSOR_ALT_GREEN), g_settings.getInteger(Config::CURSOR_ALT_BLUE), g_settings.getInteger(Config::CURSOR_ALT_ALPHA))), 0);
		SetWindowToolTip(cursor_alt_color_pick, tmp, "Secondary cursor color for houses and flags.");

		grid->Add(tmp = newd wxStaticText(panel, wxID_ANY, "Crosshair color:"), 0, wxALIGN_CENTER_VERTICAL);
		mouse_crosshair_color_pick = newd wxColourPickerCtrl(panel, wxID_ANY, wxColor(
			g_settings.getInteger(Config::CURSOR_CROSSHAIR_RED),
			g_settings.getInteger(Config::CURSOR_CROSSHAIR_GREEN),
			g_settings.getInteger(Config::CURSOR_CROSSHAIR_BLUE)));
		mouse_crosshair_color_pick->Enable(show_mouse_crosshair_chkbox->GetValue());
		grid->Add(mouse_crosshair_color_pick, 0);
		SetWindowToolTip(mouse_crosshair_color_pick, tmp, "Color used for the mouse crosshair overlay.");
		show_mouse_crosshair_chkbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
			mouse_crosshair_color_pick->Enable(event.IsChecked());
		});

		grid->Add(tmp = newd wxStaticText(panel, wxID_ANY, "Viewport background:"), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(viewport_background_color_pick = newd wxColourPickerCtrl(panel, wxID_ANY, wxColor(
			g_settings.getInteger(Config::VIEWPORT_BACKGROUND_RED),
			g_settings.getInteger(Config::VIEWPORT_BACKGROUND_GREEN),
			g_settings.getInteger(Config::VIEWPORT_BACKGROUND_BLUE))), 0);
		SetWindowToolTip(viewport_background_color_pick, tmp, "Solid color behind the map view.");

		section->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Sprites and icons");
		AddCheckbox(section, &icon_selection_shadow_chkbox, panel, "Use icon selection shadow", g_settings.getBoolean(Config::USE_GUI_SELECTION_SHADOW),
			"Shade selected items in palette menus.");
		AddCheckbox(section, &use_memcached_chkbox, panel, "Use memcached sprites", g_settings.getBoolean(Config::USE_MEMCACHED_SPRITES),
			"Load sprites into memory at startup for faster rendering.\nUses more memory but avoids repeated disk reads.");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Screenshots");
		auto* grid = CreateFormGrid(panel);

		grid->Add(tmp = newd wxStaticText(panel, wxID_ANY, "Screenshot directory:"), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(CreateBrowsePathRow(panel, &screenshot_directory_path, wxstr(g_settings.getString(Config::SCREENSHOT_DIRECTORY)),
			"Screenshots taken in the editor will be saved to this directory."), 1, wxEXPAND);
		SetWindowToolTip(screenshot_directory_path, tmp, "Screenshots taken in the editor will be saved to this directory.");

		screenshot_format_choice = newd wxChoice(panel, wxID_ANY);
		screenshot_format_choice->Append("PNG");
		screenshot_format_choice->Append("JPG");
		screenshot_format_choice->Append("TGA");
		screenshot_format_choice->Append("BMP");
		const std::string screenshot_format = g_settings.getString(Config::SCREENSHOT_FORMAT);
		if (screenshot_format == "jpg") {
			screenshot_format_choice->SetSelection(1);
		} else if (screenshot_format == "tga") {
			screenshot_format_choice->SetSelection(2);
		} else if (screenshot_format == "bmp") {
			screenshot_format_choice->SetSelection(3);
		} else {
			screenshot_format_choice->SetSelection(0);
		}

		grid->Add(tmp = newd wxStaticText(panel, wxID_ANY, "Screenshot format:"), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(screenshot_format_choice, 0, wxEXPAND);
		SetWindowToolTip(screenshot_format_choice, tmp, "Screenshot format used by the editor. Press F11 to capture.");

		section->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	}

	tab.scroll->FitInside();
	return tab.page;
}

wxChoice* PreferencesWindow::AddPaletteStyleChoice(wxWindow* parent, wxSizer* sizer, const wxString& short_description, const wxString& description, const std::string& setting) {
	auto* text = newd wxStaticText(parent, wxID_ANY, short_description);
	sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);

	auto* choice = newd wxChoice(parent, wxID_ANY);
	sizer->Add(choice, 0, wxEXPAND);

	choice->Append("Large Icons");
	choice->Append("Small Icons");
	choice->Append("Listbox with Icons");
	choice->Append("Exact Size Icons");

	text->SetToolTip(description);
	choice->SetToolTip(description);

	if (setting == "large icons") {
		choice->SetSelection(0);
	} else if (setting == "small icons") {
		choice->SetSelection(1);
	} else if (setting == "listbox") {
		choice->SetSelection(2);
	} else if (setting == "actual icons") {
		choice->SetSelection(3);
	}

	return choice;
}

void PreferencesWindow::SetPaletteStyleChoice(wxChoice* ctrl, int key) {
	switch (ctrl->GetSelection()) {
		case 0: g_settings.setString(key, "large icons"); break;
		case 1: g_settings.setString(key, "small icons"); break;
		case 2: g_settings.setString(key, "listbox"); break;
		case 3: g_settings.setString(key, "actual icons"); break;
		default: break;
	}
}

int PreferencesWindow::IconBackgroundSelection() const {
	switch (g_settings.getInteger(Config::ICON_BACKGROUND)) {
		case 255: return 3;
		case 88: return 2;
		case 0: return 1;
		default: return 0;
	}
}

void PreferencesWindow::SetIconBackgroundSelection(int selection) {
	icon_background_choice->SetSelection(selection);
}

wxNotebookPage* PreferencesWindow::CreateUIPage() {
	const ScrollableTab tab = CreateScrollableTab();
	wxWindow* const panel = tab.scroll;
	wxSizer* const root = tab.content;

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Appearance");
		auto* grid = CreateFormGrid(panel);
		theme_choice = newd wxChoice(panel, wxID_ANY);
		theme_choice->Append("Dark");
		theme_choice->Append("Light");
		theme_choice->Append("System");
		theme_choice->SetSelection(static_cast<int>(ThemeManager::Get().GetMode()));
		AddFormLabel(grid, panel, "Theme:");
		grid->Add(theme_choice, 0, wxEXPAND);
		section->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Palette layout");
		auto* grid = CreateFormGrid(panel);
		terrain_palette_style_choice = AddPaletteStyleChoice(panel, grid, "Terrain palette:", "Configures the look of the terrain palette.", g_settings.getString(Config::PALETTE_TERRAIN_STYLE));
		doodad_palette_style_choice = AddPaletteStyleChoice(panel, grid, "Doodad palette:", "Configures the look of the doodad palette.", g_settings.getString(Config::PALETTE_DOODAD_STYLE));
		item_palette_style_choice = AddPaletteStyleChoice(panel, grid, "Item palette:", "Configures the look of the item palette.", g_settings.getString(Config::PALETTE_ITEM_STYLE));
		raw_palette_style_choice = AddPaletteStyleChoice(panel, grid, "RAW palette:", "Configures the look of the raw palette.", g_settings.getString(Config::PALETTE_RAW_STYLE));
		section->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Palette panels and icon sizes");
		auto* iconGrid = newd wxFlexGridSizer(2, FROM_DIP(panel, 12), FROM_DIP(panel, 4));
		iconGrid->AddGrowableCol(0);
		iconGrid->AddGrowableCol(1);

		large_terrain_tools_chkbox = newd wxCheckBox(panel, wxID_ANY, "Large terrain tool icons");
		large_terrain_tools_chkbox->SetValue(g_settings.getBoolean(Config::USE_LARGE_TERRAIN_TOOLBAR));
		iconGrid->Add(large_terrain_tools_chkbox, 0, wxEXPAND);

		show_palette_tools_chkbox = newd wxCheckBox(panel, wxID_ANY, "Show Tools panel");
		show_palette_tools_chkbox->SetValue(g_settings.getBoolean(Config::SHOW_PALETTE_TOOLS));
		iconGrid->Add(show_palette_tools_chkbox, 0, wxEXPAND);

		show_palette_brush_size_chkbox = newd wxCheckBox(panel, wxID_ANY, "Show Brush Size panel");
		show_palette_brush_size_chkbox->SetValue(g_settings.getBoolean(Config::SHOW_PALETTE_BRUSH_SIZE));
		iconGrid->Add(show_palette_brush_size_chkbox, 0, wxEXPAND);

		large_doodad_sizebar_chkbox = newd wxCheckBox(panel, wxID_ANY, "Large doodad size icons");
		large_doodad_sizebar_chkbox->SetValue(g_settings.getBoolean(Config::USE_LARGE_DOODAD_SIZEBAR));
		iconGrid->Add(large_doodad_sizebar_chkbox, 0, wxEXPAND);

		large_item_sizebar_chkbox = newd wxCheckBox(panel, wxID_ANY, "Large item size icons");
		large_item_sizebar_chkbox->SetValue(g_settings.getBoolean(Config::USE_LARGE_ITEM_SIZEBAR));
		iconGrid->Add(large_item_sizebar_chkbox, 0, wxEXPAND);

		large_house_sizebar_chkbox = newd wxCheckBox(panel, wxID_ANY, "Large house size icons");
		large_house_sizebar_chkbox->SetValue(g_settings.getBoolean(Config::USE_LARGE_HOUSE_SIZEBAR));
		iconGrid->Add(large_house_sizebar_chkbox, 0, wxEXPAND);

		large_raw_sizebar_chkbox = newd wxCheckBox(panel, wxID_ANY, "Large raw size icons");
		large_raw_sizebar_chkbox->SetValue(g_settings.getBoolean(Config::USE_LARGE_RAW_SIZEBAR));
		iconGrid->Add(large_raw_sizebar_chkbox, 0, wxEXPAND);

		large_container_icons_chkbox = newd wxCheckBox(panel, wxID_ANY, "Large container view icons");
		large_container_icons_chkbox->SetValue(g_settings.getBoolean(Config::USE_LARGE_CONTAINER_ICONS));
		iconGrid->Add(large_container_icons_chkbox, 0, wxEXPAND);

		large_pick_item_icons_chkbox = newd wxCheckBox(panel, wxID_ANY, "Large item picker icons");
		large_pick_item_icons_chkbox->SetValue(g_settings.getBoolean(Config::USE_LARGE_CHOOSE_ITEM_ICONS));
		iconGrid->Add(large_pick_item_icons_chkbox, 0, wxEXPAND);

		section->Add(iconGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
		AddCheckbox(section, &container_default_name_search_chkbox, panel, "Default to \"Find by Name\" in container picker", g_settings.getBoolean(Config::CONTAINER_FIND_DEFAULT_NAMES),
			"Automatically switch to name search when opening the container item picker.");
	}

	{
		wxStaticBoxSizer* const section = AddSection(root, panel, "Mouse and navigation");
		AddCheckbox(section, &switch_mousebtn_chkbox, panel, "Switch mouse buttons", g_settings.getBoolean(Config::SWITCH_MOUSEBUTTONS),
			"Swap right and center mouse button behavior.");
		AddCheckbox(section, &doubleclick_properties_chkbox, panel, "Double click for properties", g_settings.getBoolean(Config::DOUBLECLICK_PROPERTIES),
			"Double clicking a tile opens properties for the top item.");
		AddCheckbox(section, &inversed_scroll_chkbox, panel, "Use inversed scroll", g_settings.getFloat(Config::SCROLL_SPEED) < 0,
			"Invert map dragging with the center mouse button (RTS-style).");

		section->Add(newd wxStaticText(panel, wxID_ANY, "Scroll speed:"), 0, wxTOP | wxLEFT | wxRIGHT, 4);
		const auto true_scrollspeed = int(std::abs(g_settings.getFloat(Config::SCROLL_SPEED)) * 10);
		scroll_speed_slider = newd wxSlider(panel, wxID_ANY, true_scrollspeed, 1, max(true_scrollspeed, 100), wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
		scroll_speed_slider->SetToolTip("How fast the map scrolls when dragging with the center mouse button.");
		section->Add(scroll_speed_slider, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

		section->Add(newd wxStaticText(panel, wxID_ANY, "Zoom speed:"), 0, wxTOP | wxLEFT | wxRIGHT, 4);
		const auto true_zoomspeed = int(g_settings.getFloat(Config::ZOOM_SPEED) * 10);
		zoom_speed_slider = newd wxSlider(panel, wxID_ANY, true_zoomspeed, 1, max(true_zoomspeed, 100), wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
		zoom_speed_slider->SetToolTip("How fast the map zooms when scrolling the mouse wheel.");
		section->Add(zoom_speed_slider, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	}

	tab.scroll->FitInside();
	return tab.page;
}

wxNotebookPage* PreferencesWindow::CreateClientPage() {
	wxNotebookPage* client_page = newd wxPanel(book, wxID_ANY);
	auto* topsizer = newd wxBoxSizer(wxVERTICAL);

	{
		wxStaticBoxSizer* const section = newd wxStaticBoxSizer(wxVERTICAL, client_page, "Client defaults");
		auto* grid = CreateFormGrid(client_page);

		default_version_choice = newd wxChoice(client_page, wxID_ANY);
		wxStaticText* default_client_tooltip = newd wxStaticText(client_page, wxID_ANY, "Default client version:");
		grid->Add(default_client_tooltip, 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(default_version_choice, 0, wxEXPAND);
		SetWindowToolTip(default_client_tooltip, default_version_choice, "Client version used when creating new maps.");

		{
			wxArrayString version_names;
			const ClientVersionList versions = ClientVersion::getAllVisible();
			int default_selection = 0;
			const int default_version_id = g_settings.getInteger(Config::DEFAULT_CLIENT_VERSION);
			for (size_t i = 0; i < versions.size(); ++i) {
				version_names.Add(wxstr(versions[i]->getName()));
				if (versions[i]->getID() == default_version_id) {
					default_selection = static_cast<int>(i);
				}
			}
			default_version_choice->Append(version_names);
			if (!version_names.IsEmpty()) {
				default_version_choice->SetSelection(default_selection);
			}
		}

		section->Add(grid, 0, wxEXPAND | wxALL, 6);
		check_sigs_chkbox = newd wxCheckBox(client_page, wxID_ANY, "Check file signatures");
		check_sigs_chkbox->SetValue(g_settings.getBoolean(Config::CHECK_SIGNATURES));
		check_sigs_chkbox->SetToolTip("When unchecked, the editor loads any OTB/DAT/SPR combination without validation. May cause graphics bugs.");
		section->Add(check_sigs_chkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
		topsizer->Add(section, 0, wxEXPAND | wxALL, 8);
	}

	client_list_window = newd wxScrolledWindow(client_page, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	client_list_sizer = newd wxBoxSizer(wxVERTICAL);
	client_list_window->SetSizer(client_list_sizer);
	client_list_window->SetScrollRate(10, 10);
	topsizer->Add(client_list_window, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	client_page->SetSizer(topsizer);
	return client_page;
}

void PreferencesWindow::EnsureClientVersionListBuilt() {
	if (client_list_built) {
		return;
	}

	wxBusyCursor busy;
	client_list_window->Freeze();

	version_dir_pickers.clear();
	version_items_dir_pickers.clear();
	version_monsters_dir_pickers.clear();
	version_npcs_dir_pickers.clear();

	const ClientVersionList versions = ClientVersion::getAllVisible();

	for (ClientVersion* version : versions) {
		auto* versionSection = newd wxStaticBoxSizer(wxVERTICAL, client_list_window, wxstr(version->getName()));
		auto* grid = CreateFormGrid(client_list_window);

		wxString client_tooltip;
		client_tooltip << "The editor will look for " << wxstr(version->getName()) << " DAT & SPR here.";
		AddFormLabel(grid, client_list_window, "Client data:");
		wxTextCtrl* client_path = nullptr;
		grid->Add(CreateBrowsePathRow(client_list_window, &client_path, version->getClientPath().GetFullPath(), client_tooltip), 1, wxEXPAND);
		version_dir_pickers.push_back(client_path);

		wxString items_tooltip;
		items_tooltip << "Items.otb and items.xml for " << wxstr(version->getName()) << ".";
		AddFormLabel(grid, client_list_window, "Items:");
		const FileName version_items_path = version->hasCustomItemsPath() ? version->getCustomItemsPath() : version->getDataPath();
		wxTextCtrl* items_path = nullptr;
		grid->Add(CreateBrowsePathRow(client_list_window, &items_path, version_items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), items_tooltip), 1, wxEXPAND);
		version_items_dir_pickers.push_back(items_path);

		wxString monsters_tooltip;
		monsters_tooltip << "Monster XML files for " << wxstr(version->getName()) << ".";
		AddFormLabel(grid, client_list_window, "Monsters:");
		const FileName version_monsters_path = version->hasCustomMonstersPath() ? version->getCustomMonstersPath() : FileName();
		wxTextCtrl* monsters_path = nullptr;
		grid->Add(CreateBrowsePathRow(client_list_window, &monsters_path, version_monsters_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), monsters_tooltip), 1, wxEXPAND);
		version_monsters_dir_pickers.push_back(monsters_path);

		wxString npcs_tooltip;
		npcs_tooltip << "NPC XML files for " << wxstr(version->getName()) << ".";
		AddFormLabel(grid, client_list_window, "NPCs:");
		const FileName version_npcs_path = version->hasCustomNpcsPath() ? version->getCustomNpcsPath() : FileName();
		wxTextCtrl* npcs_path = nullptr;
		grid->Add(CreateBrowsePathRow(client_list_window, &npcs_path, version_npcs_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), npcs_tooltip), 1, wxEXPAND);
		version_npcs_dir_pickers.push_back(npcs_path);

		versionSection->Add(grid, 0, wxEXPAND | wxALL, 6);
		client_list_sizer->Add(versionSection, 0, wxEXPAND | wxBOTTOM, 8);
	}

	client_list_window->FitInside();
	client_list_window->Layout();
	client_list_window->Thaw();
	client_list_built = true;
}

void PreferencesWindow::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	if (Apply()) {
		EndModal(0);
	}
}

void PreferencesWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(0);
}

void PreferencesWindow::OnClickApply(wxCommandEvent& WXUNUSED(event)) {
	Apply();
}

bool PreferencesWindow::Apply() {
	const ThemeMode themeMode = ThemeManager::ModeFromChoice(theme_choice->GetSelection());
	if (!ThemeManager::Get().Apply(themeMode, g_gui.root)) {
		wxMessageBox("The selected theme could not be applied.", "Theme", wxOK | wxICON_ERROR, this);
		return false;
	}
	g_settings.setInteger(Config::UI_THEME, static_cast<int>(themeMode));

	bool must_restart = false;
	bool palette_update_needed = false;

	const wxString open_map_path = open_map_on_startup_path->GetValue();
	const bool open_map_on_startup_enabled = open_map_on_startup_chkbox->GetValue() && !open_map_path.empty();

	g_settings.setString(Config::OPEN_MAP_ON_STARTUP_PATH, nstr(open_map_path));
	g_settings.setInteger(Config::OPEN_MAP_ON_STARTUP, open_map_on_startup_enabled ? 1 : 0);

	if (open_map_on_startup_enabled && show_welcome_dialog_chkbox->GetValue() != 0) {
		show_welcome_dialog_chkbox->SetValue(false);
	}

	g_settings.setInteger(Config::WELCOME_DIALOG, open_map_on_startup_enabled ? 0 : show_welcome_dialog_chkbox->GetValue());
	g_settings.setInteger(Config::ALWAYS_MAKE_BACKUP, always_make_backup_chkbox->GetValue());
	g_settings.setInteger(Config::ONLY_ONE_INSTANCE, only_one_instance_chkbox->GetValue());
	g_settings.setInteger(Config::IGNORE_SAVE_PROMPT, ignore_save_prompt_chkbox->GetValue());
	g_settings.setInteger(Config::AUTO_SAVE_ON_CLOSE, auto_save_on_close_chkbox->GetValue());
	g_settings.setInteger(Config::UNDO_SIZE, undo_size_spin->GetValue());
	g_settings.setInteger(Config::UNDO_MEM_SIZE, undo_mem_size_spin->GetValue());
	g_settings.setInteger(Config::WORKER_THREADS, worker_threads_spin->GetValue());
	g_settings.setInteger(Config::REPLACE_SIZE, replace_size_spin->GetValue());
	g_settings.setInteger(Config::COPY_POSITION_FORMAT, position_format->GetSelection());

	if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR) != enable_tileset_editing_chkbox->GetValue()) {
		palette_update_needed = true;
	}
	g_settings.setInteger(Config::SHOW_TILESET_EDITOR, enable_tileset_editing_chkbox->GetValue());

	g_settings.setInteger(Config::GROUP_ACTIONS, group_actions_chkbox->GetValue());
	g_settings.setInteger(Config::WARN_FOR_DUPLICATE_ID, duplicate_id_warn_chkbox->GetValue());
	g_settings.setInteger(Config::HOUSE_BRUSH_REMOVE_ITEMS, house_remove_chkbox->GetValue());
	g_settings.setInteger(Config::AUTO_ASSIGN_DOORID, auto_assign_doors_chkbox->GetValue());
	g_settings.setInteger(Config::ERASER_LEAVE_UNIQUE, eraser_leave_unique_chkbox->GetValue());
	g_settings.setInteger(Config::DOODAD_BRUSH_ERASE_LIKE, doodad_erase_same_chkbox->GetValue());
	g_settings.setInteger(Config::AUTO_CREATE_SPAWN, auto_create_spawn_chkbox->GetValue());
	g_settings.setInteger(Config::RAW_LIKE_SIMONE, allow_multiple_orderitems_chkbox->GetValue());
	g_settings.setInteger(Config::MERGE_MOVE, merge_move_chkbox->GetValue());
	g_settings.setInteger(Config::MERGE_PASTE, merge_paste_chkbox->GetValue());
	g_settings.setInteger(Config::LIVE_ALLOW_CLIPBOARD, live_allow_clipboard_chkbox->GetValue());

	g_settings.setInteger(Config::USE_GUI_SELECTION_SHADOW, icon_selection_shadow_chkbox->GetValue());
	if (g_settings.getBoolean(Config::USE_MEMCACHED_SPRITES) != use_memcached_chkbox->GetValue()) {
		must_restart = true;
	}
	g_settings.setInteger(Config::USE_MEMCACHED_SPRITES_TO_SAVE, use_memcached_chkbox->GetValue());

	static const int icon_background_values[] = { -1, 0, 88, 255 };
	const int icon_background_selection = icon_background_choice->GetSelection();
	if (icon_background_selection >= 0 && icon_background_selection < 4) {
		const int new_background = icon_background_values[icon_background_selection];
		if (g_settings.getInteger(Config::ICON_BACKGROUND) != new_background) {
			g_gui.gfx.cleanSoftwareSprites();
		}
		g_settings.setInteger(Config::ICON_BACKGROUND, new_background);
	}

	g_settings.setString(Config::SCREENSHOT_DIRECTORY, nstr(screenshot_directory_path->GetValue()));

	const std::string new_format = nstr(screenshot_format_choice->GetStringSelection());
	if (new_format == "PNG") {
		g_settings.setString(Config::SCREENSHOT_FORMAT, "png");
	} else if (new_format == "TGA") {
		g_settings.setString(Config::SCREENSHOT_FORMAT, "tga");
	} else if (new_format == "JPG") {
		g_settings.setString(Config::SCREENSHOT_FORMAT, "jpg");
	} else if (new_format == "BMP") {
		g_settings.setString(Config::SCREENSHOT_FORMAT, "bmp");
	}

	wxColor clr = cursor_color_pick->GetColour();
	g_settings.setInteger(Config::CURSOR_RED, clr.Red());
	g_settings.setInteger(Config::CURSOR_GREEN, clr.Green());
	g_settings.setInteger(Config::CURSOR_BLUE, clr.Blue());

	clr = cursor_alt_color_pick->GetColour();
	g_settings.setInteger(Config::CURSOR_ALT_RED, clr.Red());
	g_settings.setInteger(Config::CURSOR_ALT_GREEN, clr.Green());
	g_settings.setInteger(Config::CURSOR_ALT_BLUE, clr.Blue());

	clr = mouse_crosshair_color_pick->GetColour();
	g_settings.setInteger(Config::CURSOR_CROSSHAIR_RED, clr.Red());
	g_settings.setInteger(Config::CURSOR_CROSSHAIR_GREEN, clr.Green());
	g_settings.setInteger(Config::CURSOR_CROSSHAIR_BLUE, clr.Blue());

	clr = viewport_background_color_pick->GetColour();
	g_settings.setInteger(Config::VIEWPORT_BACKGROUND_RED, clr.Red());
	g_settings.setInteger(Config::VIEWPORT_BACKGROUND_GREEN, clr.Green());
	g_settings.setInteger(Config::VIEWPORT_BACKGROUND_BLUE, clr.Blue());

	g_settings.setInteger(Config::HIDE_ITEMS_WHEN_ZOOMED, hide_items_when_zoomed_chkbox->GetValue());
	g_settings.setInteger(Config::SHOW_CHUNK_BOUNDARIES, show_chunk_boundaries_chkbox->GetValue());
	g_settings.setInteger(Config::SHOW_MOUSE_CROSSHAIR, show_mouse_crosshair_chkbox->GetValue());

	SetPaletteStyleChoice(terrain_palette_style_choice, Config::PALETTE_TERRAIN_STYLE);
	SetPaletteStyleChoice(doodad_palette_style_choice, Config::PALETTE_DOODAD_STYLE);
	SetPaletteStyleChoice(item_palette_style_choice, Config::PALETTE_ITEM_STYLE);
	SetPaletteStyleChoice(raw_palette_style_choice, Config::PALETTE_RAW_STYLE);
	g_settings.setInteger(Config::USE_LARGE_TERRAIN_TOOLBAR, large_terrain_tools_chkbox->GetValue());
	g_settings.setInteger(Config::SHOW_PALETTE_TOOLS, show_palette_tools_chkbox->GetValue());
	g_settings.setInteger(Config::SHOW_PALETTE_BRUSH_SIZE, show_palette_brush_size_chkbox->GetValue());
	g_settings.setInteger(Config::USE_LARGE_DOODAD_SIZEBAR, large_doodad_sizebar_chkbox->GetValue());
	g_settings.setInteger(Config::USE_LARGE_ITEM_SIZEBAR, large_item_sizebar_chkbox->GetValue());
	g_settings.setInteger(Config::USE_LARGE_HOUSE_SIZEBAR, large_house_sizebar_chkbox->GetValue());
	g_settings.setInteger(Config::USE_LARGE_RAW_SIZEBAR, large_raw_sizebar_chkbox->GetValue());
	g_settings.setInteger(Config::USE_LARGE_CONTAINER_ICONS, large_container_icons_chkbox->GetValue());
	g_settings.setInteger(Config::USE_LARGE_CHOOSE_ITEM_ICONS, large_pick_item_icons_chkbox->GetValue());
	g_settings.setInteger(Config::CONTAINER_FIND_DEFAULT_NAMES, container_default_name_search_chkbox->GetValue());

	g_settings.setInteger(Config::SWITCH_MOUSEBUTTONS, switch_mousebtn_chkbox->GetValue());
	g_settings.setInteger(Config::DOUBLECLICK_PROPERTIES, doubleclick_properties_chkbox->GetValue());

	float scroll_mul = 1.0f;
	if (inversed_scroll_chkbox->GetValue()) {
		scroll_mul = -1.0f;
	}
	g_settings.setFloat(Config::SCROLL_SPEED, scroll_mul * scroll_speed_slider->GetValue() / 10.f);
	g_settings.setFloat(Config::ZOOM_SPEED, zoom_speed_slider->GetValue() / 10.f);

	if (client_list_built) {
		const ClientVersionList versions = ClientVersion::getAllVisible();
		int version_counter = 0;
		for (ClientVersion* version : versions) {
			version->setClientPath(FileName(NormalizeDirectoryPath(version_dir_pickers[version_counter]->GetValue())));

			const wxString items_dir = NormalizeDirectoryPath(version_items_dir_pickers[version_counter]->GetValue());
			const wxString default_items_dir = version->getDataPath().GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			if (items_dir.IsEmpty() || items_dir.CmpNoCase(default_items_dir) == 0) {
				version->clearItemsPath();
			} else {
				FileName items_path;
				items_path.Assign(items_dir);
				version->setItemsPath(items_path);
			}

			const wxString monsters_dir = NormalizeDirectoryPath(version_monsters_dir_pickers[version_counter]->GetValue());
			if (monsters_dir.IsEmpty()) {
				version->clearMonstersPath();
			} else {
				FileName monsters_path;
				monsters_path.Assign(monsters_dir);
				version->setMonstersPath(monsters_path);
			}

			const wxString npcs_dir = NormalizeDirectoryPath(version_npcs_dir_pickers[version_counter]->GetValue());
			if (npcs_dir.IsEmpty()) {
				version->clearNpcsPath();
			} else {
				FileName npcs_path;
				npcs_path.Assign(npcs_dir);
				version->setNpcsPath(npcs_path);
			}

			++version_counter;
		}
	}

	const std::string selected_version_name = nstr(default_version_choice->GetStringSelection());
	for (ClientVersion* version : ClientVersion::getAllVisible()) {
		if (version->getName() == selected_version_name) {
			g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, version->getID());
			break;
		}
	}

	g_settings.setInteger(Config::CHECK_SIGNATURES, check_sigs_chkbox->GetValue());

	ClientVersion::saveVersions();
	ClientVersion::loadVersions();

	g_settings.save();

	if (must_restart) {
		g_gui.PopupDialog(this, "Notice", "You must restart the editor for the changes to take effect.", wxOK);
	}

	if (!palette_update_needed) {
		g_gui.RebuildPalettes();
	} else {
		wxString error;
		wxArrayString warnings;
		g_gui.LoadVersion(g_gui.GetCurrentVersionID(), error, warnings, true);
		g_gui.PopupDialog("Error", error, wxOK);
		g_gui.ListDialog("Warnings", warnings);
	}

	return true;
}
