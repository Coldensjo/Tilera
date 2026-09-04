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

#ifndef RME_PREFERENCES_WINDOW_H_
#define RME_PREFERENCES_WINDOW_H_

#include "main.h"
#include <wx/clrpicker.h>

class PreferencesWindow : public wxDialog {
public:
	explicit PreferencesWindow(wxWindow* parent) : PreferencesWindow(parent, false) { }
	PreferencesWindow(wxWindow* parent, bool clientVersionSelected);
	~PreferencesWindow() override = default;

	void OnClickApply(wxCommandEvent&);
	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);
	void OnNotebookPageChanged(wxBookCtrlEvent&);

protected:
	struct ScrollableTab {
		wxPanel* page = nullptr;
		wxScrolledWindow* scroll = nullptr;
		wxBoxSizer* content = nullptr;
	};

	bool Apply();
	void EnsureClientVersionListBuilt();
	ScrollableTab CreateScrollableTab();
	wxStaticBoxSizer* AddSection(wxSizer* parent, wxWindow* window, const wxString& title);
	wxFlexGridSizer* CreateFormGrid(wxWindow* parent);

	wxChoice* AddPaletteStyleChoice(wxWindow* parent, wxSizer* sizer, const wxString& short_description, const wxString& description, const std::string& setting);
	void SetPaletteStyleChoice(wxChoice* ctrl, int key);
	int IconBackgroundSelection() const;
	void SetIconBackgroundSelection(int selection);

	static constexpr int CLIENT_PAGE_INDEX = 4;

	wxBookCtrl* book = nullptr;
	bool client_list_built = false;
	wxScrolledWindow* client_list_window = nullptr;
	wxBoxSizer* client_list_sizer = nullptr;

	// General
	wxCheckBox* always_make_backup_chkbox = nullptr;
	wxCheckBox* only_one_instance_chkbox = nullptr;
	wxCheckBox* show_welcome_dialog_chkbox = nullptr;
	wxCheckBox* ignore_save_prompt_chkbox = nullptr;
	wxCheckBox* auto_save_on_close_chkbox = nullptr;
	wxCheckBox* enable_tileset_editing_chkbox = nullptr;
	wxCheckBox* open_map_on_startup_chkbox = nullptr;
	wxTextCtrl* open_map_on_startup_path = nullptr;
	wxSpinCtrl* undo_size_spin = nullptr;
	wxSpinCtrl* undo_mem_size_spin = nullptr;
	wxSpinCtrl* worker_threads_spin = nullptr;
	wxSpinCtrl* replace_size_spin = nullptr;
	wxChoice* position_format = nullptr;

	// Editor
	wxCheckBox* group_actions_chkbox = nullptr;
	wxCheckBox* duplicate_id_warn_chkbox = nullptr;
	wxCheckBox* house_remove_chkbox = nullptr;
	wxCheckBox* auto_assign_doors_chkbox = nullptr;
	wxCheckBox* eraser_leave_unique_chkbox = nullptr;
	wxCheckBox* doodad_erase_same_chkbox = nullptr;
	wxCheckBox* auto_create_spawn_chkbox = nullptr;
	wxCheckBox* allow_multiple_orderitems_chkbox = nullptr;
	wxCheckBox* merge_move_chkbox = nullptr;
	wxCheckBox* merge_paste_chkbox = nullptr;
	wxCheckBox* live_allow_clipboard_chkbox = nullptr;

	// Graphics
	wxCheckBox* icon_selection_shadow_chkbox = nullptr;
	wxChoice* icon_background_choice = nullptr;
	wxCheckBox* use_memcached_chkbox = nullptr;
	wxTextCtrl* screenshot_directory_path = nullptr;
	wxChoice* screenshot_format_choice = nullptr;
	wxCheckBox* hide_items_when_zoomed_chkbox = nullptr;
	wxCheckBox* show_chunk_boundaries_chkbox = nullptr;
	wxCheckBox* show_mouse_crosshair_chkbox = nullptr;
	wxColourPickerCtrl* cursor_color_pick = nullptr;
	wxColourPickerCtrl* cursor_alt_color_pick = nullptr;
	wxColourPickerCtrl* mouse_crosshair_color_pick = nullptr;
	wxColourPickerCtrl* viewport_background_color_pick = nullptr;

	// Interface
	wxChoice* theme_choice = nullptr;
	wxCheckBox* flat_chrome_chkbox = nullptr;
	wxChoice* terrain_palette_style_choice = nullptr;
	wxChoice* doodad_palette_style_choice = nullptr;
	wxChoice* item_palette_style_choice = nullptr;
	wxChoice* raw_palette_style_choice = nullptr;

	wxCheckBox* large_terrain_tools_chkbox = nullptr;
	wxCheckBox* show_palette_tools_chkbox = nullptr;
	wxCheckBox* show_palette_brush_size_chkbox = nullptr;
	wxCheckBox* large_doodad_sizebar_chkbox = nullptr;
	wxCheckBox* large_item_sizebar_chkbox = nullptr;
	wxCheckBox* large_house_sizebar_chkbox = nullptr;
	wxCheckBox* large_raw_sizebar_chkbox = nullptr;
	wxCheckBox* large_container_icons_chkbox = nullptr;
	wxCheckBox* large_pick_item_icons_chkbox = nullptr;
	wxCheckBox* container_default_name_search_chkbox = nullptr;

	wxCheckBox* switch_mousebtn_chkbox = nullptr;
	wxCheckBox* doubleclick_properties_chkbox = nullptr;
	wxCheckBox* inversed_scroll_chkbox = nullptr;
	wxSlider* scroll_speed_slider = nullptr;
	wxSlider* zoom_speed_slider = nullptr;

	// Client info
	wxChoice* default_version_choice = nullptr;
	std::vector<wxTextCtrl*> version_dir_pickers;
	std::vector<wxTextCtrl*> version_items_dir_pickers;
	std::vector<wxTextCtrl*> version_monsters_dir_pickers;
	std::vector<wxTextCtrl*> version_npcs_dir_pickers;
	wxCheckBox* check_sigs_chkbox = nullptr;

	wxNotebookPage* CreateGeneralPage();
	wxNotebookPage* CreateGraphicsPage();
	wxNotebookPage* CreateUIPage();
	wxNotebookPage* CreateEditorPage();
	wxNotebookPage* CreateClientPage();

	DECLARE_EVENT_TABLE()
};

#endif
