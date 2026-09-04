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

#include "items.h"
#include "item.h"
#include "brush.h"
#include "raw_brush.h"
#include "materials.h"
#include "tileset.h"

#include "gui.h"
#include "application.h"
#include "dcbutton.h"
#include "add_to_tileset_window.h"

// Persistent memory: re-use the last choices when the dialog is opened again.
namespace {
struct LastUsed {
	int palette_index = 4; // 0=Terrain,1=Doodad,2=Item,3=Collection,4=Raw
	std::string tileset_name;
	TilesetInsertSpec spec = { TilesetInsertSpec::BOTTOM, 0 };
};
static LastUsed s_last;
} // namespace

namespace {

// Canonical XML tag for a category (used when creating a new category node).
const char* canonicalCategoryTag(TilesetCategoryType type) {
	switch (type) {
		case TILESET_TERRAIN:
			return "terrain";
		case TILESET_DOODAD:
			return "doodad";
		case TILESET_ITEM:
			return "items";
		case TILESET_COLLECTION:
			return "collections";
		case TILESET_RAW:
		default:
			return "raw";
	}
}

// Mirrors Tileset::loadCategory: does an XML category tag feed the given type?
bool nodeFeedsCategory(const std::string& nodeName, TilesetCategoryType type) {
	switch (type) {
		case TILESET_TERRAIN:
			return nodeName == "terrain" || nodeName == "terrain_and_raw" || nodeName == "collections_and_terrain";
		case TILESET_DOODAD:
			return nodeName == "doodad" || nodeName == "doodad_and_raw";
		case TILESET_ITEM:
			return nodeName == "items" || nodeName == "items_and_raw";
		case TILESET_COLLECTION:
			return nodeName == "collections" || nodeName == "collections_and_raw" || nodeName == "collections_and_terrain";
		case TILESET_RAW:
			return nodeName == "raw" || nodeName == "terrain_and_raw" || nodeName == "collections_and_raw" || nodeName == "doodad_and_raw" || nodeName == "items_and_raw";
		default:
			return false;
	}
}

std::string locateTilesetSourceFile() {
	for (const auto& entry : g_materials.tilesets) {
		if (entry.second && !entry.second->getSourceFile().empty()) {
			return entry.second->getSourceFile();
		}
	}
	return std::string();
}

// Runtime: translate an insert spec into an index in the brushlist.
size_t runtimeInsertIndex(const std::vector<Brush*>& list, const TilesetInsertSpec& spec) {
	if (spec.kind == TilesetInsertSpec::TOP) {
		return 0;
	}
	if (spec.kind == TilesetInsertSpec::BOTTOM) {
		return list.size();
	}

	int separatorsSeen = 0;
	for (size_t i = 0; i < list.size(); ++i) {
		if (list[i] && list[i]->isPaletteSeparator()) {
			if (separatorsSeen == spec.separator_index) {
				return spec.kind == TilesetInsertSpec::ABOVE_SEPARATOR ? i : i + 1;
			}
			++separatorsSeen;
		}
	}
	return list.size();
}

pugi::xml_node findTilesetNode(pugi::xml_node root, const std::string& tilesetName) {
	for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
		if (as_lower_str(child.name()) == "tileset" && tilesetName == child.attribute("name").as_string()) {
			return child;
		}
	}
	return pugi::xml_node();
}

pugi::xml_node findCategoryNode(pugi::xml_node tilesetNode, TilesetCategoryType type, bool create) {
	for (pugi::xml_node child = tilesetNode.first_child(); child; child = child.next_sibling()) {
		if (nodeFeedsCategory(as_lower_str(child.name()), type)) {
			return child;
		}
	}
	if (create) {
		return tilesetNode.append_child(canonicalCategoryTag(type));
	}
	return pugi::xml_node();
}

// Remove existing single <item id="X"/> nodes so re-positioning acts as a move
// rather than leaving duplicates. Ranges (fromid/toid) are left untouched.
void removeMatchingItemNodes(pugi::xml_node categoryNode, uint16_t itemId) {
	for (pugi::xml_node child = categoryNode.first_child(); child;) {
		pugi::xml_node next = child.next_sibling();
		if (as_lower_str(child.name()) == "item" && !child.attribute("fromid") && child.attribute("id").as_uint() == itemId) {
			categoryNode.remove_child(child);
		}
		child = next;
	}
}

bool persistAdd(const std::string& file, const std::string& tilesetName, TilesetCategoryType type, uint16_t itemId, const TilesetInsertSpec& spec, wxString& error) {
	pugi::xml_document doc;
	if (!doc.load_file(file.c_str(), pugi::parse_full)) {
		error = wxString::Format("Could not open '%s'.", wxstr(file));
		return false;
	}

	pugi::xml_node root = doc.child("materials");
	if (!root) {
		error = wxString::Format("Invalid materials file '%s'.", wxstr(file));
		return false;
	}

	pugi::xml_node tilesetNode = findTilesetNode(root, tilesetName);
	if (!tilesetNode) {
		tilesetNode = root.append_child("tileset");
		tilesetNode.append_attribute("name") = tilesetName.c_str();
	}

	pugi::xml_node categoryNode = findCategoryNode(tilesetNode, type, true);
	if (!categoryNode) {
		error = "Could not create the tileset category.";
		return false;
	}

	removeMatchingItemNodes(categoryNode, itemId);

	pugi::xml_node itemNode;
	if (spec.kind == TilesetInsertSpec::TOP) {
		itemNode = categoryNode.prepend_child("item");
	} else if (spec.kind == TilesetInsertSpec::BOTTOM) {
		itemNode = categoryNode.append_child("item");
	} else {
		pugi::xml_node separator;
		int separatorsSeen = 0;
		for (pugi::xml_node child = categoryNode.first_child(); child; child = child.next_sibling()) {
			if (as_lower_str(child.name()) == "separator") {
				if (separatorsSeen == spec.separator_index) {
					separator = child;
					break;
				}
				++separatorsSeen;
			}
		}
		if (separator) {
			itemNode = (spec.kind == TilesetInsertSpec::ABOVE_SEPARATOR)
				? categoryNode.insert_child_before("item", separator)
				: categoryNode.insert_child_after("item", separator);
		} else {
			itemNode = categoryNode.append_child("item");
		}
	}

	itemNode.append_attribute("id") = itemId;

	if (!doc.save_file(file.c_str())) {
		error = wxString::Format("Could not save '%s'.", wxstr(file));
		return false;
	}
	return true;
}

} // namespace

bool AddItemToTilesetAt(uint16_t itemId, const std::string& tilesetName, TilesetCategoryType categoryType, const TilesetInsertSpec& spec, wxString& error) {
	ItemType& it = g_items[itemId];
	if (it.id == 0) {
		error = "That item does not exist.";
		return false;
	}
	if (it.isMetaItem()) {
		error = "Meta items cannot be added to a tileset.";
		return false;
	}
	if (tilesetName.empty()) {
		error = "Please choose or type a tileset.";
		return false;
	}

	Tileset* tileset;
	std::string file;
	auto iter = g_materials.tilesets.find(tilesetName);
	if (iter != g_materials.tilesets.end()) {
		tileset = iter->second;
		file = tileset->getSourceFile();
	} else {
		tileset = newd Tileset(g_brushes, tilesetName);
		g_materials.tilesets.insert(std::make_pair(tilesetName, tileset));
		file = locateTilesetSourceFile();
		tileset->setSourceFile(file);
	}

	TilesetCategory* category = tileset->getCategory(categoryType);

	RAWBrush* rawBrush = it.raw_brush;
	if (!rawBrush) {
		rawBrush = it.raw_brush = newd RAWBrush(it.id);
		it.has_raw = true;
		g_brushes.addBrush(rawBrush);
	}
	rawBrush->flagAsVisible();
	if (categoryType == TILESET_COLLECTION) {
		rawBrush->setCollection();
	}

	// Remove any existing occurrence so the chosen position acts as a move.
	std::vector<Brush*>& list = category->brushlist;
	for (auto i = list.begin(); i != list.end();) {
		if (*i == rawBrush) {
			i = list.erase(i);
		} else {
			++i;
		}
	}

	size_t index = std::min(runtimeInsertIndex(list, spec), list.size());
	list.insert(list.begin() + index, rawBrush);
	it.in_other_tileset = true;

	if (!file.empty()) {
		if (!persistAdd(file, tilesetName, categoryType, itemId, spec, error)) {
			return false;
		}
	}

	g_materials.modify();
	return true;
}

// ============================================================================
// Add to Tileset window

BEGIN_EVENT_TABLE(AddToTilesetWindow, wxDialog)
EVT_BUTTON(wxID_OK, AddToTilesetWindow::OnClickOK)
EVT_BUTTON(wxID_CANCEL, AddToTilesetWindow::OnClickCancel)
END_EVENT_TABLE()

AddToTilesetWindow::AddToTilesetWindow(wxWindow* win_parent, uint16_t itemId, TilesetCategoryType defaultCategory, wxPoint pos) :
	AddToTilesetWindow(win_parent, std::vector<uint16_t> { itemId }, defaultCategory, pos) {
}

AddToTilesetWindow::AddToTilesetWindow(wxWindow* win_parent, std::vector<uint16_t> itemIds, TilesetCategoryType defaultCategory, wxPoint pos) :
	ObjectPropertiesWindowBase(win_parent, "Add to Tileset", pos),
	item_ids(std::move(itemIds)),
	palette_field(nullptr),
	tileset_field(nullptr),
	position_field(nullptr) {

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* boxsizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Add item to tileset");

	// Item preview — show the first item sprite plus a summary label.
	wxSizer* itemsizer = newd wxBoxSizer(wxHORIZONTAL);
	if (!item_ids.empty()) {
		const ItemType& it = g_items.getItemType(item_ids[0]);
		DCButton* itembtn = newd DCButton(this, wxID_ANY, wxDefaultPosition, DC_BTN_TOGGLE, RENDER_SIZE_32x32, 0);
		itembtn->SetSprite(it.clientID);
		itemsizer->Add(itembtn, wxSizerFlags(0).CenterVertical().Border(wxRIGHT, 8));
		wxString label;
		if (item_ids.size() == 1) {
			label = "ID " + i2ws(item_ids[0]) + "  \"" + wxstr(it.name) + "\"";
		} else {
			label = wxString::Format("%zu items selected (first: ID %u \"%s\")", item_ids.size(), (unsigned)item_ids[0], wxstr(it.name));
		}
		itemsizer->Add(newd wxStaticText(this, wxID_ANY, label), wxSizerFlags(1).CenterVertical());
	}
	boxsizer->Add(itemsizer, wxSizerFlags(0).Expand().Border(wxALL, 6));

	wxFlexGridSizer* grid = newd wxFlexGridSizer(2, 8, 8);
	grid->AddGrowableCol(1);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Palette"), wxSizerFlags(0).CenterVertical());
	palette_field = newd wxChoice(this, wxID_ANY);
	palette_field->Append("Terrain", newd int(TILESET_TERRAIN));
	palette_field->Append("Doodad", newd int(TILESET_DOODAD));
	palette_field->Append("Item", newd int(TILESET_ITEM));
	palette_field->Append("Collection", newd int(TILESET_COLLECTION));
	palette_field->Append("Raw", newd int(TILESET_RAW));
	// If a previous session stored a palette, use it; otherwise fall back to defaultCategory.
	if (s_last.palette_index >= 0) {
		palette_field->SetSelection(s_last.palette_index);
	} else {
		switch (defaultCategory) {
			case TILESET_TERRAIN: palette_field->SetSelection(0); break;
			case TILESET_DOODAD: palette_field->SetSelection(1); break;
			case TILESET_ITEM: palette_field->SetSelection(2); break;
			case TILESET_COLLECTION: palette_field->SetSelection(3); break;
			default: palette_field->SetSelection(4); break;
		}
	}
	grid->Add(palette_field, wxSizerFlags(1).Expand());

	// Make the value controls wide enough to show long tileset names and the
	// "Below separator #N (before '<item name>')" position labels in full.
	const wxSize fieldSize(420, -1);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Tileset"), wxSizerFlags(0).CenterVertical());
	tileset_field = newd wxComboBox(this, wxID_ANY, "", wxDefaultPosition, fieldSize);
	grid->Add(tileset_field, wxSizerFlags(1).Expand());

	grid->Add(newd wxStaticText(this, wxID_ANY, "Position"), wxSizerFlags(0).CenterVertical());
	position_field = newd wxChoice(this, wxID_ANY, wxDefaultPosition, fieldSize);
	grid->Add(position_field, wxSizerFlags(1).Expand());

	boxsizer->Add(grid, wxSizerFlags(0).Expand().Border(wxALL, 6));
	topsizer->Add(boxsizer, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 12));

	wxSizer* buttonsizer = newd wxBoxSizer(wxHORIZONTAL);
	buttonsizer->Add(newd wxButton(this, wxID_OK, "Add"), wxSizerFlags(1).Center().Border(wxALL, 6));
	buttonsizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), wxSizerFlags(1).Center().Border(wxALL, 6));
	topsizer->Add(buttonsizer, wxSizerFlags(0).Center().Border(wxALL, 8));

	RebuildTilesetChoices();
	// Restore last-used tileset name if it's available in the rebuilt list.
	if (!s_last.tileset_name.empty()) {
		const wxString remembered = wxstr(s_last.tileset_name);
		if (tileset_field->FindString(remembered) != wxNOT_FOUND) {
			tileset_field->SetValue(remembered);
		}
	}
	RebuildPositionChoices();
	// Restore last-used position (match by kind + separator_index).
	for (int i = 0; i < static_cast<int>(position_specs.size()); ++i) {
		if (position_specs[i].kind == s_last.spec.kind && position_specs[i].separator_index == s_last.spec.separator_index) {
			position_field->SetSelection(i);
			break;
		}
	}

	SetSizerAndFit(topsizer);
	Centre(wxBOTH);

	palette_field->Connect(wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(AddToTilesetWindow::OnChangePalette), NULL, this);
	tileset_field->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AddToTilesetWindow::OnChangeTileset), NULL, this);
	tileset_field->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(AddToTilesetWindow::OnChangeTileset), NULL, this);
}

TilesetCategoryType AddToTilesetWindow::GetSelectedCategory() const {
	int sel = palette_field->GetSelection();
	if (sel == wxNOT_FOUND) {
		return TILESET_RAW;
	}
	int* data = static_cast<int*>(palette_field->GetClientData(sel));
	return data ? TilesetCategoryType(*data) : TILESET_RAW;
}

void AddToTilesetWindow::RebuildTilesetChoices() {
	const wxString previous = tileset_field->GetValue();
	const TilesetCategoryType type = GetSelectedCategory();

	tileset_field->Clear();
	for (const auto& entry : g_materials.tilesets) {
		const TilesetCategory* category = entry.second ? entry.second->getCategory(type) : nullptr;
		if (category && category->size() > 0) {
			tileset_field->Append(wxstr(entry.first));
		}
	}

	if (!previous.IsEmpty() && tileset_field->FindString(previous) != wxNOT_FOUND) {
		tileset_field->SetValue(previous);
	} else if (tileset_field->GetCount() > 0) {
		tileset_field->SetSelection(0);
	}
}

void AddToTilesetWindow::RebuildPositionChoices() {
	position_specs.clear();
	position_field->Clear();

	const TilesetCategoryType type = GetSelectedCategory();
	const std::string tilesetName = std::string(tileset_field->GetValue().Trim(true).Trim(false).mb_str());

	auto addPosition = [&](const wxString& label, const TilesetInsertSpec& spec) {
		position_field->Append(label);
		position_specs.push_back(spec);
	};

	addPosition("At the top", { TilesetInsertSpec::TOP, 0 });

	auto iter = g_materials.tilesets.find(tilesetName);
	if (iter != g_materials.tilesets.end()) {
		const TilesetCategory* category = iter->second->getCategory(type);
		if (category) {
			int separatorIndex = 0;
			std::string previousName = "(start)";
			for (size_t i = 0; i < category->brushlist.size(); ++i) {
				Brush* brush = category->brushlist[i];
				if (!brush) {
					continue;
				}
				if (brush->isPaletteSeparator()) {
					std::string nextName = "(end)";
					for (size_t j = i + 1; j < category->brushlist.size(); ++j) {
						if (category->brushlist[j] && !category->brushlist[j]->isPaletteSeparator()) {
							nextName = category->brushlist[j]->getName();
							break;
						}
					}
					const int human = separatorIndex + 1;
					addPosition(wxString::Format("Above separator #%d (after '%s')", human, wxstr(previousName)), { TilesetInsertSpec::ABOVE_SEPARATOR, separatorIndex });
					addPosition(wxString::Format("Below separator #%d (before '%s')", human, wxstr(nextName)), { TilesetInsertSpec::BELOW_SEPARATOR, separatorIndex });
					++separatorIndex;
				} else {
					previousName = brush->getName();
				}
			}
		}
	}

	addPosition("At the bottom", { TilesetInsertSpec::BOTTOM, 0 });

	position_field->SetSelection(position_field->GetCount() - 1); // default: bottom
}

void AddToTilesetWindow::OnChangePalette(wxCommandEvent& WXUNUSED(event)) {
	RebuildTilesetChoices();
	RebuildPositionChoices();
}

void AddToTilesetWindow::OnChangeTileset(wxCommandEvent& WXUNUSED(event)) {
	RebuildPositionChoices();
}

void AddToTilesetWindow::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	const std::string tilesetName = std::string(tileset_field->GetValue().Trim(true).Trim(false).mb_str());
	const TilesetCategoryType type = GetSelectedCategory();

	int posSel = position_field->GetSelection();
	if (posSel == wxNOT_FOUND || posSel >= static_cast<int>(position_specs.size())) {
		g_gui.PopupDialog("Error", "Please choose a position.", wxOK);
		return;
	}

	const TilesetInsertSpec& spec = position_specs[posSel];
	wxArrayString errors;
	for (uint16_t id : item_ids) {
		wxString err;
		if (!AddItemToTilesetAt(id, tilesetName, type, spec, err)) {
			errors.Add(err);
		}
	}

	if (!errors.IsEmpty()) {
		wxString msg;
		for (const wxString& e : errors) {
			msg += e + "\n";
		}
		g_gui.PopupDialog("Error", msg, wxOK);
		return;
	}

	// Persist choices for next open.
	s_last.palette_index = palette_field->GetSelection();
	s_last.tileset_name = tilesetName;
	s_last.spec = spec;

	result_tileset = tilesetName;
	result_category = type;
	EndModal(1);
}

void AddToTilesetWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(0);
}
