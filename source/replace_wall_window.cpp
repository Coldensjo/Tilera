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

#include "replace_wall_window.h"

#include "editor.h"
#include "gui.h"
#include "materials.h"
#include "palette_brushlist.h"
#include "settings.h"
#include "tileset.h"
#include "wall_brush.h"

namespace {

struct SavedDoor {
	::DoorType type;
	bool open;
};

bool isSelectableWallBrush(const Brush* brush) {
	if (!brush || !brush->isWall() || !brush->visibleInPalette()) {
		return false;
	}
	WallBrush* wall = const_cast<Brush*>(brush)->asWall();
	return wall && !wall->isWallDecoration();
}

bool belongsToWallNetwork(WallBrush* brush, WallBrush* sourceBrush) {
	if (!brush || !sourceBrush || brush->isWallDecoration()) {
		return false;
	}
	return brush == sourceBrush || sourceBrush->friendOf(brush) || brush->friendOf(sourceBrush);
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

void collectSavedDoors(Tile* tile, WallBrush* sourceBrush, std::vector<SavedDoor>& doors) {
	for (Item* item : tile->items) {
		if (!item->isWall() || !item->isBrushDoor()) {
			continue;
		}
		WallBrush* wb = item->getWallBrush();
		if (!belongsToWallNetwork(wb, sourceBrush)) {
			continue;
		}
		::DoorType doorType = sourceBrush->getDoorTypeFromID(item->getID());
		if (doorType == WALL_UNDEFINED) {
			continue;
		}
		SavedDoor saved;
		saved.type = doorType;
		saved.open = item->isOpen();
		doors.push_back(saved);
	}
}

void cleanNetworkWalls(Tile* tile, WallBrush* sourceBrush) {
	ItemVector::iterator it = tile->items.begin();
	while (it != tile->items.end()) {
		Item* item = *it;
		if (item->isWall() && belongsToWallNetwork(item->getWallBrush(), sourceBrush)) {
			delete item;
			it = tile->items.erase(it);
		} else {
			++it;
		}
	}
}

void restoreDoorsOnTile(Tile* tile, WallBrush* newBrush, const std::vector<SavedDoor>& doors) {
	if (doors.empty()) {
		return;
	}

	const bool prefLocked = g_gui.HasDoorLocked();
	size_t doorIndex = 0;

	for (Item* item : tile->items) {
		if (doorIndex >= doors.size()) {
			break;
		}
		if (!item->isWall() || item->isBorder() || item->isBrushDoor()) {
			continue;
		}
		WallBrush* wb = item->getWallBrush();
		if (!wb || wb->isWallDecoration() || wb != newBrush) {
			continue;
		}

		const SavedDoor& saved = doors[doorIndex++];
		const uint16_t doorId = newBrush->findMatchingDoorItem(item->getWallAlignment(), saved.type, saved.open, prefLocked);
		if (doorId != 0) {
			item->setID(doorId);
		}
	}
}

} // namespace

bool ReplaceConnectedWalls(Editor& editor, const Position& startPos, WallBrush* sourceBrush, WallBrush* newBrush) {
	if (!sourceBrush || !newBrush || sourceBrush == newBrush) {
		return false;
	}

	PositionVector tiles;
	WallBrush::collectConnectedTiles(&editor.map, startPos, sourceBrush, tiles);
	if (tiles.empty()) {
		return false;
	}

	std::map<Position, std::vector<SavedDoor>> savedDoors;
	for (const Position& pos : tiles) {
		Tile* tile = editor.map.getTile(pos);
		if (!tile) {
			continue;
		}
		collectSavedDoors(tile, sourceBrush, savedDoors[pos]);
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
		cleanNetworkWalls(new_tile, sourceBrush);
		newBrush->draw(&editor.map, new_tile, nullptr);
		action->addChange(newd Change(new_tile));
	}
	batch->addAndCommitAction(action);

	action = editor.actionQueue->createAction(batch);
	std::set<Position> wallizeSet(borders.begin(), borders.end());
	wallizeSet.insert(tiles.begin(), tiles.end());

	for (const Position& pos : wallizeSet) {
		Tile* tile = editor.map.getTile(pos);
		if (!tile) {
			continue;
		}
		Tile* new_tile = tile->deepCopy(editor.map);
		new_tile->wallize(&editor.map);
		action->addChange(newd Change(new_tile));
	}
	batch->addAndCommitAction(action);

	action = editor.actionQueue->createAction(batch);
	for (const Position& pos : tiles) {
		const std::map<Position, std::vector<SavedDoor>>::const_iterator saved = savedDoors.find(pos);
		if (saved == savedDoors.end() || saved->second.empty()) {
			continue;
		}

		Tile* tile = editor.map.getTile(pos);
		if (!tile) {
			continue;
		}

		Tile* new_tile = tile->deepCopy(editor.map);
		restoreDoorsOnTile(new_tile, newBrush, saved->second);
		action->addChange(newd Change(new_tile));
	}
	batch->addAndCommitAction(action);

	editor.addBatch(batch, 2);
	return true;
}

BEGIN_EVENT_TABLE(ReplaceWallDialog, wxDialog)
EVT_CHOICE(wxID_ANY, ReplaceWallDialog::OnTilesetSelected)
EVT_BUTTON(wxID_OK, ReplaceWallDialog::OnClickOK)
EVT_BUTTON(wxID_CANCEL, ReplaceWallDialog::OnClickCancel)
END_EVENT_TABLE()

ReplaceWallDialog::ReplaceWallDialog(wxWindow* parent, Editor& editor, const Position& startPos, WallBrush* sourceBrush) :
	wxDialog(parent, wxID_ANY, "Replace Wall", wxDefaultPosition, wxWindow::FromDIP(wxSize(520, 520), parent), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	editor(editor),
	start_pos(startPos),
	source_brush(sourceBrush),
	tileset_field(nullptr),
	brush_panel(nullptr) {
	ASSERT(source_brush);

	wxBoxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* infoSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Replace connected walls");
	infoSizer->Add(newd wxStaticText(this, wxID_ANY, "All walls, doors, and windows connected to the clicked tile will be replaced with the brush you pick below."), 0, wxALL, 8);

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
		if (!TilesetHasWallBrushes(iter->first)) {
			continue;
		}
		tileset_field->Append(wxstr(iter->first), newd std::string(iter->first));
		if (iter->first == preferredTileset) {
			defaultSelection = index;
		}
		++index;
	}

	if (tileset_field->GetCount() == 0) {
		g_gui.PopupDialog("Replace Wall", "No wall tilesets are available.", wxOK);
		EndModal(wxID_CANCEL);
		return;
	}

	tileset_field->SetSelection(defaultSelection);
	LoadSelectedTileset();
	Centre(wxBOTH);
}

ReplaceWallDialog::~ReplaceWallDialog() {
	////
}

bool ReplaceWallDialog::TilesetHasWallBrushes(const std::string& tilesetName) const {
	TilesetContainer::const_iterator iter = g_materials.tilesets.find(tilesetName);
	if (iter == g_materials.tilesets.end()) {
		return false;
	}

	const TilesetCategory* category = iter->second->getCategory(TILESET_TERRAIN);
	if (!category) {
		return false;
	}

	for (size_t i = 0; i < category->size(); ++i) {
		if (isSelectableWallBrush(category->brushlist[i])) {
			return true;
		}
	}
	return false;
}

std::string ReplaceWallDialog::FindTilesetForBrush(WallBrush* brush) const {
	for (TilesetContainer::const_iterator iter = g_materials.tilesets.begin(); iter != g_materials.tilesets.end(); ++iter) {
		if (g_materials.isInTileset(brush, iter->first)) {
			return iter->first;
		}
	}
	return "Walls";
}

void ReplaceWallDialog::LoadSelectedTileset() {
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

void ReplaceWallDialog::OnTilesetSelected(wxCommandEvent& WXUNUSED(event)) {
	LoadSelectedTileset();
}

void ReplaceWallDialog::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	Brush* brush = brush_panel->GetSelectedBrush();
	if (!isSelectableWallBrush(brush)) {
		g_gui.PopupDialog("Replace Wall", "Select a wall brush to continue.", wxOK);
		return;
	}

	WallBrush* newBrush = brush->asWall();
	if (newBrush == source_brush) {
		g_gui.PopupDialog("Replace Wall", "Pick a different wall brush than the one being replaced.", wxOK);
		return;
	}

	if (ReplaceConnectedWalls(editor, start_pos, source_brush, newBrush)) {
		g_gui.RefreshView();
		EndModal(wxID_OK);
	} else {
		g_gui.PopupDialog("Replace Wall", "No connected walls were found to replace.", wxOK);
	}
}

void ReplaceWallDialog::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}
