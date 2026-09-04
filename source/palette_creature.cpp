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
#include "brush.h"
#include "gui.h"
#include "palette_creature.h"
#include "creature_brush.h"
#include "creature.h"
#include "creatures.h"
#include "items.h"
#include "spawn_brush.h"
#include "materials.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

#include <wx/dcclient.h>

// ============================================================================
// Shared sprite helpers
//
// Creatures come in two sizes: 32x32 (1x1 tile) and 64x64 (2x2 tile). To make
// them line up in the palette every creature is laid out inside a fixed 64x64
// slot. 32x32 sprites are drawn at their native size, pushed into the
// bottom-right corner of the slot so their feet line up with the 64x64 ones.

namespace {

using SpriteList = std::vector<Sprite*>;

// The size of the square slot every creature icon is laid out within.
constexpr int CREATURE_SLOT_SIZE = 2 * SPRITE_PIXELS; // 64

// Returns the native (unscaled) pixel dimensions of a creature sprite, with a
// floor of one tile (32x32). Larger outfits report their extra draw height.
std::pair<int, int> GetCreatureSpriteSize(Sprite* sprite) {
	int width = SPRITE_PIXELS;
	int height = SPRITE_PIXELS;
	if (GameSprite* game_sprite = dynamic_cast<GameSprite*>(sprite)) {
		width = std::max<int>(1, int(game_sprite->width)) * SPRITE_PIXELS;
		height = std::max<int>(1, int(game_sprite->height)) * SPRITE_PIXELS;
		int draw_height = game_sprite->getDrawHeight();
		if (draw_height > 0) {
			height = std::max(height, draw_height);
		}
	}
	return { std::max<int>(SPRITE_PIXELS, width), std::max<int>(SPRITE_PIXELS, height) };
}

void CollectBrushSprites(Brush* brush, SpriteList& sprites) {
	sprites.clear();
	if (!brush) {
		return;
	}

	if (brush->isCreature()) {
		CreatureBrush* creature_brush = dynamic_cast<CreatureBrush*>(brush);
		if (!creature_brush) {
			return;
		}

		CreatureType* type = creature_brush->getType();
		if (!type) {
			return;
		}

		const Outfit& outfit = type->outfit;
		if (outfit.lookItem != 0 && g_items.typeExists(outfit.lookItem)) {
			ItemType& item = g_items.getItemType(outfit.lookItem);
			if (item.sprite) {
				sprites.push_back(item.sprite);
			}
			return;
		}

		if (outfit.lookMount != 0) {
			if (Sprite* mount_sprite = g_gui.gfx.getCreatureSprite(outfit.lookMount)) {
				sprites.push_back(mount_sprite);
			}
		}

		if (outfit.lookType != 0) {
			if (Sprite* creature_sprite = g_gui.gfx.getCreatureSprite(outfit.lookType)) {
				sprites.push_back(creature_sprite);
			}
		}
		return;
	}

	int look_id = brush->getLookID();
	if (look_id != 0) {
		if (Sprite* sprite = g_gui.gfx.getSprite(look_id)) {
			sprites.push_back(sprite);
		}
	}
}

// Draws every sprite of a brush inside the given square slot, native size,
// anchored to the bottom-right corner. Sprites larger than the slot are scaled
// down to fit while keeping their aspect ratio.
void DrawSpritesInSlot(wxDC& dc, const SpriteList& sprites, int slot_x, int slot_y, int slot_size) {
	for (Sprite* sprite : sprites) {
		if (!sprite) {
			continue;
		}
		auto [sprite_width, sprite_height] = GetCreatureSpriteSize(sprite);
		int draw_width = sprite_width;
		int draw_height = sprite_height;
		if (draw_width > slot_size || draw_height > slot_size) {
			double scale = std::min(double(slot_size) / draw_width, double(slot_size) / draw_height);
			draw_width = std::max<int>(1, int(std::lround(draw_width * scale)));
			draw_height = std::max<int>(1, int(std::lround(draw_height * scale)));
		}
		int draw_x = slot_x + (slot_size - draw_width);
		int draw_y = slot_y + (slot_size - draw_height);
		sprite->DrawTo(&dc, SPRITE_SIZE_ACTUAL, draw_x, draw_y, draw_width, draw_height);
	}
}

void DrawCreatureBrushInSlot(wxDC& dc, Brush* brush, int slot_x, int slot_y, int slot_size) {
	if (!brush || !brush->isCreature()) {
		SpriteList sprites;
		CollectBrushSprites(brush, sprites);
		DrawSpritesInSlot(dc, sprites, slot_x, slot_y, slot_size);
		return;
	}

	CreatureBrush* creature_brush = dynamic_cast<CreatureBrush*>(brush);
	if (!creature_brush) {
		return;
	}

	CreatureType* type = creature_brush->getType();
	if (!type) {
		return;
	}

	const Outfit& outfit = type->outfit;
	if (outfit.lookItem != 0) {
		SpriteList sprites;
		CollectBrushSprites(brush, sprites);
		DrawSpritesInSlot(dc, sprites, slot_x, slot_y, slot_size);
		return;
	}

	GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
	if (!spr || outfit.lookType == 0) {
		return;
	}

	wxImage image;
	int pattern_z = 0;
	if (outfit.lookMount != 0) {
		if (GameSprite* mount_spr = g_gui.gfx.getCreatureSprite(outfit.lookMount)) {
			Outfit mount_outfit;
			mount_outfit.lookType = outfit.lookMount;
			mount_outfit.lookHead = outfit.lookMountHead;
			mount_outfit.lookBody = outfit.lookMountBody;
			mount_outfit.lookLegs = outfit.lookMountLegs;
			mount_outfit.lookFeet = outfit.lookMountFeet;
			image = mount_spr->getCreatureImage(SOUTH, 0, 0, mount_outfit);
			pattern_z = std::min<int>(1, spr->pattern_z - 1);
		}
	}

	for (int addon = 0; addon < spr->pattern_y; ++addon) {
		if (addon > 0 && !(outfit.lookAddon & (1 << (addon - 1)))) {
			continue;
		}
		wxImage part = spr->getCreatureImage(SOUTH, addon, pattern_z, outfit);
		if (!image.IsOk()) {
			image = part;
		} else if (part.IsOk()) {
			image.Paste(part, 0, 0);
		}
	}

	if (!image.IsOk()) {
		return;
	}

	int draw_width = std::max<int>(SPRITE_PIXELS, image.GetWidth());
	int draw_height = std::max<int>(SPRITE_PIXELS, image.GetHeight());
	int draw_height_extra = spr->getDrawHeight();
	if (draw_height_extra > 0) {
		draw_height = std::max(draw_height, draw_height_extra);
	}
	if (draw_width > slot_size || draw_height > slot_size) {
		double scale = std::min(double(slot_size) / draw_width, double(slot_size) / draw_height);
		draw_width = std::max<int>(1, int(std::lround(draw_width * scale)));
		draw_height = std::max<int>(1, int(std::lround(draw_height * scale)));
		image.Rescale(draw_width, draw_height, wxIMAGE_QUALITY_NEAREST);
	}
	int draw_x = slot_x + (slot_size - draw_width);
	int draw_y = slot_y + (slot_size - draw_height);
	wxBitmap bmp(image, -1);
	dc.DrawBitmap(bmp, draw_x, draw_y, true);
}

} // namespace

// ============================================================================
// CreatureListBox - single column list, icon on the left, name on the right

class CreatureListBox : public wxVListBox {
public:
	CreatureListBox(wxWindow* parent, wxWindowID id);

	void ClearEntries();
	void Append(const wxString& label, Brush* brush);
	void Sort();
	void SelectAll(); // Override to prevent assertion for single-selection listbox

	Brush* GetBrush(size_t index) const;
	Brush* GetSelectedBrush() const;
	bool SetSelectionByName(const std::string& name);
	size_t GetCount() const;
	void OnChar(wxKeyEvent& event); // Intercept Ctrl+A to prevent SelectAll assertion

private:
	struct Entry {
		wxString name;
		Brush* brush;
	};

	void RefreshAfterChange();

protected:
	void OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const override;
	wxCoord OnMeasureItem(size_t index) const override;

private:
	std::vector<Entry> entries;
};

CreatureListBox::CreatureListBox(wxWindow* parent, wxWindowID id) :
	wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE | wxWANTS_CHARS) {
	SetItemCount(0);
	Bind(wxEVT_CHAR, &CreatureListBox::OnChar, this);
}

void CreatureListBox::SelectAll() {
	// No-op for single-selection listbox to prevent assertion
}

void CreatureListBox::OnChar(wxKeyEvent& event) {
	// Intercept Ctrl+A to prevent SelectAll assertion for single-selection listbox
	if (isSelectAllShortcut(event)) {
		// Do nothing - prevent base class from calling SelectAll
		return;
	}
	event.Skip();
}

void CreatureListBox::ClearEntries() {
	SetSelection(wxNOT_FOUND);
	entries.clear();
	SetItemCount(0);
	RefreshAll();
}

void CreatureListBox::Append(const wxString& label, Brush* brush) {
	entries.push_back({ label, brush });
	SetItemCount(entries.size());
}

Brush* CreatureListBox::GetBrush(size_t index) const {
	if (index >= entries.size()) {
		return nullptr;
	}
	return entries[index].brush;
}

Brush* CreatureListBox::GetSelectedBrush() const {
	int selection = GetSelection();
	if (selection == wxNOT_FOUND) {
		if (entries.empty()) {
			return nullptr;
		}
		selection = 0;
	}
	return GetBrush(static_cast<size_t>(selection));
}

bool CreatureListBox::SetSelectionByName(const std::string& name) {
	if (entries.empty()) {
		return false;
	}

	wxString target(wxstr(name));
	for (size_t index = 0; index < entries.size(); ++index) {
		if (entries[index].name.IsSameAs(target, false)) {
			SetSelection(index);
			return true;
		}
	}

	SetSelection(0);
	return false;
}

void CreatureListBox::Sort() {
	if (entries.empty()) {
		RefreshAfterChange();
		return;
	}

	Brush* selected = GetSelectedBrush();
	std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		return a.name.CmpNoCase(b.name) < 0;
	});

	if (selected) {
		for (size_t index = 0; index < entries.size(); ++index) {
			if (entries[index].brush == selected) {
				SetSelection(index);
				break;
			}
		}
	}

	RefreshAfterChange();
}

void CreatureListBox::RefreshAfterChange() {
	SetItemCount(entries.size());
	RefreshAll();
}

size_t CreatureListBox::GetCount() const {
	return entries.size();
}

void CreatureListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const {
	ASSERT(index < entries.size());
	const Entry& entry = entries[index];

	// Every creature is laid out within a fixed 64x64 slot so that 32x32 and
	// 64x64 creatures line up consistently.
	int slot_x = rect.GetX();
	int slot_y = rect.GetY() + (rect.GetHeight() - CREATURE_SLOT_SIZE) / 2;
	DrawCreatureBrushInSlot(dc, entry.brush, slot_x, slot_y, CREATURE_SLOT_SIZE);

	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);

	int text_width;
	int text_height;
	dc.GetTextExtent(entry.name, &text_width, &text_height);
	int text_x = slot_x + CREATURE_SLOT_SIZE + 8;
	int text_y = rect.GetY() + (rect.GetHeight() - text_height) / 2;
	dc.DrawText(entry.name, text_x, text_y);
}

wxCoord CreatureListBox::OnMeasureItem(size_t index) const {
	ASSERT(index < entries.size());
	return CREATURE_SLOT_SIZE + 4;
}

// ============================================================================
// CreatureSpriteGrid - multi column grid, icon with the name below each cell

class CreatureSpriteGrid : public wxScrolledWindow {
public:
	CreatureSpriteGrid(wxWindow* parent, wxWindowID id);

	void ClearEntries();
	void Append(const wxString& label, Brush* brush);
	void Sort();

	Brush* GetBrush(size_t index) const;
	Brush* GetSelectedBrush() const;
	int GetSelection() const;
	void SetSelection(int index);
	bool SetSelectionByName(const std::string& name);
	size_t GetCount() const;

	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnMouseClick(wxMouseEvent& event);

private:
	struct Entry {
		wxString name;
		Brush* brush;
	};

	void RecalculateGrid();
	void EnsureVisible(int index);
	int CellIndexAt(const wxPoint& unscrolled) const;
	void NotifySelection();

	static constexpr int CELL_PADDING = 4;
	static constexpr int TEXT_HEIGHT = 14; // single line of name text

	std::vector<Entry> entries;
	int columns;
	int cell_width;
	int cell_height;
	int selected_index;

	DECLARE_EVENT_TABLE();
};

BEGIN_EVENT_TABLE(CreatureSpriteGrid, wxScrolledWindow)
EVT_PAINT(CreatureSpriteGrid::OnPaint)
EVT_SIZE(CreatureSpriteGrid::OnSize)
EVT_LEFT_DOWN(CreatureSpriteGrid::OnMouseClick)
END_EVENT_TABLE()

CreatureSpriteGrid::CreatureSpriteGrid(wxWindow* parent, wxWindowID id) :
	wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxWANTS_CHARS),
	columns(1),
	cell_width(CREATURE_SLOT_SIZE + 2 * CELL_PADDING),
	cell_height(CREATURE_SLOT_SIZE + TEXT_HEIGHT + 2 * CELL_PADDING),
	selected_index(wxNOT_FOUND) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(ThemeManager::Get().GetPalette().surface);
	SetScrollRate(0, cell_height);
}

void CreatureSpriteGrid::ClearEntries() {
	entries.clear();
	selected_index = wxNOT_FOUND;
	RecalculateGrid();
	Refresh();
}

void CreatureSpriteGrid::Append(const wxString& label, Brush* brush) {
	entries.push_back({ label, brush });
}

void CreatureSpriteGrid::Sort() {
	Brush* selected = GetSelectedBrush();
	std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
		return a.name.CmpNoCase(b.name) < 0;
	});

	selected_index = wxNOT_FOUND;
	if (selected) {
		for (size_t index = 0; index < entries.size(); ++index) {
			if (entries[index].brush == selected) {
				selected_index = int(index);
				break;
			}
		}
	}

	RecalculateGrid();
	Refresh();
}

Brush* CreatureSpriteGrid::GetBrush(size_t index) const {
	if (index >= entries.size()) {
		return nullptr;
	}
	return entries[index].brush;
}

Brush* CreatureSpriteGrid::GetSelectedBrush() const {
	if (selected_index == wxNOT_FOUND) {
		if (entries.empty()) {
			return nullptr;
		}
		return entries[0].brush;
	}
	return GetBrush(static_cast<size_t>(selected_index));
}

int CreatureSpriteGrid::GetSelection() const {
	return selected_index;
}

void CreatureSpriteGrid::SetSelection(int index) {
	if (index < 0 || index >= int(entries.size())) {
		selected_index = wxNOT_FOUND;
	} else {
		selected_index = index;
		EnsureVisible(index);
	}
	Refresh();
}

bool CreatureSpriteGrid::SetSelectionByName(const std::string& name) {
	if (entries.empty()) {
		return false;
	}

	wxString target(wxstr(name));
	for (size_t index = 0; index < entries.size(); ++index) {
		if (entries[index].name.IsSameAs(target, false)) {
			SetSelection(int(index));
			return true;
		}
	}

	SetSelection(0);
	return false;
}

size_t CreatureSpriteGrid::GetCount() const {
	return entries.size();
}

void CreatureSpriteGrid::RecalculateGrid() {
	int client_width = GetClientSize().GetWidth();
	columns = std::max(1, client_width / cell_width);

	int rows = entries.empty() ? 0 : int((entries.size() + columns - 1) / columns);
	SetVirtualSize(columns * cell_width, rows * cell_height);
}

void CreatureSpriteGrid::EnsureVisible(int index) {
	if (index < 0 || columns <= 0) {
		return;
	}
	int row = index / columns;
	int cell_top = row * cell_height;
	int cell_bottom = cell_top + cell_height;

	int view_start = GetViewStart().y * cell_height;
	int view_height = GetClientSize().GetHeight();

	if (cell_top < view_start) {
		Scroll(0, row);
	} else if (cell_bottom > view_start + view_height) {
		int rows_visible = std::max(1, view_height / cell_height);
		Scroll(0, std::max(0, row - rows_visible + 1));
	}
}

int CreatureSpriteGrid::CellIndexAt(const wxPoint& unscrolled) const {
	if (columns <= 0) {
		return wxNOT_FOUND;
	}
	int col = unscrolled.x / cell_width;
	int row = unscrolled.y / cell_height;
	if (col < 0 || col >= columns) {
		return wxNOT_FOUND;
	}
	int index = row * columns + col;
	if (index < 0 || index >= int(entries.size())) {
		return wxNOT_FOUND;
	}
	return index;
}

void CreatureSpriteGrid::OnSize(wxSizeEvent& event) {
	RecalculateGrid();
	Refresh();
	event.Skip();
}

void CreatureSpriteGrid::OnMouseClick(wxMouseEvent& event) {
	SetFocus();
	wxPoint unscrolled = CalcUnscrolledPosition(event.GetPosition());
	int index = CellIndexAt(unscrolled);
	if (index != wxNOT_FOUND && index != selected_index) {
		selected_index = index;
		Refresh();
		NotifySelection();
	} else if (index != wxNOT_FOUND) {
		NotifySelection();
	}
}

void CreatureSpriteGrid::NotifySelection() {
	wxCommandEvent evt(wxEVT_COMMAND_LISTBOX_SELECTED, GetId());
	evt.SetEventObject(this);
	evt.SetInt(selected_index);
	GetEventHandler()->ProcessEvent(evt);
}

void CreatureSpriteGrid::OnPaint(wxPaintEvent& event) {
	wxPaintDC dc(this);
	DoPrepareDC(dc);
	const ThemePalette& palette = ThemeManager::Get().GetPalette();

	dc.SetBackground(wxBrush(palette.surface));
	dc.Clear();

	if (entries.empty() || columns <= 0) {
		return;
	}

	// Only paint the rows that intersect the visible region.
	wxRect update = GetUpdateRegion().GetBox();
	wxPoint top_left = CalcUnscrolledPosition(update.GetTopLeft());
	wxPoint bottom_right = CalcUnscrolledPosition(update.GetBottomRight());

	int first_row = std::max(0, top_left.y / cell_height);
	int last_row = bottom_right.y / cell_height;

	for (int row = first_row; row <= last_row; ++row) {
		for (int col = 0; col < columns; ++col) {
			int index = row * columns + col;
			if (index >= int(entries.size())) {
				break;
			}

			const Entry& entry = entries[index];
			int cell_x = col * cell_width;
			int cell_y = row * cell_height;

			bool selected = (index == selected_index);
			if (selected) {
				dc.SetBrush(wxBrush(palette.selection));
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.DrawRectangle(cell_x, cell_y, cell_width, cell_height);
			}

			int slot_x = cell_x + (cell_width - CREATURE_SLOT_SIZE) / 2;
			int slot_y = cell_y + CELL_PADDING;
			DrawCreatureBrushInSlot(dc, entry.brush, slot_x, slot_y, CREATURE_SLOT_SIZE);

			dc.SetTextForeground(palette.text);
			wxString label = entry.name;
			int text_width;
			int text_height;
			dc.GetTextExtent(label, &text_width, &text_height);
			// Trim the name if it does not fit the cell width.
			while (text_width > cell_width - 2 && label.length() > 1) {
				label.RemoveLast();
				dc.GetTextExtent(label + "...", &text_width, &text_height);
				if (text_width <= cell_width - 2) {
					label += "...";
					break;
				}
			}
			dc.GetTextExtent(label, &text_width, &text_height);
			int text_x = cell_x + (cell_width - text_width) / 2;
			int text_y = cell_y + CELL_PADDING + CREATURE_SLOT_SIZE + (TEXT_HEIGHT - text_height) / 2;
			dc.DrawText(label, text_x, text_y);
		}
	}
}

// ============================================================================
// Creature palette

BEGIN_EVENT_TABLE(CreaturePalettePanel, PalettePanel)
EVT_CHOICE(PALETTE_CREATURE_TILESET_CHOICE, CreaturePalettePanel::OnTilesetChange)
EVT_CHOICE(PALETTE_CREATURE_VIEW_STYLE, CreaturePalettePanel::OnViewStyleChange)

EVT_LISTBOX(PALETTE_CREATURE_LISTBOX, CreaturePalettePanel::OnListBoxChange)
EVT_LISTBOX(PALETTE_CREATURE_GRID, CreaturePalettePanel::OnListBoxChange)

EVT_SPINCTRL(PALETTE_CREATURE_SPAWN_TIME, CreaturePalettePanel::OnChangeSpawnTime)
EVT_SPINCTRL(PALETTE_CREATURE_SPAWN_SIZE, CreaturePalettePanel::OnChangeSpawnSize)
END_EVENT_TABLE()

CreaturePalettePanel::CreaturePalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	use_grid_view(true),
	handling_event(false) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Creatures");
	tileset_choice = newd wxChoice(this, PALETTE_CREATURE_TILESET_CHOICE, wxDefaultPosition, wxDefaultSize, (int)0, (const wxString*)nullptr);
	sidesizer->Add(tileset_choice, 0, wxEXPAND);

	wxArrayString view_styles;
	view_styles.Add("List view");
	view_styles.Add("Grid view");
	view_style_choice = newd wxChoice(this, PALETTE_CREATURE_VIEW_STYLE, wxDefaultPosition, wxDefaultSize, view_styles);
	view_style_choice->SetSelection(1);
	sidesizer->Add(view_style_choice, 0, wxEXPAND | wxTOP, 2);

	creature_list = newd CreatureListBox(this, PALETTE_CREATURE_LISTBOX);
	sidesizer->Add(creature_list, 1, wxEXPAND);
	creature_list->Hide();

	creature_grid = newd CreatureSpriteGrid(this, PALETTE_CREATURE_GRID);
	sidesizer->Add(creature_grid, 1, wxEXPAND);

	topsizer->Add(sidesizer, 1, wxEXPAND);

	// Brush selection
	sidesizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Brushes", wxDefaultPosition, FromDIP(wxSize(150, 200))), wxVERTICAL);

	wxFlexGridSizer* grid = newd wxFlexGridSizer(2, 10, 10);
	grid->AddGrowableCol(1);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Spawntime"));
	creature_spawntime_spin = newd wxSpinCtrl(this, PALETTE_CREATURE_SPAWN_TIME, i2ws(g_settings.getInteger(Config::DEFAULT_SPAWNTIME)), wxDefaultPosition, FromDIP(wxSize(50, 20)), wxSP_ARROW_KEYS, 0, 86400, g_settings.getInteger(Config::DEFAULT_SPAWNTIME));
	grid->Add(creature_spawntime_spin, 0, wxEXPAND);
	g_gui.SetSpawnTime(creature_spawntime_spin->GetValue());

	grid->Add(newd wxStaticText(this, wxID_ANY, "Spawn size"));
	spawn_size_spin = newd wxSpinCtrl(this, PALETTE_CREATURE_SPAWN_SIZE, i2ws(5), wxDefaultPosition, FromDIP(wxSize(50, 20)), wxSP_ARROW_KEYS, 1, g_settings.getInteger(Config::MAX_SPAWN_RADIUS), g_settings.getInteger(Config::CURRENT_SPAWN_RADIUS));
	grid->Add(spawn_size_spin, 0, wxEXPAND);
	grid->AddSpacer(0);

	sidesizer->Add(grid, 0, wxEXPAND);
	topsizer->Add(sidesizer, 0, wxEXPAND);
	SetSizerAndFit(topsizer);

	OnUpdate();
}

CreaturePalettePanel::~CreaturePalettePanel() {
	////
}

PaletteType CreaturePalettePanel::GetType() const {
	return TILESET_CREATURE;
}

void CreaturePalettePanel::SelectFirstBrush() {
	if (creature_list->GetCount() > 0) {
		SelectCreature(size_t(0));
	}
}

Brush* CreaturePalettePanel::GetSelectedBrush() const {
	if (creature_list->GetCount() == 0) {
		return nullptr;
	}

	Brush* brush = use_grid_view ? creature_grid->GetSelectedBrush() : creature_list->GetSelectedBrush();
	if (brush && brush->isCreature()) {
		g_gui.SetSpawnTime(creature_spawntime_spin->GetValue());
		g_settings.setInteger(Config::CURRENT_SPAWN_RADIUS, spawn_size_spin->GetValue());
		g_settings.setInteger(Config::DEFAULT_SPAWNTIME, creature_spawntime_spin->GetValue());
		return brush;
	}
	return nullptr;
}

bool CreaturePalettePanel::SelectBrush(const Brush* whatbrush) {
	if (!whatbrush) {
		return false;
	}

	if (whatbrush->isCreature()) {
		int current_index = tileset_choice->GetSelection();
		if (current_index != wxNOT_FOUND) {
			const TilesetCategory* tsc = reinterpret_cast<const TilesetCategory*>(tileset_choice->GetClientData(current_index));
			if (tsc) {
				for (BrushVector::const_iterator iter = tsc->brushlist.begin(); iter != tsc->brushlist.end(); ++iter) {
					if (*iter == whatbrush) {
						SelectCreature(whatbrush->getName());
						return true;
					}
				}
			}
		}
		// Not in the current display, search the hidden one's
		for (size_t i = 0; i < tileset_choice->GetCount(); ++i) {
			if (current_index != (int)i) {
				const TilesetCategory* tsc = reinterpret_cast<const TilesetCategory*>(tileset_choice->GetClientData(i));
				if (tsc) {
					for (BrushVector::const_iterator iter = tsc->brushlist.begin();
						 iter != tsc->brushlist.end();
						 ++iter) {
						if (*iter == whatbrush) {
							SelectTileset(i);
							SelectCreature(whatbrush->getName());
							g_gui.SetSpawnTime(creature_spawntime_spin->GetValue());
							return true;
						}
					}
				}
			}
		}
	} else if (whatbrush->isSpawn()) {
		// Spawn brush is a special brush that doesn't need to be selected from a list
		// Just return true to indicate successful selection
		return true;
	}
	return false;
}

int CreaturePalettePanel::GetSelectedBrushSize() const {
	return spawn_size_spin->GetValue();
}

void CreaturePalettePanel::OnUpdate() {
	tileset_choice->Clear();
	g_materials.createOtherTileset();

	for (TilesetContainer::const_iterator iter = g_materials.tilesets.begin(); iter != g_materials.tilesets.end(); ++iter) {
		const TilesetCategory* tsc = iter->second->getCategory(TILESET_CREATURE);
		if (tsc && tsc->size() > 0) {
			tileset_choice->Append(wxstr(iter->second->name), const_cast<TilesetCategory*>(tsc));
		} else if (iter->second->name == "NPCs" || iter->second->name == "Others") {
			Tileset* ts = const_cast<Tileset*>(iter->second);
			TilesetCategory* rtsc = ts->getCategory(TILESET_CREATURE);
			tileset_choice->Append(wxstr(ts->name), rtsc);
		}
	}
	SelectTileset(0);
}

void CreaturePalettePanel::OnUpdateBrushSize(BrushShape shape, int size) {
	const int value = size >= 0 ? size : g_gui.GetBrushSize();
	return spawn_size_spin->SetValue(value);
}

void CreaturePalettePanel::OnSwitchIn() {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetSpawnTime(creature_spawntime_spin->GetValue());
	g_gui.SetBrushSize(spawn_size_spin->GetValue());
}

void CreaturePalettePanel::SetViewStyle(bool use_grid) {
	if (use_grid == use_grid_view) {
		return;
	}

	// Carry the current selection across to the view we are switching to.
	Brush* selected = use_grid_view ? creature_grid->GetSelectedBrush() : creature_list->GetSelectedBrush();

	use_grid_view = use_grid;
	creature_list->Show(!use_grid_view);
	creature_grid->Show(use_grid_view);

	if (selected) {
		if (use_grid_view) {
			creature_grid->SetSelectionByName(selected->getName());
		} else {
			creature_list->SetSelectionByName(selected->getName());
		}
	}

	Layout();
}

void CreaturePalettePanel::SelectTileset(size_t index) {
	ASSERT(tileset_choice->GetCount() >= index);

	creature_list->ClearEntries();
	creature_grid->ClearEntries();
	if (tileset_choice->GetCount() == 0) {
		// No tilesets
		return;
	}

	const TilesetCategory* tsc = reinterpret_cast<const TilesetCategory*>(tileset_choice->GetClientData(index));
	if (tsc) {
		for (BrushVector::const_iterator iter = tsc->brushlist.begin();
			 iter != tsc->brushlist.end();
			 ++iter) {
			creature_list->Append(wxstr((*iter)->getName()), *iter);
			creature_grid->Append(wxstr((*iter)->getName()), *iter);
		}
		// Both views share the same ordering, so cell indices stay aligned.
		creature_list->Sort();
		creature_grid->Sort();
		SelectCreature(0);

		tileset_choice->SetSelection(index);
	}
}

void CreaturePalettePanel::SelectCreature(size_t index) {
	ASSERT(creature_list->GetCount() >= index);

	if (creature_list->GetCount() > 0) {
		creature_list->SetSelection(index);
		creature_grid->SetSelection(int(index));
	}
}

void CreaturePalettePanel::SelectCreature(std::string name) {
	if (creature_list->GetCount() > 0) {
		if (!creature_list->SetSelectionByName(name)) {
			creature_list->SetSelection(0);
		}
		if (!creature_grid->SetSelectionByName(name)) {
			creature_grid->SetSelection(0);
		}
	}
}

void CreaturePalettePanel::OnTilesetChange(wxCommandEvent& event) {
	SelectTileset(event.GetSelection());
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnViewStyleChange(wxCommandEvent& event) {
	SetViewStyle(event.GetSelection() == 1);
}

void CreaturePalettePanel::OnListBoxChange(wxCommandEvent& event) {
	int selection = event.GetSelection();
	if (selection < 0) {
		return;
	}
	// Keep both views in sync; their indices are aligned.
	SelectCreature(static_cast<size_t>(selection));
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnChangeSpawnTime(wxSpinEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetSpawnTime(event.GetPosition());
}

void CreaturePalettePanel::OnChangeSpawnSize(wxSpinEvent& event) {
	if (!handling_event) {
		handling_event = true;
		g_gui.ActivatePalette(GetParentPalette());
		g_gui.SetBrushSize(event.GetPosition());
		handling_event = false;
	}
}
