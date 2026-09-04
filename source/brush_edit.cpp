//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "brush_edit.h"
#include "brush.h"
#include "doodad_brush.h"
#include "ground_brush.h"
#include "wall_brush.h"
#include "items.h"
#include "item.h"
#include "gui.h"
#include "graphics.h"

namespace {

pugi::xml_node findBrushNode(pugi::xml_node root, const std::string& brushName) {
	for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
		if (as_lower_str(child.name()) != "brush") {
			continue;
		}
		pugi::xml_attribute nameAttr = child.attribute("name");
		if (nameAttr && brushName == nameAttr.as_string()) {
			return child;
		}
	}
	return pugi::xml_node();
}

void removeChildNodesByName(pugi::xml_node parent, const char* childName) {
	for (pugi::xml_node child = parent.first_child(); child;) {
		pugi::xml_node next = child.next_sibling();
		if (as_lower_str(child.name()) == childName) {
			parent.remove_child(child);
		}
		child = next;
	}
}

void appendItemNode(pugi::xml_node parent, uint16_t itemId, int chance) {
	pugi::xml_node itemNode = parent.append_child("item");
	itemNode.append_attribute("id") = itemId;
	itemNode.append_attribute("chance") = chance;
}

void appendCompositeNode(pugi::xml_node parent, int chance, const std::vector<std::pair<Position, uint16_t>>& tiles) {
	pugi::xml_node compositeNode = parent.append_child("composite");
	compositeNode.append_attribute("chance") = chance;

	for (const auto& tileEntry : tiles) {
		pugi::xml_node tileNode = compositeNode.append_child("tile");
		tileNode.append_attribute("x") = tileEntry.first.x;
		tileNode.append_attribute("y") = tileEntry.first.y;
		if (tileEntry.first.z != 0) {
			tileNode.append_attribute("z") = tileEntry.first.z;
		}

		pugi::xml_node itemNode = tileNode.append_child("item");
		itemNode.append_attribute("id") = tileEntry.second;
	}
}

int wallAlignmentFromType(const std::string& type) {
	if (type == "horizontal") {
		return 0;
	}
	if (type == "vertical") {
		return 1;
	}
	if (type == "corner") {
		return 2;
	}
	if (type == "pole") {
		return 3;
	}
	if (type == "entrance") {
		return 4;
	}
	if (type.rfind("alignment ", 0) == 0) {
		try {
			return std::stoi(type.substr(10));
		} catch (...) {
			return -1;
		}
	}
	return -1;
}

bool updateDoodadBrushXml(pugi::xml_node brushNode, const std::vector<BrushEditEntry>& entries) {
	removeChildNodesByName(brushNode, "item");
	removeChildNodesByName(brushNode, "composite");

	for (const BrushEditEntry& entry : entries) {
		if (entry.kind == BRUSH_EDIT_ITEM) {
			appendItemNode(brushNode, entry.item_id, entry.chance);
		} else if (entry.kind == BRUSH_EDIT_COMPOSITE) {
			appendCompositeNode(brushNode, entry.chance, entry.composite_tiles);
		}
	}
	return true;
}

bool updateGroundBrushXml(pugi::xml_node brushNode, const std::vector<BrushEditEntry>& entries) {
	auto borderNodeHasSpecific = [](pugi::xml_node borderNode) {
		for (pugi::xml_node child = borderNode.first_child(); child; child = child.next_sibling()) {
			if (as_lower_str(child.name()) == "specific") {
				return true;
			}
		}
		return false;
	};

	removeChildNodesByName(brushNode, "item");
	for (pugi::xml_node child = brushNode.first_child(); child;) {
		pugi::xml_node next = child.next_sibling();
		const std::string& childName = as_lower_str(child.name());
		if (childName == "optional") {
			brushNode.remove_child(child);
		} else if (childName == "border" && !borderNodeHasSpecific(child)) {
			brushNode.remove_child(child);
		}
		child = next;
	}

	for (const BrushEditEntry& entry : entries) {
		if (entry.kind == BRUSH_EDIT_ITEM) {
			appendItemNode(brushNode, entry.item_id, entry.chance);
		} else if (entry.kind == BRUSH_EDIT_GROUND_BORDER) {
			pugi::xml_node borderNode = brushNode.append_child("border");
			borderNode.append_attribute("align") = entry.border_outer ? "outer" : "inner";
			if (entry.border_to == "none") {
				borderNode.append_attribute("to") = "none";
			} else if (entry.border_to != "all" && !entry.border_to.empty()) {
				borderNode.append_attribute("to") = entry.border_to.c_str();
			}
			if (entry.border_super) {
				borderNode.append_attribute("super") = true;
			}
			if (entry.border_id != 0) {
				borderNode.append_attribute("id") = entry.border_id;
			} else if (entry.ground_equivalent != 0) {
				borderNode.append_attribute("ground_equivalent") = entry.ground_equivalent;
			}
		} else if (entry.kind == BRUSH_EDIT_GROUND_OPTIONAL) {
			pugi::xml_node optionalNode = brushNode.append_child("optional");
			optionalNode.append_attribute("id") = entry.border_id;
		}
	}
	return true;
}

bool updateWallBrushXml(pugi::xml_node brushNode, const std::vector<BrushEditEntry>& entries) {
	std::map<int, std::vector<std::pair<uint16_t, int>>> grouped;
	for (const BrushEditEntry& entry : entries) {
		if (entry.kind != BRUSH_EDIT_ITEM) {
			continue;
		}
		int alignment = wallAlignmentFromType(entry.wall_alignment);
		if (alignment < 0) {
			continue;
		}
		grouped[alignment].push_back(std::make_pair(entry.item_id, entry.chance));
	}

	for (pugi::xml_node child = brushNode.first_child(); child;) {
		pugi::xml_node next = child.next_sibling();
		if (as_lower_str(child.name()) == "wall") {
			pugi::xml_attribute typeAttr = child.attribute("type");
			if (typeAttr) {
				int alignment = wallAlignmentFromType(typeAttr.as_string());
				auto it = grouped.find(alignment);
				if (it != grouped.end()) {
					removeChildNodesByName(child, "item");
					for (const auto& itemEntry : it->second) {
						appendItemNode(child, itemEntry.first, itemEntry.second);
					}
					grouped.erase(it);
				}
			}
		}
		child = next;
	}
	return true;
}

} // namespace

bool BrushCanBeEdited(Brush* brush) {
	if (!brush) {
		return false;
	}
	return brush->isDoodad() || brush->isGround() || brush->isWall();
}

bool BrushExtractEditEntries(Brush* brush, std::vector<BrushEditEntry>& entries, wxString& error) {
	entries.clear();
	if (!brush) {
		error = "No brush selected.";
		return false;
	}

	if (brush->isDoodad()) {
		return brush->asDoodad()->extractEditEntries(entries, error);
	}
	if (brush->isGround()) {
		return brush->asGround()->extractEditEntries(entries);
	}
	if (brush->isWall()) {
		return brush->asWall()->extractEditEntries(entries);
	}

	error = "This brush type cannot be edited.";
	return false;
}

bool BrushApplyEditEntries(Brush* brush, const std::vector<BrushEditEntry>& entries, wxString& error) {
	if (!brush) {
		error = "No brush selected.";
		return false;
	}

	if (brush->isDoodad()) {
		std::vector<std::pair<int, uint16_t>> singleItems;
		std::vector<std::pair<int, std::vector<std::pair<Position, uint16_t>>>> composites;

		for (const BrushEditEntry& entry : entries) {
			if (entry.kind == BRUSH_EDIT_ITEM) {
				if (entry.item_id == 0 || entry.chance <= 0) {
					error = "Each item needs a valid ID and a positive chance.";
					return false;
				}
				singleItems.push_back(std::make_pair(entry.chance, entry.item_id));
			} else if (entry.kind == BRUSH_EDIT_COMPOSITE) {
				if (entry.composite_tiles.empty() || entry.chance <= 0) {
					error = "Each composite needs tiles and a positive chance.";
					return false;
				}
				for (const auto& tileEntry : entry.composite_tiles) {
					if (tileEntry.second == 0 || g_items.getItemType(tileEntry.second).id == 0) {
						error = "Each composite tile needs a valid item ID.";
						return false;
					}
				}
				composites.push_back(std::make_pair(entry.chance, entry.composite_tiles));
			}
		}

		brush->asDoodad()->replaceDefaultAlternative(singleItems, composites);
		return true;
	}

	if (brush->isGround()) {
		for (const BrushEditEntry& entry : entries) {
			if (entry.kind == BRUSH_EDIT_ITEM) {
				if (entry.item_id == 0 || entry.chance < 0) {
					error = "Each item needs a valid ID and a non-negative chance.";
					return false;
				}
			} else if (entry.kind == BRUSH_EDIT_GROUND_BORDER) {
				if (entry.border_id == 0 && entry.ground_equivalent == 0) {
					error = "Each border needs a border id or ground equivalent item.";
					return false;
				}
			} else if (entry.kind == BRUSH_EDIT_GROUND_OPTIONAL) {
				if (entry.border_id == 0) {
					error = "Optional border needs a valid border id.";
					return false;
				}
			}
		}
		brush->asGround()->replaceFromEditEntries(entries);
		return true;
	}

	if (brush->isWall()) {
		std::map<int, std::vector<std::pair<uint16_t, int>>> grouped;
		for (const BrushEditEntry& entry : entries) {
			if (entry.kind != BRUSH_EDIT_ITEM) {
				continue;
			}
			if (entry.item_id == 0 || entry.chance < 0) {
				error = "Each item needs a valid ID and a non-negative chance.";
				return false;
			}
			int alignment = wallAlignmentFromType(entry.wall_alignment);
			if (alignment < 0) {
				error = "Invalid wall alignment in brush entry.";
				return false;
			}
			grouped[alignment].push_back(std::make_pair(entry.item_id, entry.chance));
		}

		WallBrush* wall = brush->asWall();
		for (int alignment = 0; alignment < 17; ++alignment) {
			auto it = grouped.find(alignment);
			if (it != grouped.end()) {
				wall->replaceWallItems(alignment, it->second);
			}
		}
		return true;
	}

	error = "This brush type cannot be edited.";
	return false;
}

bool BrushSaveToXml(Brush* brush, wxString& error) {
	if (!brush) {
		error = "No brush selected.";
		return false;
	}

	const std::string& sourceFile = brush->getSourceFile();
	if (sourceFile.empty()) {
		error = "This brush has no known source XML file.";
		return false;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(sourceFile.c_str(), pugi::parse_full);
	if (!result) {
		error = wxString::Format("Could not open '%s' for writing.", wxstr(sourceFile));
		return false;
	}

	pugi::xml_node root = doc.child("materials");
	if (!root) {
		error = "Invalid materials file root.";
		return false;
	}

	pugi::xml_node brushNode = findBrushNode(root, brush->getName());
	if (!brushNode) {
		error = wxString::Format("Could not find brush '%s' in '%s'.", wxstr(brush->getName()), wxstr(sourceFile));
		return false;
	}

	std::vector<BrushEditEntry> entries;
	if (!BrushExtractEditEntries(brush, entries, error)) {
		return false;
	}

	if (brush->isDoodad()) {
		updateDoodadBrushXml(brushNode, entries);
	} else if (brush->isGround()) {
		updateGroundBrushXml(brushNode, entries);
	} else if (brush->isWall()) {
		updateWallBrushXml(brushNode, entries);
	} else {
		error = "This brush type cannot be saved.";
		return false;
	}

	if (!doc.save_file(sourceFile.c_str())) {
		error = wxString::Format("Could not save '%s'.", wxstr(sourceFile));
		return false;
	}

	return true;
}

std::vector<uint32_t> BrushGetAllBorderIds() {
	return g_brushes.getAutoBorderIds();
}

void DrawAutoBorderPreview(wxDC& dc, const wxRect& rect, const AutoBorder* border) {
	if (!border || rect.GetWidth() <= 0 || rect.GetHeight() <= 0) {
		return;
	}

	const int tileSize = std::min(rect.GetWidth(), rect.GetHeight());
	const int ox = rect.GetX() + (rect.GetWidth() - tileSize) / 2;
	const int oy = rect.GetY() + (rect.GetHeight() - tileSize) / 2;

	static const int drawOrder[] = {
		NORTHWEST_DIAGONAL,
		NORTHEAST_DIAGONAL,
		SOUTHEAST_DIAGONAL,
		SOUTHWEST_DIAGONAL,
		NORTHWEST_CORNER,
		NORTHEAST_CORNER,
		SOUTHWEST_CORNER,
		SOUTHEAST_CORNER,
		NORTH_HORIZONTAL,
		EAST_HORIZONTAL,
		SOUTH_HORIZONTAL,
		WEST_HORIZONTAL,
	};

	for (int edgeId : drawOrder) {
		if (edgeId >= 13) {
			continue;
		}
		const uint32_t itemId = border->tiles[edgeId];
		if (itemId == 0) {
			continue;
		}
		const ItemType& itemType = g_items[itemId];
		if (itemType.id == 0) {
			continue;
		}
		Sprite* sprite = g_gui.gfx.getSprite(itemType.clientID);
		if (sprite) {
			sprite->DrawTo(&dc, SPRITE_SIZE_32x32, ox, oy, tileSize, tileSize);
		}
	}
}
