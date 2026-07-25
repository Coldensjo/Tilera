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
#include "gui.h"
#include "brush.h"
#include "map_display.h"

#include "palette_window.h"
#include "palette_brushlist.h"
#include "palette_house.h"
#include "palette_creature.h"
#include "palette_button.h"

#include "house_brush.h"
#include "map.h"
#include "editor.h"
#include "pngfiles.h"

// ============================================================================
// Palette window

BEGIN_EVENT_TABLE(PaletteWindow, wxPanel)
EVT_BUTTON(PALETTE_BUTTON_TERRAIN, PaletteWindow::OnButtonClick)
EVT_BUTTON(PALETTE_BUTTON_DOODAD, PaletteWindow::OnButtonClick)
EVT_BUTTON(PALETTE_BUTTON_ITEM, PaletteWindow::OnButtonClick)
EVT_BUTTON(PALETTE_BUTTON_HOUSE, PaletteWindow::OnButtonClick)
EVT_BUTTON(PALETTE_BUTTON_CREATURE, PaletteWindow::OnButtonClick)
EVT_BUTTON(PALETTE_BUTTON_RAW, PaletteWindow::OnButtonClick)
EVT_CLOSE(PaletteWindow::OnClose)

EVT_KEY_DOWN(PaletteWindow::OnKey)
END_EVENT_TABLE()

PaletteWindow::PaletteWindow(wxWindow* parent, const TilesetContainer& tilesets) :
	// wxCLIP_CHILDREN (MSW: WS_CLIPCHILDREN) is required on every container in the
	// palette hierarchy. Without it a Refresh() on this window erases its whole
	// client area - including the rectangles occupied by the child buttons and
	// panels - while those children are *not* invalidated, so their icons stay
	// blank until something else (e.g. a mouse hover) invalidates them.
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(450, 250), wxTAB_TRAVERSAL | wxCLIP_CHILDREN),
	page_container(nullptr),
	terrain_button(nullptr),
	doodad_button(nullptr),
	item_button(nullptr),
	house_button(nullptr),
	creature_button(nullptr),
	raw_button(nullptr),
	comments_button(nullptr),
	terrain_palette(nullptr),
	doodad_palette(nullptr),
	item_palette(nullptr),
	creature_palette(nullptr),
	house_palette(nullptr),
	raw_palette(nullptr),
	current_page(TILESET_UNKNOWN) {
	SetMinSize(wxSize(450, 250));

	// Create page container
	page_container = newd wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN);

	// Create palette panels
	terrain_palette = static_cast<BrushPalettePanel*>(CreateTerrainPalette(page_container, tilesets));
	doodad_palette = static_cast<BrushPalettePanel*>(CreateDoodadPalette(page_container, tilesets));
	item_palette = static_cast<BrushPalettePanel*>(CreateItemPalette(page_container, tilesets));
	house_palette = static_cast<HousePalettePanel*>(CreateHousePalette(page_container, tilesets));
	creature_palette = static_cast<CreaturePalettePanel*>(CreateCreaturePalette(page_container, tilesets));
	raw_palette = static_cast<BrushPalettePanel*>(CreateRAWPalette(page_container, tilesets));

	// Create buttons
	terrain_button = newd PaletteButton(this, PALETTE_BUTTON_TERRAIN, "&Terrain", PNG_BITMAP(icon_terrain_png));
	doodad_button = newd PaletteButton(this, PALETTE_BUTTON_DOODAD, "&Doodad", PNG_BITMAP(icon_doodad_png));
	item_button = newd PaletteButton(this, PALETTE_BUTTON_ITEM, "&Item", PNG_BITMAP(icon_item_png));
	house_button = newd PaletteButton(this, PALETTE_BUTTON_HOUSE, "&House", PNG_BITMAP(icon_house_png));
	creature_button = newd PaletteButton(this, PALETTE_BUTTON_CREATURE, "&Creature", PNG_BITMAP(icon_creature_png));
	raw_button = newd PaletteButton(this, PALETTE_BUTTON_RAW, "&RAW", PNG_BITMAP(icon_raw_png));

	// Give all category buttons the same size so they line up neatly, since
	// each button otherwise sizes itself to fit its own label text.
	{
		PaletteButton* category_buttons[] = { terrain_button, doodad_button, item_button, house_button, creature_button, raw_button };
		wxSize uniform_size(0, 0);
		for (PaletteButton* button : category_buttons) {
			uniform_size.IncTo(button->GetMinSize());
		}
		for (PaletteButton* button : category_buttons) {
			button->SetMinSize(uniform_size);
		}
	}

	// Setup sizers
	wxBoxSizer* main_sizer = newd wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* button_sizer = newd wxBoxSizer(wxVERTICAL);
	wxBoxSizer* page_sizer = newd wxBoxSizer(wxVERTICAL);

	// Add buttons to button sizer
	button_sizer->Add(terrain_button, 0, wxALL, 2);
	button_sizer->Add(doodad_button, 0, wxALL, 2);
	button_sizer->Add(item_button, 0, wxALL, 2);
	button_sizer->Add(house_button, 0, wxALL, 2);
	button_sizer->Add(creature_button, 0, wxALL, 2);
	button_sizer->Add(raw_button, 0, wxALL, 2);

	// Live-mapping-only shortcut: browse all map comments, see who left them,
	// jump to one, and edit/delete it. Hidden until OnUpdate finds a live editor.
	comments_button = newd wxBitmapButton(this, wxID_ANY, PNG_BITMAP(icon_comments_png));
	comments_button->SetToolTip("Map Comments");
	comments_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (g_gui.IsCommentsWindowShown()) {
			g_gui.HideCommentsWindow();
		} else {
			g_gui.ShowCommentsWindow();
		}
	});
	comments_button->Hide();
	button_sizer->Add(comments_button, 0, wxALL, 2);

	// Add palette panels to page sizer (initially hidden)
	page_sizer->Add(terrain_palette, 1, wxEXPAND);
	page_sizer->Add(doodad_palette, 1, wxEXPAND);
	page_sizer->Add(item_palette, 1, wxEXPAND);
	page_sizer->Add(house_palette, 1, wxEXPAND);
	page_sizer->Add(creature_palette, 1, wxEXPAND);
	page_sizer->Add(raw_palette, 1, wxEXPAND);

	page_container->SetSizer(page_sizer);

	// Hide all panels initially
	terrain_palette->Hide();
	doodad_palette->Hide();
	item_palette->Hide();
	house_palette->Hide();
	creature_palette->Hide();
	raw_palette->Hide();

	// Add to main sizer
	main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, 2);
	main_sizer->Add(page_container, 1, wxEXPAND | wxALL, 2);
	SetSizer(main_sizer);

	// Show first page (Terrain)
	ShowPalettePage(TILESET_TERRAIN);

	// Load first page
	LoadCurrentContents();

	Fit();
}

PaletteWindow::~PaletteWindow() {
	////
}

PalettePanel* PaletteWindow::CreateTerrainPalette(wxWindow* parent, const TilesetContainer& tilesets) {
	BrushPalettePanel* panel = newd BrushPalettePanel(parent, tilesets, TILESET_TERRAIN);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_TERRAIN_STYLE)));

	BrushToolPanel* tool_panel = newd BrushToolPanel(panel);
	tool_panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_TERRAIN_TOOLBAR));
	panel->AddToolPanel(tool_panel);

	BrushSizePanel* size_panel = newd BrushSizePanel(panel);
	size_panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_TERRAIN_TOOLBAR));
	panel->AddToolPanel(size_panel);

	return panel;
}

PalettePanel* PaletteWindow::CreateDoodadPalette(wxWindow* parent, const TilesetContainer& tilesets) {
	BrushPalettePanel* panel = newd BrushPalettePanel(parent, tilesets, TILESET_DOODAD);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_DOODAD_STYLE)));

	panel->AddToolPanel(newd BrushThicknessPanel(panel));

	BrushSizePanel* size_panel = newd BrushSizePanel(panel);
	size_panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_DOODAD_SIZEBAR));
	panel->AddToolPanel(size_panel);

	return panel;
}

PalettePanel* PaletteWindow::CreateItemPalette(wxWindow* parent, const TilesetContainer& tilesets) {
	BrushPalettePanel* panel = newd BrushPalettePanel(parent, tilesets, TILESET_ITEM);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_ITEM_STYLE)));

	BrushSizePanel* size_panel = newd BrushSizePanel(panel);
	size_panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_ITEM_SIZEBAR));
	panel->AddToolPanel(size_panel);
	return panel;
}

PalettePanel* PaletteWindow::CreateHousePalette(wxWindow* parent, const TilesetContainer& tilesets) {
	HousePalettePanel* panel = newd HousePalettePanel(parent);

	BrushSizePanel* size_panel = newd BrushSizePanel(panel);
	size_panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_HOUSE_SIZEBAR));
	panel->AddToolPanel(size_panel);
	return panel;
}

PalettePanel* PaletteWindow::CreateCreaturePalette(wxWindow* parent, const TilesetContainer& tilesets) {
	CreaturePalettePanel* panel = newd CreaturePalettePanel(parent);
	return panel;
}

PalettePanel* PaletteWindow::CreateRAWPalette(wxWindow* parent, const TilesetContainer& tilesets) {
	BrushPalettePanel* panel = newd BrushPalettePanel(parent, tilesets, TILESET_RAW);
	panel->SetListType(wxstr(g_settings.getString(Config::PALETTE_RAW_STYLE)));

	BrushSizePanel* size_panel = newd BrushSizePanel(panel);
	size_panel->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_RAW_SIZEBAR));
	panel->AddToolPanel(size_panel);

	return panel;
}

void PaletteWindow::ReloadSettings(Map* map) {
	if (terrain_palette) {
		terrain_palette->SetListType(wxstr(g_settings.getString(Config::PALETTE_TERRAIN_STYLE)));
		terrain_palette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_TERRAIN_TOOLBAR));
	}
	if (doodad_palette) {
		doodad_palette->SetListType(wxstr(g_settings.getString(Config::PALETTE_DOODAD_STYLE)));
		doodad_palette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_DOODAD_SIZEBAR));
	}
	if (house_palette) {
		house_palette->SetMap(map);
		house_palette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_HOUSE_SIZEBAR));
	}
	if (item_palette) {
		item_palette->SetListType(wxstr(g_settings.getString(Config::PALETTE_ITEM_STYLE)));
		item_palette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_ITEM_SIZEBAR));
	}
	if (raw_palette) {
		raw_palette->SetListType(wxstr(g_settings.getString(Config::PALETTE_RAW_STYLE)));
		raw_palette->SetToolbarIconSize(g_settings.getBoolean(Config::USE_LARGE_RAW_SIZEBAR));
	}
	InvalidateContents();
	UpdateToolPanelVisibility();
}

void PaletteWindow::UpdateToolPanelVisibility() {
	auto updatePanel = [](PalettePanel* panel) {
		if (panel) {
			panel->UpdateToolPanelVisibility();
		}
	};

	updatePanel(terrain_palette);
	updatePanel(doodad_palette);
	updatePanel(item_palette);
	updatePanel(house_palette);
	updatePanel(creature_palette);
	updatePanel(raw_palette);

	if (page_container) {
		page_container->Layout();
	}
	Layout();
	Fit();
}

void PaletteWindow::ShowPalettePage(PaletteType type) {
	if (current_page == type) {
		return;
	}

	// Hide current page
	PalettePanel* old_panel = nullptr;
	switch (current_page) {
		case TILESET_TERRAIN:
			old_panel = terrain_palette;
			break;
		case TILESET_DOODAD:
			old_panel = doodad_palette;
			break;
		case TILESET_ITEM:
			old_panel = item_palette;
			break;
		case TILESET_HOUSE:
			old_panel = house_palette;
			break;
		case TILESET_CREATURE:
			old_panel = creature_palette;
			break;
		case TILESET_RAW:
			old_panel = raw_palette;
			break;
		default:
			break;
	}
	if (old_panel) {
		old_panel->OnSwitchOut();
		old_panel->Hide();
	}

	// Show new page
	PalettePanel* new_panel = nullptr;
	switch (type) {
		case TILESET_TERRAIN:
			new_panel = terrain_palette;
			break;
		case TILESET_DOODAD:
			new_panel = doodad_palette;
			break;
		case TILESET_ITEM:
			new_panel = item_palette;
			break;
		case TILESET_HOUSE:
			new_panel = house_palette;
			break;
		case TILESET_CREATURE:
			new_panel = creature_palette;
			break;
		case TILESET_RAW:
			new_panel = raw_palette;
			break;
		default:
			return;
	}

	if (new_panel) {
		new_panel->Show();
		new_panel->OnSwitchIn();
		current_page = type;
		UpdateCategoryButtonStates();
		page_container->Layout();
		Layout();
		Refresh();
	}
}

void PaletteWindow::UpdateCategoryButtonStates() {
	if (terrain_button) {
		terrain_button->SetActive(current_page == TILESET_TERRAIN);
	}
	if (doodad_button) {
		doodad_button->SetActive(current_page == TILESET_DOODAD);
	}
	if (item_button) {
		item_button->SetActive(current_page == TILESET_ITEM);
	}
	if (house_button) {
		house_button->SetActive(current_page == TILESET_HOUSE);
	}
	if (creature_button) {
		creature_button->SetActive(current_page == TILESET_CREATURE);
	}
	if (raw_button) {
		raw_button->SetActive(current_page == TILESET_RAW);
	}
}

void PaletteWindow::LoadCurrentContents() {
	PalettePanel* panel = nullptr;
	switch (current_page) {
		case TILESET_TERRAIN:
			panel = terrain_palette;
			break;
		case TILESET_DOODAD:
			panel = doodad_palette;
			break;
		case TILESET_ITEM:
			panel = item_palette;
			break;
		case TILESET_HOUSE:
			panel = house_palette;
			break;
		case TILESET_CREATURE:
			panel = creature_palette;
			break;
		case TILESET_RAW:
			panel = raw_palette;
			break;
		default:
			return;
	}
	if (panel) {
		panel->LoadCurrentContents();
		Fit();
		Refresh();
		// Removed Update() - let paint events process asynchronously for better responsiveness
	}
}

void PaletteWindow::InvalidateContents() {
	if (terrain_palette) {
		terrain_palette->InvalidateContents();
	}
	if (doodad_palette) {
		doodad_palette->InvalidateContents();
	}
	if (item_palette) {
		item_palette->InvalidateContents();
	}
	if (house_palette) {
		house_palette->InvalidateContents();
	}
	if (creature_palette) {
		creature_palette->InvalidateContents();
	}
	if (raw_palette) {
		raw_palette->InvalidateContents();
	}
	LoadCurrentContents();
	if (creature_palette) {
		creature_palette->OnUpdate();
	}
	if (house_palette) {
		house_palette->OnUpdate();
	}
}

bool PaletteWindow::RefreshTilesetPage(TilesetCategoryType type, const std::string& tilesetName) {
	BrushPalettePanel* panel = nullptr;
	switch (type) {
		case TILESET_TERRAIN:
			panel = terrain_palette;
			break;
		case TILESET_DOODAD:
			panel = doodad_palette;
			break;
		case TILESET_ITEM:
			panel = item_palette;
			break;
		case TILESET_RAW:
			panel = raw_palette;
			break;
		default:
			break;
	}

	return panel ? panel->RefreshTilesetPage(tilesetName) : false;
}

void PaletteWindow::SelectPage(PaletteType id) {
	if (id == GetSelectedPage()) {
		return;
	}
	ShowPalettePage(id);
}

Brush* PaletteWindow::GetSelectedBrush() const {
	PalettePanel* panel = nullptr;
	switch (current_page) {
		case TILESET_TERRAIN:
			panel = terrain_palette;
			break;
		case TILESET_DOODAD:
			panel = doodad_palette;
			break;
		case TILESET_ITEM:
			panel = item_palette;
			break;
		case TILESET_HOUSE:
			panel = house_palette;
			break;
		case TILESET_CREATURE:
			panel = creature_palette;
			break;
		case TILESET_RAW:
			panel = raw_palette;
			break;
		default:
			return nullptr;
	}
	if (panel) {
		return panel->GetSelectedBrush();
	}
	return nullptr;
}

int PaletteWindow::GetSelectedBrushSize() const {
	PalettePanel* panel = nullptr;
	switch (current_page) {
		case TILESET_TERRAIN:
			panel = terrain_palette;
			break;
		case TILESET_DOODAD:
			panel = doodad_palette;
			break;
		case TILESET_ITEM:
			panel = item_palette;
			break;
		case TILESET_HOUSE:
			panel = house_palette;
			break;
		case TILESET_CREATURE:
			panel = creature_palette;
			break;
		case TILESET_RAW:
			panel = raw_palette;
			break;
		default:
			return 0;
	}
	if (panel) {
		return panel->GetSelectedBrushSize();
	}
	return 0;
}

PaletteType PaletteWindow::GetSelectedPage() const {
	return current_page;
}

bool PaletteWindow::OnSelectBrush(const Brush* whatbrush, PaletteType primary) {
	if (!whatbrush) {
		return false;
	}

	if (whatbrush->isHouse() && house_palette) {
		house_palette->SelectBrush(whatbrush);
		SelectPage(TILESET_HOUSE);
		return true;
	}

	switch (primary) {
		case TILESET_TERRAIN: {
			// This is already searched first
			break;
		}
		case TILESET_DOODAD: {
			// Ok, search doodad before terrain
			if (doodad_palette && doodad_palette->SelectBrush(whatbrush)) {
				SelectPage(TILESET_DOODAD);
				return true;
			}
			break;
		}
		case TILESET_ITEM: {
			if (item_palette && item_palette->SelectBrush(whatbrush)) {
				SelectPage(TILESET_ITEM);
				return true;
			}
			break;
		}
		case TILESET_CREATURE: {
			if (creature_palette && creature_palette->SelectBrush(whatbrush)) {
				SelectPage(TILESET_CREATURE);
				return true;
			}
			break;
		}
		case TILESET_RAW: {
			if (raw_palette && raw_palette->SelectBrush(whatbrush)) {
				SelectPage(TILESET_RAW);
				return true;
			}
			break;
		}
		default:
			break;
	}

	// Test if it's a terrain brush
	if (terrain_palette && terrain_palette->SelectBrush(whatbrush)) {
		SelectPage(TILESET_TERRAIN);
		return true;
	}

	// Test if it's a doodad brush
	if (primary != TILESET_DOODAD) {
		if (doodad_palette && doodad_palette->SelectBrush(whatbrush)) {
			SelectPage(TILESET_DOODAD);
			return true;
		}
	}

	// Test if it's an item brush
	if (primary != TILESET_ITEM) {
		if (item_palette && item_palette->SelectBrush(whatbrush)) {
			SelectPage(TILESET_ITEM);
			return true;
		}
	}

	// Test if it's a creature brush
	if (primary != TILESET_CREATURE) {
		if (creature_palette && creature_palette->SelectBrush(whatbrush)) {
			SelectPage(TILESET_CREATURE);
			return true;
		}
	}

	// Test if it's a raw brush
	if (primary != TILESET_RAW) {
		if (raw_palette && raw_palette->SelectBrush(whatbrush)) {
			SelectPage(TILESET_RAW);
			return true;
		}
	}

	return false;
}

void PaletteWindow::OnButtonClick(wxCommandEvent& event) {
	PaletteType type = TILESET_UNKNOWN;
	switch (event.GetId()) {
		case PALETTE_BUTTON_TERRAIN:
			type = TILESET_TERRAIN;
			break;
		case PALETTE_BUTTON_DOODAD:
			type = TILESET_DOODAD;
			break;
		case PALETTE_BUTTON_ITEM:
			type = TILESET_ITEM;
			break;
		case PALETTE_BUTTON_HOUSE:
			type = TILESET_HOUSE;
			break;
		case PALETTE_BUTTON_CREATURE:
			type = TILESET_CREATURE;
			break;
		case PALETTE_BUTTON_RAW:
			type = TILESET_RAW;
			break;
		default:
			return;
	}
	SelectPage(type);
	g_gui.SelectBrush();
}

void PaletteWindow::OnUpdateBrushSize(BrushShape shape, int size) {
	PalettePanel* page = nullptr;
	switch (current_page) {
		case TILESET_TERRAIN:
			page = terrain_palette;
			break;
		case TILESET_DOODAD:
			page = doodad_palette;
			break;
		case TILESET_ITEM:
			page = item_palette;
			break;
		case TILESET_HOUSE:
			page = house_palette;
			break;
		case TILESET_CREATURE:
			page = creature_palette;
			break;
		case TILESET_RAW:
			page = raw_palette;
			break;
		default:
			return;
	}
	if (page) {
		page->OnUpdateBrushSize(shape, size);
	}
}

void PaletteWindow::OnUpdate(Map* map) {
	if (creature_palette) {
		creature_palette->OnUpdate();
	}
	if (house_palette) {
		house_palette->SetMap(map);
	}
	if (comments_button) {
		Editor* editor = g_gui.GetCurrentEditor();
		bool showComments = editor && editor->IsLive();
		if (comments_button->IsShown() != showComments) {
			comments_button->Show(showComments);
			Layout();
		}
	}
}

void PaletteWindow::OnKey(wxKeyEvent& event) {
	if (g_gui.GetCurrentTab() != nullptr) {
		g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
	}
}

void PaletteWindow::OnClose(wxCloseEvent& event) {
	if (!event.CanVeto()) {
		// We can't do anything! This sucks!
		// (application is closed, we have to destroy ourselves)
		Destroy();
	} else {
		Show(false);
		event.Veto(true);
	}
}
