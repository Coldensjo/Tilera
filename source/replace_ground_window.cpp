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

#include "replace_ground_window.h"

#include "editor.h"
#include "ground_brush.h"
#include "gui.h"
#include "materials.h"
#include "palette_brushlist.h"
#include "settings.h"
#include "tileset.h"

namespace {

bool isSelectableGroundBrush(const Brush* brush) {
	return brush && brush->isGround() && brush->visibleInPalette();
}

void collectBorderTiles(const PositionVector& tiles, PositionList& borders) {
	std::set<Position> tileSet(tiles.begin(), tiles.end());
	std::set<Position> borderSet;
	static const int dx[] = { 0, -1, 1, 0 };
	static const int dy[] = { -1, 0, 0, 1 };

	for (const Position& pos : tiles) {
		for (int i = 0; i < 4; ++i) {
			const Position neighbor(pos.x + dx[i], pos.y + dy[i], pos.z);
			if (!neighbor.isValid() || tileSet.count(neighbor) != 0) {
				continue;
			}
			borderSet.insert(neighbor);
		}
	}

	borders.assign(borderSet.begin(), borderSet.end());
}

} // namespace

bool ReplaceConnectedGrounds(Editor& editor, const Position& startPos, GroundBrush* sourceBrush, GroundBrush* newBrush) {
	if (!sourceBrush || !newBrush || sourceBrush == newBrush) {
		return false;
	}

	PositionVector tiles;
	GroundBrush::collectConnectedTiles(&editor.map, startPos, sourceBrush, tiles);
	if (tiles.empty()) {
		return false;
	}

	PositionList borders;
	collectBorderTiles(tiles, borders);

	BatchAction* batch = editor.actionQueue->createBatch(ACTION_DRAW);
	Action* action = editor.actionQueue->createAction(batch);

	for (const Position& pos : tiles) {
		TileLocation* location = editor.map.createTileL(pos);
		Tile* tile = location->get();
		if (!tile) {
			continue;
		}

		Tile* new_tile = tile->deepCopy(editor.map);
		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			new_tile->cleanBorders();
		}
		sourceBrush->undraw(&editor.map, new_tile);
		newBrush->draw(&editor.map, new_tile, nullptr);
		action->addChange(newd Change(new_tile));
	}
	batch->addAndCommitAction(action);

	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		action = editor.actionQueue->createAction(batch);
		std::set<Position> borderizeSet(borders.begin(), borders.end());
		borderizeSet.insert(tiles.begin(), tiles.end());

		for (const Position& pos : borderizeSet) {
			TileLocation* location = editor.map.createTileL(pos);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(editor.map);
				new_tile->borderize(&editor.map);
				action->addChange(newd Change(new_tile));
			} else {
				Tile* new_tile = editor.map.allocator(location);
				new_tile->borderize(&editor.map);
				if (new_tile->size() > 0) {
					action->addChange(newd Change(new_tile));
				} else {
					delete new_tile;
				}
			}
		}
		batch->addAndCommitAction(action);
	}

	editor.addBatch(batch, 2);
	return true;
}

BEGIN_EVENT_TABLE(ReplaceGroundDialog, wxDialog)
EVT_CHOICE(wxID_ANY, ReplaceGroundDialog::OnTilesetSelected)
EVT_BUTTON(wxID_OK, ReplaceGroundDialog::OnClickOK)
EVT_BUTTON(wxID_CANCEL, ReplaceGroundDialog::OnClickCancel)
END_EVENT_TABLE()

ReplaceGroundDialog::ReplaceGroundDialog(wxWindow* parent, Editor& editor, const Position& startPos, GroundBrush* sourceBrush) :
	wxDialog(parent, wxID_ANY, "Replace Ground", wxDefaultPosition, wxWindow::FromDIP(wxSize(520, 520), parent), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor),
	start_pos(startPos),
	source_brush(sourceBrush),
	tileset_field(nullptr),
	brush_panel(nullptr) {
	ASSERT(source_brush);

	wxBoxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* infoSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Replace connected ground");
	infoSizer->Add(newd wxStaticText(this, wxID_ANY, "All ground connected to the clicked tile will be replaced with the brush you pick below."), 0, wxALL, 8);

	wxFlexGridSizer* grid = newd wxFlexGridSizer(2, 8, 8);
	grid->AddGrowableCol(1);
	grid->Add(newd wxStaticText(this, wxID_ANY, "Tileset"), 0, wxALIGN_CENTER_VERTICAL);
	tileset_field = newd wxChoice(this, wxID_ANY);
	grid->Add(tileset_field, 1, wxEXPAND);
	infoSizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	topsizer->Add(infoSizer, 0, wxEXPAND | wxALL, 10);

	brush_panel = newd BrushPanel(this);
	brush_panel->SetPickerMode(true);
	brush_panel->SetListType(BRUSHLIST_LARGE_ICONS);
	topsizer->Add(brush_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

	wxStdDialogButtonSizer* buttons = newd wxStdDialogButtonSizer();
	buttons->AddButton(newd wxButton(this, wxID_OK, "Replace"));
	buttons->AddButton(newd wxButton(this, wxID_CANCEL, "Cancel"));
	buttons->Realize();
	topsizer->Add(buttons, 0, wxEXPAND | wxALL, 10);

	SetSizer(topsizer);
	SetMinSize(FromDIP(wxSize(420, 420)));

	int defaultSelection = 0;
	int index = 0;
	const std::string preferredTileset = FindTilesetForBrush(source_brush);
	for (TilesetContainer::const_iterator iter = g_materials.tilesets.begin(); iter != g_materials.tilesets.end(); ++iter) {
		if (!TilesetHasGroundBrushes(iter->first)) {
			continue;
		}
		tileset_field->Append(wxstr(iter->first), newd std::string(iter->first));
		if (iter->first == preferredTileset) {
			defaultSelection = index;
		}
		++index;
	}

	if (tileset_field->GetCount() == 0) {
		g_gui.PopupDialog("Replace Ground", "No ground tilesets are available.", wxOK);
		EndModal(wxID_CANCEL);
		return;
	}

	tileset_field->SetSelection(defaultSelection);
	LoadSelectedTileset();
	Centre(wxBOTH);
}

ReplaceGroundDialog::~ReplaceGroundDialog() {
	////
}

bool ReplaceGroundDialog::TilesetHasGroundBrushes(const std::string& tilesetName) const {
	TilesetContainer::const_iterator iter = g_materials.tilesets.find(tilesetName);
	if (iter == g_materials.tilesets.end()) {
		return false;
	}

	const TilesetCategory* category = iter->second->getCategory(TILESET_TERRAIN);
	if (!category) {
		return false;
	}

	for (size_t i = 0; i < category->size(); ++i) {
		if (isSelectableGroundBrush(category->brushlist[i])) {
			return true;
		}
	}
	return false;
}

std::string ReplaceGroundDialog::FindTilesetForBrush(GroundBrush* brush) const {
	for (TilesetContainer::const_iterator iter = g_materials.tilesets.begin(); iter != g_materials.tilesets.end(); ++iter) {
		if (g_materials.isInTileset(brush, iter->first)) {
			return iter->first;
		}
	}
	return "Grounds";
}

void ReplaceGroundDialog::LoadSelectedTileset() {
	const int selection = tileset_field->GetSelection();
	if (selection == wxNOT_FOUND) {
		return;
	}

	const std::string* tilesetName = static_cast<std::string*>(tileset_field->GetClientData(selection));
	if (!tilesetName) {
		return;
	}

	TilesetContainer::const_iterator iter = g_materials.tilesets.find(*tilesetName);
	if (iter == g_materials.tilesets.end()) {
		return;
	}

	const TilesetCategory* category = iter->second->getCategory(TILESET_TERRAIN);
	if (!category) {
		return;
	}

	brush_panel->AssignTileset(category);
	brush_panel->LoadContents();
	if (brush_panel->SelectBrush(source_brush)) {
		return;
	}
	brush_panel->SelectFirstBrush();
}

void ReplaceGroundDialog::OnTilesetSelected(wxCommandEvent& WXUNUSED(event)) {
	LoadSelectedTileset();
}

void ReplaceGroundDialog::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	Brush* brush = brush_panel->GetSelectedBrush();
	if (!isSelectableGroundBrush(brush)) {
		g_gui.PopupDialog("Replace Ground", "Select a ground brush to continue.", wxOK);
		return;
	}

	GroundBrush* newBrush = brush->asGround();
	if (newBrush == source_brush) {
		g_gui.PopupDialog("Replace Ground", "Pick a different ground brush than the one being replaced.", wxOK);
		return;
	}

	if (ReplaceConnectedGrounds(editor, start_pos, source_brush, newBrush)) {
		g_gui.RefreshView();
		EndModal(wxID_OK);
	} else {
		g_gui.PopupDialog("Replace Ground", "No connected ground was found to replace.", wxOK);
	}
}

void ReplaceGroundDialog::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}
