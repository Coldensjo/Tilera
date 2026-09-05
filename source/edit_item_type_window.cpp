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

#include <wx/grid.h>
#include <wx/spinctrl.h>
#include <wx/dnd.h>

#include "items.h"
#include "gui.h"
#include "graphics.h"
#include "theme.h"
#include "client_version.h"
#include "dcbutton.h"
#include "item_shaders.h"
#include "edit_item_type_window.h"

namespace {

// The items.xml attribute vocabulary (TFS-style), grouped by what kind of
// item can carry each key, so the key-column dropdown only suggests keys that
// make sense for the item being edited (an armor never sees shootType, a
// sword never sees floorChange). Free text is still allowed. The editor
// itself only interprets a subset of these — the rest are written for the
// game server.

// The key vocabulary mirrors the Ironcore server's items.xml parser (the
// ITEM_PARSE_* map in its src/items.cpp) so the editor never suggests a key
// the server would ignore, and misses none it supports.

// Applies to any item.
const char* const KEYS_GENERAL[] = { "description", "weight", "worth", "showCount", "pickupable", "allowPickupable", "moveable", "storeItem", "shader", "forceSave", "forceSerialize" };
// Applies to anything that can be equipped.
const char* const KEYS_EQUIP[] = { "slotType", "vocation", "charges", "showCharges", "showAttributes", "duration", "showDuration", "decayTo", "transformEquipTo", "transformDeEquipTo" };
// Wearer stat bonuses.
const char* const KEYS_STATS[] = { "speed", "manaShield", "healthGain", "healthTicks", "manaGain", "manaTicks", "maxHitPoints", "maxHitPointsPercent", "maxManaPoints", "maxManaPointsPercent", "maxSoulPoints", "maxSoulPointsPercent", "soulGain", "soulTicks", "magicLevelPoints", "magicPoints", "magicPointsPercent", "skillSword", "skillAxe", "skillClub", "skillDist", "skillShield", "skillFist", "skillFish", "criticalHitChance", "criticalHitAmount", "lifeLeechChance", "lifeLeechAmount", "manaLeechChance", "manaLeechAmount", "manaOnDamage", "manaOnDamagePercent", "dodge", "parry", "blockChance", "blockAmount", "armorPenetration", "magicPenetration" };
// Resistances, flat protections, reflects and condition suppression.
const char* const KEYS_PROTECTION[] = { "absorbPercentAll", "absorbPercentElements", "absorbPercentMagic", "absorbPercentPhysical", "absorbPercentFire", "absorbPercentEnergy", "absorbPercentEarth", "absorbPercentPoison", "absorbPercentIce", "absorbPercentHoly", "absorbPercentDeath", "absorbPercentLifeDrain", "absorbPercentManaDrain", "absorbPercentDrown", "absorbPercentHealing", "absorbPercentUndefined", "protectionPhysicalFlat", "protectionFireFlat", "protectionEnergyFlat", "protectionPoisonFlat", "protectionIceFlat", "protectionHolyFlat", "protectionDeathFlat", "protectionLifeDrainFlat", "protectionManaDrainFlat", "protectionDrownFlat", "protectionHealingFlat", "reflectPercentAll", "reflectPercentElements", "reflectPercentMagic", "reflectPercentPhysical", "reflectPercentFire", "reflectPercentEnergy", "reflectPercentEarth", "reflectPercentIce", "reflectPercentHoly", "reflectPercentDeath", "reflectPercentLifeDrain", "reflectPercentManaDrain", "reflectPercentDrown", "reflectPercentHealing", "reflectChanceAll", "reflectChanceElements", "reflectChanceMagic", "reflectChancePhysical", "reflectChanceFire", "reflectChanceEnergy", "reflectChanceEarth", "reflectChanceIce", "reflectChanceHoly", "reflectChanceDeath", "reflectChanceLifeDrain", "reflectChanceManaDrain", "reflectChanceDrown", "reflectChanceHealing", "fieldAbsorbPercentFire", "fieldAbsorbPercentEnergy", "fieldAbsorbPercentEarth", "damageTaken", "suppressDrunk", "suppressEnergy", "suppressFire", "suppressPoison", "suppressDrown", "suppressPhysical", "suppressFreeze", "suppressDazzle", "suppressCurse" };
// Damage boosts (weapons and offensive equipment).
const char* const KEYS_BOOST[] = { "boostPercentAll", "boostPercentElements", "boostPercentMagic", "boostPercentPhysical", "boostPercentFire", "boostPercentEnergy", "boostPercentEarth", "boostPercentIce", "boostPercentHoly", "boostPercentDeath", "boostPercentLifeDrain", "boostPercentManaDrain", "boostPercentDrown", "boostPercentHealing" };
const char* const KEYS_MELEE[] = { "weaponType", "attack", "defense", "extraDef", "attackSpeed" };
const char* const KEYS_DISTANCE[] = { "weaponType", "ammoType", "range", "hitChance", "maxHitChance", "distanceHitChance", "attackSpeed" };
const char* const KEYS_AMMO[] = { "weaponType", "ammoType", "shootType", "attack", "hitChance" };
const char* const KEYS_WAND[] = { "weaponType", "shootType", "range" };
const char* const KEYS_SHIELD[] = { "weaponType", "defense" };
const char* const KEYS_ARMOR[] = { "armor" };
const char* const KEYS_CONTAINER[] = { "containerSize" };
// Everything else — only offered while the item kind is still undetermined.
const char* const KEYS_MISC[] = { "type", "rotateTo", "floorChange", "corpseType", "fluidSource", "effect", "blocking", "blockProjectile", "walkStack", "replaceable", "invisible", "allowDistRead", "readable", "writeable", "maxTextLen", "writeOnceItemId", "levelDoor", "partnerDirection", "field", "runeSpellName", "stopDuration", "transformTo", "destroyTo", "maleTransformTo", "femaleTransformTo", "maleSleeper", "femaleSleeper" };

template <size_t N>
void appendKeys(std::vector<wxString>& out, const char* const (&group)[N]) {
	for (const char* key : group) {
		bool present = false;
		for (const wxString& existing : out) {
			if (existing.CmpNoCase(key) == 0) {
				present = true;
				break;
			}
		}
		if (!present) {
			out.push_back(key);
		}
	}
}

// What kind of equipment the attribute list currently describes, judged from
// its weaponType / slotType / containerSize entries. weaponType wins over
// slotType so a two-handed sword still counts as a melee weapon.
enum class EquipKind {
	Generic,
	MeleeWeapon,
	DistanceWeapon,
	Ammunition,
	Wand,
	Shield,
	Armor,
	Trinket,
	Container,
};

EquipKind detectEquipKind(const std::vector<ItemXmlAttribute>& attributes) {
	EquipKind slotKind = EquipKind::Generic;
	for (const ItemXmlAttribute& attribute : attributes) {
		const std::string key = as_lower_str(attribute.key);
		const std::string value = as_lower_str(attribute.value);
		if (key == "weapontype") {
			if (value == "sword" || value == "club" || value == "axe") {
				return EquipKind::MeleeWeapon;
			} else if (value == "distance") {
				return EquipKind::DistanceWeapon;
			} else if (value == "ammunition") {
				return EquipKind::Ammunition;
			} else if (value == "wand") {
				return EquipKind::Wand;
			} else if (value == "shield") {
				return EquipKind::Shield;
			}
		} else if (key == "slottype" && slotKind == EquipKind::Generic) {
			if (value == "body" || value == "head" || value == "legs" || value == "feet") {
				slotKind = EquipKind::Armor;
			} else if (value == "ring" || value == "necklace") {
				slotKind = EquipKind::Trinket;
			} else if (value == "two-handed") {
				slotKind = EquipKind::MeleeWeapon;
			}
		} else if (key == "containersize" && slotKind == EquipKind::Generic) {
			slotKind = EquipKind::Container;
		}
	}
	return slotKind;
}

std::vector<wxString> buildKeySuggestions(EquipKind kind) {
	std::vector<wxString> keys;
	appendKeys(keys, KEYS_GENERAL);
	switch (kind) {
		case EquipKind::MeleeWeapon:
			appendKeys(keys, KEYS_MELEE);
			appendKeys(keys, KEYS_BOOST);
			appendKeys(keys, KEYS_EQUIP);
			appendKeys(keys, KEYS_STATS);
			break;
		case EquipKind::DistanceWeapon:
			appendKeys(keys, KEYS_DISTANCE);
			appendKeys(keys, KEYS_BOOST);
			appendKeys(keys, KEYS_EQUIP);
			appendKeys(keys, KEYS_STATS);
			break;
		case EquipKind::Ammunition:
			appendKeys(keys, KEYS_AMMO);
			appendKeys(keys, KEYS_BOOST);
			break;
		case EquipKind::Wand:
			appendKeys(keys, KEYS_WAND);
			appendKeys(keys, KEYS_BOOST);
			appendKeys(keys, KEYS_EQUIP);
			appendKeys(keys, KEYS_STATS);
			break;
		case EquipKind::Shield:
			appendKeys(keys, KEYS_SHIELD);
			appendKeys(keys, KEYS_EQUIP);
			appendKeys(keys, KEYS_STATS);
			appendKeys(keys, KEYS_PROTECTION);
			appendKeys(keys, KEYS_BOOST);
			break;
		case EquipKind::Armor:
			appendKeys(keys, KEYS_ARMOR);
			appendKeys(keys, KEYS_EQUIP);
			appendKeys(keys, KEYS_STATS);
			appendKeys(keys, KEYS_PROTECTION);
			appendKeys(keys, KEYS_BOOST);
			break;
		case EquipKind::Trinket:
			appendKeys(keys, KEYS_EQUIP);
			appendKeys(keys, KEYS_STATS);
			appendKeys(keys, KEYS_PROTECTION);
			appendKeys(keys, KEYS_BOOST);
			break;
		case EquipKind::Container:
			appendKeys(keys, KEYS_CONTAINER);
			break;
		case EquipKind::Generic:
			appendKeys(keys, KEYS_MELEE);
			appendKeys(keys, KEYS_DISTANCE);
			appendKeys(keys, KEYS_AMMO);
			appendKeys(keys, KEYS_WAND);
			appendKeys(keys, KEYS_ARMOR);
			appendKeys(keys, KEYS_EQUIP);
			appendKeys(keys, KEYS_STATS);
			appendKeys(keys, KEYS_PROTECTION);
			appendKeys(keys, KEYS_BOOST);
			appendKeys(keys, KEYS_CONTAINER);
			appendKeys(keys, KEYS_MISC);
			break;
	}
	return keys;
}

// A preset seeds the grid with the attributes an item kind typically carries.
// An empty value means "fill it in yourself" — empty-valued rows are skipped
// when saving.
struct PresetAttribute {
	const char* key;
	const char* value;
};

struct ItemTypePreset {
	const char* name;
	std::vector<PresetAttribute> attributes;
};

// Accepts an image file dropped onto the sprite preview box.
class SpriteImageDropTarget : public wxFileDropTarget {
public:
	SpriteImageDropTarget(EditItemTypeWindow* window) :
		window(window) { }

	bool OnDropFiles(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y), const wxArrayString& filenames) override {
		if (filenames.IsEmpty()) {
			return false;
		}
		window->ImportSpriteFromFile(filenames[0]);
		return true;
	}

private:
	EditItemTypeWindow* window;
};

// Virtual list of all item sprites whose tile layout matches a given size.
class SpritePickerListBox : public wxVListBox {
public:
	SpritePickerListBox(wxWindow* parent, uint8_t matchWidth, uint8_t matchHeight) :
		wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE) {
		for (int id = 100; id <= g_gui.gfx.getItemSpriteMaxID(); ++id) {
			GameSprite* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(id));
			if (sprite && sprite->width == matchWidth && sprite->height == matchHeight) {
				ids.push_back(static_cast<uint16_t>(id));
			}
		}
		SetItemCount(ids.size());
	}

	uint16_t GetClientId(int index) const {
		return (index >= 0 && index < static_cast<int>(ids.size())) ? ids[index] : 0;
	}

	int FindClientId(uint16_t clientId) const {
		for (size_t i = 0; i < ids.size(); ++i) {
			if (ids[i] == clientId) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const override {
		if (index >= ids.size()) {
			return;
		}
		if (Sprite* sprite = g_gui.gfx.getSprite(ids[index])) {
			sprite->DrawTo(&dc, SPRITE_SIZE_32x32, rect.GetX() + 4, rect.GetY() + 2, 32, 32);
		}
		dc.SetTextForeground(ThemeManager::Get().GetPalette().text);
		dc.DrawText(wxString::Format("Client ID %u", static_cast<unsigned>(ids[index])), rect.GetX() + 44, rect.GetY() + 10);
	}

	wxCoord OnMeasureItem(size_t WXUNUSED(index)) const override {
		return 36;
	}

private:
	std::vector<uint16_t> ids;
};

// Modal picker: choose another item sprite with the same tile layout.
class SpritePickerDialog : public wxDialog {
public:
	SpritePickerDialog(wxWindow* parent, uint16_t currentClientId) :
		wxDialog(parent, wxID_ANY, "Pick a Sprite", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
		GameSprite* current = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(currentClientId));
		const uint8_t matchWidth = current ? current->width : 1;
		const uint8_t matchHeight = current ? current->height : 1;

		wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
		topsizer->Add(newd wxStaticText(this, wxID_ANY, wxString::Format("Item sprites with a %ux%u tile layout:", static_cast<unsigned>(matchWidth), static_cast<unsigned>(matchHeight))), wxSizerFlags(0).Border(wxALL, 8));
		list = newd SpritePickerListBox(this, matchWidth, matchHeight);
		list->SetMinSize(FromDIP(wxSize(280, 400)));
		topsizer->Add(list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, 8));
		topsizer->Add(CreateButtonSizer(wxOK | wxCANCEL), wxSizerFlags(0).Center().Border(wxALL, 8));
		SetSizerAndFit(topsizer);
		Centre(wxBOTH);

		const int currentIndex = list->FindClientId(currentClientId);
		if (currentIndex >= 0) {
			list->SetSelection(currentIndex);
			list->ScrollToRow(std::max(0, currentIndex - 4));
		}
		list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { EndModal(wxID_OK); });
	}

	uint16_t GetPickedClientId() const {
		return list->GetClientId(list->GetSelection());
	}

private:
	SpritePickerListBox* list;
};

// Modal picker for the Tibia 8-bit color palette (the 216-color cube used by
// light colors, in the same layout the client uses).
class EightBitColorPickerDialog : public wxDialog {
public:
	EightBitColorPickerDialog(wxWindow* parent, int initialColor) :
		wxDialog(parent, wxID_ANY, "Pick a color"),
		picked_color(initialColor) {
		wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
		wxGridSizer* grid = newd wxGridSizer(18, FromDIP(1), FromDIP(1));
		for (int color = 0; color < 216; ++color) {
			wxPanel* swatch = newd wxPanel(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(20, 20)), color == initialColor ? wxBORDER_SIMPLE : wxBORDER_NONE);
			swatch->SetBackgroundColour(colorFromEightBit(color));
			swatch->SetToolTip(wxString::Format("%d", color));
			swatch->Bind(wxEVT_LEFT_DOWN, [this, color](wxMouseEvent&) {
				picked_color = color;
				EndModal(wxID_OK);
			});
			grid->Add(swatch);
		}
		topsizer->Add(grid, wxSizerFlags(0).Border(wxALL, 8));
		topsizer->Add(CreateButtonSizer(wxCANCEL), wxSizerFlags(0).Center().Border(wxBOTTOM, 8));
		SetSizerAndFit(topsizer);
		Centre(wxBOTH);
	}

	int GetPickedColor() const {
		return picked_color;
	}

private:
	int picked_color;
};

// An editable item flag: one checkbox, linked to its items.otb flag bit, its
// client .dat flag (canonical DatFlags value, -1 if the .dat has none), and
// the in-memory ItemType field it mirrors. invert_dat marks .dat flags with
// inverted meaning (NotMoveable / NotWalkable-style inversions that differ
// from the ItemType field polarity).
struct ItemFlagSpec {
	const char* label;
	uint32_t otb_flag; // itemflags_t bit, 0 if items.otb has none
	int dat_flag; // canonical DatFlags value, -1 if the .dat has none
	bool invert_dat;
	bool ItemType::*field;
};

const std::vector<ItemFlagSpec>& getItemFlagSpecs() {
	static const std::vector<ItemFlagSpec> specs = {
		{ "Unpassable", FLAG_UNPASSABLE, DatFlagNotWalkable, false, &ItemType::unpassable },
		{ "Block missiles", FLAG_BLOCK_MISSILES, DatFlagBlockProjectile, false, &ItemType::blockMissiles },
		{ "Block pathfinder", FLAG_BLOCK_PATHFINDER, DatFlagNotPathable, false, &ItemType::blockPathfinder },
		{ "Moveable", FLAG_MOVEABLE, DatFlagNotMoveable, true, &ItemType::moveable },
		{ "Pickupable", FLAG_PICKUPABLE, DatFlagPickupable, false, &ItemType::pickupable },
		{ "Stackable", FLAG_STACKABLE, DatFlagStackable, false, &ItemType::stackable },
		{ "Hangable", FLAG_HANGABLE, DatFlagHangable, false, &ItemType::isHangable },
		{ "Hook east", FLAG_HOOK_EAST, DatFlagHookEast, false, &ItemType::hookEast },
		{ "Hook south", FLAG_HOOK_SOUTH, DatFlagHookSouth, false, &ItemType::hookSouth },
		{ "Rotatable", FLAG_ROTABLE, DatFlagRotateable, false, &ItemType::rotable },
		{ "Readable", FLAG_READABLE, -1, false, &ItemType::canReadText },
		{ "Allow distant read", FLAG_ALLOWDISTREAD, -1, false, &ItemType::allowDistRead },
		{ "Ignore look", FLAG_IGNORE_LOOK, DatFlagLook, false, &ItemType::ignoreLook },
	};
	return specs;
}

const std::vector<ItemTypePreset>& getItemTypePresets() {
	static const std::vector<ItemTypePreset> presets = {
		{ "Sword", { { "weaponType", "sword" }, { "attack", "" }, { "defense", "" }, { "weight", "" } } },
		{ "Club", { { "weaponType", "club" }, { "attack", "" }, { "defense", "" }, { "weight", "" } } },
		{ "Axe", { { "weaponType", "axe" }, { "attack", "" }, { "defense", "" }, { "weight", "" } } },
		{ "Distance weapon", { { "weaponType", "distance" }, { "ammoType", "arrow" }, { "range", "6" }, { "hitChance", "" }, { "weight", "" } } },
		{ "Ammunition", { { "weaponType", "ammunition" }, { "ammoType", "arrow" }, { "shootType", "arrow" }, { "attack", "" }, { "weight", "" } } },
		{ "Wand / rod", { { "weaponType", "wand" }, { "shootType", "fire" }, { "range", "5" }, { "weight", "" } } },
		{ "Shield", { { "weaponType", "shield" }, { "defense", "" }, { "weight", "" }, { "absorbPercentAll", "" } } },
		{ "Armor (body)", { { "slotType", "body" }, { "armor", "" }, { "weight", "" }, { "absorbPercentAll", "" } } },
		{ "Helmet", { { "slotType", "head" }, { "armor", "" }, { "weight", "" }, { "absorbPercentAll", "" } } },
		{ "Legs", { { "slotType", "legs" }, { "armor", "" }, { "weight", "" }, { "absorbPercentAll", "" } } },
		{ "Boots", { { "slotType", "feet" }, { "armor", "" }, { "speed", "" }, { "weight", "" } } },
		{ "Ring", { { "slotType", "ring" }, { "charges", "" }, { "duration", "" }, { "weight", "" } } },
		{ "Necklace / amulet", { { "slotType", "necklace" }, { "charges", "" }, { "weight", "" } } },
		{ "Container", { { "containerSize", "20" }, { "weight", "" } } },
		{ "Rune", { { "charges", "1" }, { "runeSpellName", "" } } },
		{ "Readable", { { "readable", "1" }, { "maxTextLen", "" } } },
		{ "Writeable", { { "writeable", "1" }, { "maxTextLen", "512" } } },
		{ "Key", { { "type", "key" } } },
		{ "Door", { { "type", "door" } } },
		{ "Teleport", { { "type", "teleport" } } },
		{ "Bed", { { "type", "bed" }, { "partnerDirection", "" }, { "maleTransformTo", "" }, { "femaleTransformTo", "" } } },
		{ "Magic field", { { "type", "magicfield" }, { "field", "fire" }, { "decayTo", "0" }, { "duration", "" } } },
	};
	return presets;
}

// The last <item> node covering 'id' — mirrors loadFromGameXml, where a later
// node overrides an earlier one.
pugi::xml_node findItemNode(pugi::xml_node root, uint16_t id) {
	pugi::xml_node match;
	for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
		if (as_lower_str(child.name()) != "item") {
			continue;
		}
		if (const pugi::xml_attribute attribute = child.attribute("id")) {
			if (attribute.as_ushort() == id) {
				match = child;
			}
		} else if (child.attribute("fromid").as_ushort() <= id && id <= child.attribute("toid").as_ushort() && child.attribute("toid").as_ushort() != 0) {
			match = child;
		}
	}
	return match;
}

// Carves a standalone <item id="X"> node out of a fromid/toid range node,
// keeping the remaining ids covered by (up to two) range nodes around it.
// The returned node carries only the id (plus any editorsuffix) — the caller
// fills in name and attributes.
pugi::xml_node splitRangeNode(pugi::xml_node root, pugi::xml_node range, uint16_t id) {
	const uint16_t fromId = range.attribute("fromid").as_ushort();
	const uint16_t toId = range.attribute("toid").as_ushort();

	if (fromId == toId) {
		// Degenerate range: rewrite the node itself as a standalone item.
		range.remove_attribute("fromid");
		range.remove_attribute("toid");
		range.prepend_attribute("id") = id;
		return range;
	}

	pugi::xml_node item;
	if (id == fromId) {
		item = root.insert_child_before("item", range);
		range.attribute("fromid") = static_cast<unsigned>(fromId + 1);
	} else if (id == toId) {
		item = root.insert_child_after("item", range);
		range.attribute("toid") = static_cast<unsigned>(toId - 1);
	} else {
		// Middle of the range: [fromId, id-1], the item, [id+1, toId]. The
		// upper half keeps a copy of the range's definition so those ids
		// still load the same.
		range.attribute("toid") = static_cast<unsigned>(id - 1);
		item = root.insert_child_after("item", range);
		pugi::xml_node upper = root.insert_child_after("item", item);
		upper.append_attribute("fromid") = static_cast<unsigned>(id + 1);
		upper.append_attribute("toid") = static_cast<unsigned>(toId);
		for (pugi::xml_attribute attribute : range.attributes()) {
			const std::string attributeName = attribute.name();
			if (attributeName != "fromid" && attributeName != "toid") {
				upper.append_attribute(attribute.name()) = attribute.value();
			}
		}
		for (pugi::xml_node child : range.children()) {
			upper.append_copy(child);
		}
	}

	item.append_attribute("id") = static_cast<unsigned>(id);
	if (const pugi::xml_attribute suffix = range.attribute("editorsuffix")) {
		item.append_attribute("editorsuffix") = suffix.value();
	}
	return item;
}

// Copy/paste clipboards for the Edit Item dialog. Static so a copied set can
// be pasted onto another item in a later dialog; attributes and flags are
// independent and can both be carried at once.
std::vector<ItemXmlAttribute> s_attribute_clipboard;
bool s_attribute_clipboard_set = false;

struct FlagClipboard {
	std::vector<bool> flags; // one per getItemFlagSpecs() entry
	bool has_light = false;
	int light_intensity = 0;
	int light_color = 0;
};
FlagClipboard s_flag_clipboard;
bool s_flag_clipboard_set = false;

} // namespace

// ============================================================================
// Sprite preview panel (Edit Item sidebar)
// Draws the previewed client sprite at 2x (1x for big multi-tile sprites) and
// keeps redrawing while the sprite is animated so all frames cycle.

class SpritePreviewPanel : public wxPanel {
public:
	SpritePreviewPanel(wxWindow* parent) :
		wxPanel(parent, wxID_ANY),
		timer(this) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(FromDIP(wxSize(96, 96)));
		Bind(wxEVT_PAINT, &SpritePreviewPanel::OnPaint, this);
		Bind(wxEVT_TIMER, &SpritePreviewPanel::OnTimer, this);
	}

	~SpritePreviewPanel() {
		timer.Stop();
	}

	void SetClientId(uint16_t id) {
		client_id = id;
		GameSprite* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(client_id));
		const int tilesW = sprite ? std::max(1, static_cast<int>(sprite->width)) : 1;
		const int tilesH = sprite ? std::max(1, static_cast<int>(sprite->height)) : 1;
		zoom = (tilesW > 2 || tilesH > 2) ? 1 : 2;
		SetMinSize(FromDIP(wxSize(std::max(96, tilesW * 32 * zoom + 8), std::max(96, tilesH * 32 * zoom + 8))));
		if (sprite && sprite->isAnimated()) {
			timer.Start(100);
		} else {
			timer.Stop();
		}
		Refresh();
	}

private:
	void OnTimer(wxTimerEvent& WXUNUSED(event)) {
		Refresh();
	}

	void OnPaint(wxPaintEvent& WXUNUSED(event)) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(ThemeManager::Get().GetPalette().control));
		dc.Clear();
		if (g_gui.gfx.isUnloaded()) {
			return;
		}
		Sprite* sprite = g_gui.gfx.getSprite(client_id);
		if (!sprite) {
			return;
		}
		GameSprite* game = dynamic_cast<GameSprite*>(sprite);
		const int tilesW = game ? std::max(1, static_cast<int>(game->width)) : 1;
		const int tilesH = game ? std::max(1, static_cast<int>(game->height)) : 1;
		const int drawW = tilesW * 32 * zoom;
		const int drawH = tilesH * 32 * zoom;
		const wxSize client = GetClientSize();
		const int x = (client.x - drawW) / 2;
		const int y = (client.y - drawH) / 2;
		const int frame = game ? game->getCurrentFrame() : 0;

		wxBitmap* bitmap = sprite->getBitmap(SPRITE_SIZE_ACTUAL, frame);
		if (!bitmap || !bitmap->IsOk()) {
			sprite->DrawTo(&dc, SPRITE_SIZE_ACTUAL, x, y, drawW, drawH, frame);
			return;
		}
		if (zoom == 1) {
			dc.DrawBitmap(*bitmap, x, y, true);
			return;
		}
		wxImage image = bitmap->ConvertToImage();
		image.Rescale(image.GetWidth() * zoom, image.GetHeight() * zoom, wxIMAGE_QUALITY_NEAREST);
		dc.DrawBitmap(wxBitmap(image), x, y, true);
	}

	uint16_t client_id = 0;
	int zoom = 2;
	wxTimer timer;
};

bool LoadItemXmlDefinition(const std::string& file, uint16_t id, ItemXmlDefinition& out, wxString& error) {
	pugi::xml_document doc;
	if (!doc.load_file(file.c_str(), pugi::parse_full)) {
		error = wxString::Format("Could not open '%s'.", wxstr(file));
		return false;
	}

	pugi::xml_node root = doc.child("items");
	if (!root) {
		error = wxString::Format("Invalid items file '%s' (missing <items> root).", wxstr(file));
		return false;
	}

	pugi::xml_node node = findItemNode(root, id);
	if (!node) {
		return true;
	}

	out.name = node.attribute("name").as_string();
	for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
		if (as_lower_str(child.name()) != "attribute") {
			continue;
		}
		if (const pugi::xml_attribute key = child.attribute("key")) {
			out.attributes.push_back({ key.as_string(), child.attribute("value").as_string() });
		}
	}
	return true;
}

bool SaveItemXmlDefinition(const std::string& file, uint16_t id, const ItemXmlDefinition& def, wxString& error) {
	pugi::xml_document doc;
	if (!doc.load_file(file.c_str(), pugi::parse_full)) {
		error = wxString::Format("Could not open '%s'.", wxstr(file));
		return false;
	}

	pugi::xml_node root = doc.child("items");
	if (!root) {
		error = wxString::Format("Invalid items file '%s' (missing <items> root).", wxstr(file));
		return false;
	}

	pugi::xml_node node = findItemNode(root, id);
	if (!node) {
		// New definition — insert it in id order.
		pugi::xml_node before;
		for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
			if (as_lower_str(child.name()) != "item") {
				continue;
			}
			uint16_t startId = child.attribute("id").as_ushort();
			if (startId == 0) {
				startId = child.attribute("fromid").as_ushort();
			}
			if (startId > id) {
				before = child;
				break;
			}
		}
		node = before ? root.insert_child_before("item", before) : root.append_child("item");
		node.append_attribute("id") = static_cast<unsigned>(id);
	} else if (node.attribute("fromid")) {
		node = splitRangeNode(root, node, id);
	}

	pugi::xml_attribute nameAttribute = node.attribute("name");
	if (!def.name.empty()) {
		if (!nameAttribute) {
			nameAttribute = node.append_attribute("name");
		}
		nameAttribute = def.name.c_str();
	} else if (nameAttribute) {
		node.remove_attribute(nameAttribute);
	}

	// Merge the attribute list instead of rebuilding it, so XML the dialog
	// does not model survives a save untouched: nested <attribute> children
	// (field damage blocks) and value-less forms (minvalue/maxvalue).
	std::vector<ItemXmlAttribute> pending;
	for (const ItemXmlAttribute& attribute : def.attributes) {
		if (!attribute.key.empty()) {
			pending.push_back(attribute);
		}
	}

	for (pugi::xml_node child = node.first_child(); child;) {
		pugi::xml_node next = child.next_sibling();
		if (as_lower_str(child.name()) == "attribute") {
			const std::string key = as_lower_str(child.attribute("key").as_string());
			auto match = pending.end();
			for (auto it = pending.begin(); it != pending.end(); ++it) {
				if (as_lower_str(it->key) == key) {
					match = it;
					break;
				}
			}
			if (match == pending.end()) {
				// The row was removed in the dialog.
				node.remove_child(child);
			} else {
				if (!match->value.empty()) {
					pugi::xml_attribute value = child.attribute("value");
					if (!value) {
						value = child.append_attribute("value");
					}
					value = match->value.c_str();
				} else if (child.attribute("value")) {
					// Cleared in the dialog. Keep the node when it still
					// carries data the grid cannot show; drop it otherwise.
					bool hasOtherData = static_cast<bool>(child.first_child());
					for (pugi::xml_attribute a = child.first_attribute(); a && !hasOtherData; a = a.next_attribute()) {
						const std::string attributeName = as_lower_str(a.name());
						if (attributeName != "key" && attributeName != "value") {
							hasOtherData = true;
						}
					}
					if (hasOtherData) {
						child.remove_attribute("value");
					} else {
						node.remove_child(child);
					}
				}
				pending.erase(match);
			}
		}
		child = next;
	}

	// Rows without an existing node: append them (a new row left blank is
	// not saved).
	for (const ItemXmlAttribute& attribute : pending) {
		if (attribute.value.empty()) {
			continue;
		}
		pugi::xml_node attributeNode = node.append_child("attribute");
		attributeNode.append_attribute("key") = attribute.key.c_str();
		attributeNode.append_attribute("value") = attribute.value.c_str();
	}

	if (!doc.save_file(file.c_str())) {
		error = wxString::Format("Could not save '%s'.", wxstr(file));
		return false;
	}

	// Best-effort refresh of the in-memory item type so the editor shows the
	// change immediately. (A removed attribute only fully resets after the
	// version is reloaded — loadItemFromGameXml never clears fields.)
	if (g_items.typeExists(id)) {
		ItemType& type = g_items.getItemType(id);
		type.name = def.name;
		type.shader.clear(); // so a removed shader attribute stops rendering
		g_items.loadItemFromGameXml(node, id);
	}
	return true;
}

// Deep-copies the source item's items.xml node under a new id, preserving
// everything the editor does not model (nested attributes, minvalue/maxvalue
// forms, article, ...). 'found' reports whether the source had a node.
static bool CopyItemXmlNode(const std::string& file, uint16_t sourceId, uint16_t newId, bool& found, wxString& error) {
	found = false;
	pugi::xml_document doc;
	if (!doc.load_file(file.c_str(), pugi::parse_full)) {
		error = wxString::Format("Could not open '%s'.", wxstr(file));
		return false;
	}
	pugi::xml_node root = doc.child("items");
	if (!root) {
		error = wxString::Format("Invalid items file '%s' (missing <items> root).", wxstr(file));
		return false;
	}
	pugi::xml_node sourceNode = findItemNode(root, sourceId);
	if (!sourceNode) {
		return true; // nothing to copy - the caller creates a minimal node
	}

	pugi::xml_node before;
	for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
		if (as_lower_str(child.name()) != "item") {
			continue;
		}
		uint16_t startId = child.attribute("id").as_ushort();
		if (startId == 0) {
			startId = child.attribute("fromid").as_ushort();
		}
		if (startId > newId) {
			before = child;
			break;
		}
	}
	pugi::xml_node copy = before ? root.insert_copy_before(sourceNode, before) : root.append_copy(sourceNode);
	copy.remove_attribute("fromid");
	copy.remove_attribute("toid");
	pugi::xml_attribute idAttribute = copy.attribute("id");
	if (!idAttribute) {
		idAttribute = copy.prepend_attribute("id");
	}
	idAttribute = static_cast<unsigned>(newId);

	if (!doc.save_file(file.c_str())) {
		error = wxString::Format("Could not save '%s'.", wxstr(file));
		return false;
	}
	if (g_items.typeExists(newId)) {
		g_items.loadItemFromGameXml(copy, newId);
	}
	found = true;
	return true;
}

uint16_t DuplicateItemType(uint16_t sourceServerId, wxString& error) {
	const ItemType& source = g_items.getItemType(sourceServerId);
	if (source.id == 0) {
		error = "That item does not exist in the item database.";
		return 0;
	}

	FileName items_path = g_gui.GetCurrentVersion().getItemsPath();
	const wxString items_dir = items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	const wxString xml_path = items_dir + "items.xml";
	const wxString otb_path = items_dir + "items.otb";
	const std::string xmlFile = nstr(xml_path);
	const std::string otbFile = nstr(otb_path);

	const uint16_t newServerId = g_items.getMaxID() + 1;

	// 1) Client side first: new sprite ids in the .spr and a cloned .dat
	// entry give the duplicate its own client id.
	uint16_t newClientId = 0;
	if (!g_gui.gfx.duplicateItemSprite(source.clientID, newClientId, error)) {
		return 0;
	}

	// 2) Server side: cloned items.otb node under the new ids.
	if (!g_items.duplicateOtbItem(otbFile, sourceServerId, newServerId, newClientId, error)) {
		error = "The .spr/.dat were updated, but items.otb failed:\n" + error;
		return 0;
	}

	// 3) In-memory registration (before the XML save so it can re-apply the
	// node to the new type).
	g_items.duplicateType(sourceServerId, newServerId, newClientId);

	// 4) items.xml: deep-copy the source node under the new id (nested
	// attributes and value-less forms included).
	bool copied = false;
	if (!CopyItemXmlNode(xmlFile, sourceServerId, newServerId, copied, error)) {
		error = "The .spr/.dat/items.otb were updated, but items.xml failed:\n" + error;
		return 0;
	}
	if (!copied) {
		// The source had no items.xml node - create a minimal one so the
		// duplicate at least carries a name.
		ItemXmlDefinition def;
		def.name = source.name;
		if (!SaveItemXmlDefinition(xmlFile, newServerId, def, error)) {
			error = "The .spr/.dat/items.otb were updated, but items.xml failed:\n" + error;
			return 0;
		}
	}
	return newServerId;
}

// ============================================================================
// Edit Item window

EditItemTypeWindow::EditItemTypeWindow(wxWindow* win_parent, uint16_t itemId, wxPoint pos) :
	ObjectPropertiesWindowBase(win_parent, "Edit Item", pos),
	item_id(itemId),
	name_field(nullptr),
	attributes_grid(nullptr),
	sprite_preview(nullptr),
	sprite_info_label(nullptr) {

	// Same path construction as GUI::LoadDataFiles uses to load items.xml.
	FileName items_path = g_gui.GetCurrentVersion().getItemsPath();
	const wxString items_xml_path = items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "items.xml";
	items_xml_file = nstr(items_xml_path);
	const wxString items_otb_path = items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "items.otb";
	items_otb_file = nstr(items_otb_path);

	const ItemType& it = g_items.getItemType(item_id);

	ItemXmlDefinition def;
	wxString loadError;
	LoadItemXmlDefinition(items_xml_file, item_id, def, loadError);
	if (def.name.empty()) {
		def.name = it.name;
	}

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxSizer* leftsizer = newd wxBoxSizer(wxVERTICAL); // main column; the sprite sidebar sits to its right
	wxStaticBoxSizer* boxsizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Edit item type");

	// Item preview: clickable sprite (opens the same-size sprite picker), ids,
	// and rotate/flip buttons that edit the .spr pixels directly.
	orig_client_id = it.clientID;
	display_client_id = it.clientID;
	wxSizer* itemsizer = newd wxBoxSizer(wxHORIZONTAL);
	sprite_button = newd DCButton(this, wxID_ANY, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_32x32, 0);
	sprite_button->SetSprite(it.clientID);
	sprite_button->SetToolTip("Click to pick a different sprite of the same size, or drop a .png here to replace its pixels");
	sprite_button->SetDropTarget(newd SpriteImageDropTarget(this));
	itemsizer->Add(sprite_button, wxSizerFlags(0).CenterVertical().Border(wxRIGHT, 8));
	item_label = newd wxStaticText(this, wxID_ANY, "");
	itemsizer->Add(item_label, wxSizerFlags(1).CenterVertical());
	UpdateItemLabel();
	wxButton* rotate_button = newd wxButton(this, wxID_ANY, wxString::FromUTF8("Rotate 90\xC2\xB0"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	rotate_button->SetToolTip("Rotate the sprite's pixels 90 degrees clockwise (writes to the .spr file immediately)");
	wxButton* fliph_button = newd wxButton(this, wxID_ANY, "Flip H", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	fliph_button->SetToolTip("Mirror the sprite's pixels horizontally (writes to the .spr file immediately)");
	wxButton* flipv_button = newd wxButton(this, wxID_ANY, "Flip V", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	flipv_button->SetToolTip("Mirror the sprite's pixels vertically (writes to the .spr file immediately)");
	wxButton* import_button = newd wxButton(this, wxID_ANY, "Import...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	import_button->SetToolTip("Replace the sprite's pixels with an image file (writes to the .spr file immediately)");
	wxButton* export_button = newd wxButton(this, wxID_ANY, "Export...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	export_button->SetToolTip("Save the full sprite sheet (all frames, patterns and layers) as a PNG - edit it and import it back");
	itemsizer->Add(rotate_button, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	itemsizer->Add(fliph_button, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	itemsizer->Add(flipv_button, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	itemsizer->Add(import_button, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	itemsizer->Add(export_button, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	boxsizer->Add(itemsizer, wxSizerFlags(0).Expand().Border(wxALL, 6));

	wxFlexGridSizer* namesizer = newd wxFlexGridSizer(2, 8, 8);
	namesizer->AddGrowableCol(1);
	namesizer->Add(newd wxStaticText(this, wxID_ANY, "Name"), wxSizerFlags(0).CenterVertical());
	name_field = newd wxTextCtrl(this, wxID_ANY, wxstr(def.name));
	namesizer->Add(name_field, wxSizerFlags(1).Expand());

	namesizer->Add(newd wxStaticText(this, wxID_ANY, "Preset"), wxSizerFlags(0).CenterVertical());
	preset_field = newd wxChoice(this, wxID_ANY);
	preset_field->Append("(choose a preset to add its attributes)");
	for (const ItemTypePreset& preset : getItemTypePresets()) {
		preset_field->Append(wxString(preset.name));
	}
	preset_field->SetSelection(0);
	namesizer->Add(preset_field, wxSizerFlags(1).Expand());

	boxsizer->Add(namesizer, wxSizerFlags(0).Expand().Border(wxALL, 6));

	boxsizer->Add(newd wxStaticText(this, wxID_ANY, "Attributes"), wxSizerFlags(0).Border(wxLEFT | wxTOP, 6));

	attributes_grid = newd wxGrid(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(420, 200)));
	attributes_grid->CreateGrid(0, 2);
	attributes_grid->DisableDragRowSize();
	attributes_grid->DisableDragColSize();
	attributes_grid->SetSelectionMode(wxGrid::wxGridSelectRows);
	attributes_grid->SetRowLabelSize(0);
	attributes_grid->EnableEditing(true);
	attributes_grid->SetColLabelValue(0, "Key");
	attributes_grid->SetColSize(0, FromDIP(140));
	attributes_grid->SetColLabelValue(1, "Value");
	attributes_grid->SetColSize(1, FromDIP(260));
	key_choices = buildKeySuggestions(detectEquipKind(def.attributes));
	for (const ItemXmlAttribute& attribute : def.attributes) {
		AppendAttributeRow(attribute.key, attribute.value);
	}
	UpdateKeySuggestions(); // also gives shader rows their value dropdown
	boxsizer->Add(attributes_grid, wxSizerFlags(1).Expand().Border(wxALL, 6));

	wxSizer* attrbuttonsizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* add_button = newd wxButton(this, wxID_ANY, "Add");
	wxButton* remove_button = newd wxButton(this, wxID_ANY, "Remove");
	attrbuttonsizer->Add(add_button, wxSizerFlags(0).Border(wxRIGHT, 6));
	attrbuttonsizer->Add(remove_button, wxSizerFlags(0));
	// Attribute clipboard: copy this list, paste a copied list over it.
	wxButton* copy_attributes_button = newd wxButton(this, wxID_ANY, "Copy");
	copy_attributes_button->SetToolTip("Copy all attribute rows to the attribute clipboard");
	paste_attributes_button = newd wxButton(this, wxID_ANY, "Paste");
	paste_attributes_button->SetToolTip("Replace all attribute rows with the copied ones");
	paste_attributes_button->Enable(s_attribute_clipboard_set);
	attrbuttonsizer->Add(copy_attributes_button, wxSizerFlags(0).Border(wxLEFT, 18));
	attrbuttonsizer->Add(paste_attributes_button, wxSizerFlags(0).Border(wxLEFT, 6));
	boxsizer->Add(attrbuttonsizer, wxSizerFlags(0).Border(wxLEFT | wxBOTTOM, 6));

	leftsizer->Add(boxsizer, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 12));

	// Item flags — separate from the attributes: these live in items.otb and
	// the client .dat, not in items.xml.
	wxStaticBoxSizer* flagsizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Flags (items.otb + client .dat)");
	wxGridSizer* flaggrid = newd wxGridSizer(3, FromDIP(2), FromDIP(8));
	const std::vector<ItemFlagSpec>& flagSpecs = getItemFlagSpecs();
	flag_boxes.resize(flagSpecs.size());
	original_flags.resize(flagSpecs.size());
	for (size_t i = 0; i < flagSpecs.size(); ++i) {
		flag_boxes[i] = newd wxCheckBox(this, wxID_ANY, flagSpecs[i].label);
		original_flags[i] = it.*(flagSpecs[i].field);
		flag_boxes[i]->SetValue(original_flags[i]);
		flaggrid->Add(flag_boxes[i]);
	}
	flagsizer->Add(flaggrid, wxSizerFlags(0).Expand().Border(wxALL, 6));

	// Light — intensity plus a Tibia 8-bit palette color.
	GameSprite* sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(it.clientID));
	orig_has_light = sprite && sprite->has_light;
	orig_light_intensity = orig_has_light ? sprite->light.intensity : 0;
	orig_light_color = orig_has_light ? sprite->light.color : 215;
	light_color = orig_light_color;

	wxSizer* lightsizer = newd wxBoxSizer(wxHORIZONTAL);
	light_check = newd wxCheckBox(this, wxID_ANY, "Light");
	light_check->SetValue(orig_has_light);
	lightsizer->Add(light_check, wxSizerFlags(0).CenterVertical());
	lightsizer->Add(newd wxStaticText(this, wxID_ANY, "Intensity"), wxSizerFlags(0).CenterVertical().Border(wxLEFT, 12));
	light_intensity_field = newd wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(64, -1)), wxSP_ARROW_KEYS, 0, 255, orig_light_intensity);
	lightsizer->Add(light_intensity_field, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	lightsizer->Add(newd wxStaticText(this, wxID_ANY, "Color"), wxSizerFlags(0).CenterVertical().Border(wxLEFT, 12));
	light_color_swatch = newd wxPanel(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(24, 20)), wxBORDER_SIMPLE);
	light_color_swatch->SetBackgroundColour(colorFromEightBit(light_color));
	light_color_swatch->SetToolTip(wxString::Format("%d", light_color));
	lightsizer->Add(light_color_swatch, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	wxButton* pick_color_button = newd wxButton(this, wxID_ANY, "Pick...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	lightsizer->Add(pick_color_button, wxSizerFlags(0).CenterVertical().Border(wxLEFT, 4));
	flagsizer->Add(lightsizer, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	// Flag clipboard (flags + light), independent of the attribute clipboard.
	wxSizer* flagbuttonsizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* copy_flags_button = newd wxButton(this, wxID_ANY, "Copy Flags");
	copy_flags_button->SetToolTip("Copy the flags and light settings to the flag clipboard");
	paste_flags_button = newd wxButton(this, wxID_ANY, "Paste Flags");
	paste_flags_button->SetToolTip("Replace the flags and light settings with the copied ones");
	paste_flags_button->Enable(s_flag_clipboard_set);
	flagbuttonsizer->Add(copy_flags_button, wxSizerFlags(0));
	flagbuttonsizer->Add(paste_flags_button, wxSizerFlags(0).Border(wxLEFT, 6));
	flagsizer->Add(flagbuttonsizer, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	leftsizer->Add(flagsizer, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 12));

	leftsizer->Add(newd wxStaticText(this, wxID_ANY, "Saving writes name/attributes to items.xml and flag/light/sprite changes to\nitems.otb and the client .dat (originals kept as .bak). Rotate/flip edit the\n.spr immediately. New rows left blank are not saved; existing attributes with\nextra data (min/max values, nested entries) are preserved."), wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxTOP, 12));

	// Right sidebar: live sprite preview and its structure.
	wxStaticBoxSizer* sidebar = newd wxStaticBoxSizer(wxVERTICAL, this, "Sprite");
	sprite_preview = newd SpritePreviewPanel(this);
	sidebar->Add(sprite_preview, wxSizerFlags(0).CenterHorizontal().Border(wxALL, 6));
	sprite_info_label = newd wxStaticText(this, wxID_ANY, "");
	sidebar->Add(sprite_info_label, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));

	// Structure editing: rewrites the .dat geometry (and adds sprites to the
	// .spr) immediately, like the rotate/flip/import buttons.
	sidebar->Add(newd wxStaticText(this, wxID_ANY, "Structure"), wxSizerFlags(0).Border(wxLEFT | wxTOP, 6));
	wxFlexGridSizer* structuregrid = newd wxFlexGridSizer(2, FromDIP(4), FromDIP(6));
	const char* const structureLabels[7] = { "Tiles wide", "Tiles high", "Layers", "Patterns X", "Patterns Y", "Patterns Z", "Frames" };
	const int structureMax[7] = { 8, 8, 4, 32, 32, 8, 64 };
	for (int i = 0; i < 7; ++i) {
		structuregrid->Add(newd wxStaticText(this, wxID_ANY, structureLabels[i]), wxSizerFlags(0).CenterVertical());
		structure_fields[i] = newd wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(64, -1)), wxSP_ARROW_KEYS, 1, structureMax[i], 1);
		structuregrid->Add(structure_fields[i]);
	}
	sidebar->Add(structuregrid, wxSizerFlags(0).Border(wxALL, 6));
	wxButton* apply_structure_button = newd wxButton(this, wxID_ANY, "Apply Structure");
	apply_structure_button->SetToolTip("Change tiles/layers/patterns/frames: rewrites the .dat entry and adds sprites to the .spr immediately. New frames and patterns copy the nearest existing ones; new layers and tiles start empty.");
	sidebar->Add(apply_structure_button, wxSizerFlags(0).Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));
	apply_structure_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickApplyStructure, this);
	sidebar->SetMinSize(FromDIP(wxSize(230, -1)));
	UpdateSpriteSidebar();

	wxSizer* columns = newd wxBoxSizer(wxHORIZONTAL);
	columns->Add(leftsizer, wxSizerFlags(1).Expand());
	columns->Add(sidebar, wxSizerFlags(0).Expand().Border(wxLEFT | wxRIGHT | wxTOP, 12));
	topsizer->Add(columns, wxSizerFlags(1).Expand());

	wxSizer* buttonsizer = newd wxBoxSizer(wxHORIZONTAL);
	buttonsizer->Add(newd wxButton(this, wxID_OK, "Save"), wxSizerFlags(1).Center().Border(wxALL, 6));
	buttonsizer->Add(newd wxButton(this, wxID_CANCEL, "Cancel"), wxSizerFlags(1).Center().Border(wxALL, 6));
	topsizer->Add(buttonsizer, wxSizerFlags(0).Center().Border(wxALL, 8));

	SetSizerAndFit(topsizer);
	Centre(wxBOTH);

	preset_field->Bind(wxEVT_CHOICE, &EditItemTypeWindow::OnSelectPreset, this);
	attributes_grid->Bind(wxEVT_GRID_CELL_CHANGED, &EditItemTypeWindow::OnGridCellChanged, this);
	pick_color_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickPickLightColor, this);
	sprite_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickSprite, this);
	rotate_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickRotateSprite, this);
	fliph_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickFlipSpriteHorizontal, this);
	flipv_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickFlipSpriteVertical, this);
	import_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickImportSprite, this);
	export_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickExportSprite, this);
	add_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickAddAttribute, this);
	remove_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickRemoveAttribute, this);
	copy_attributes_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickCopyAttributes, this);
	paste_attributes_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickPasteAttributes, this);
	copy_flags_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickCopyFlags, this);
	paste_flags_button->Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickPasteFlags, this);
	Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickOK, this, wxID_OK);
	Bind(wxEVT_BUTTON, &EditItemTypeWindow::OnClickCancel, this, wxID_CANCEL);
}

void EditItemTypeWindow::AppendAttributeRow(const std::string& key, const std::string& value) {
	const int row = attributes_grid->GetNumberRows();
	attributes_grid->AppendRows(1);
	attributes_grid->SetCellEditor(row, 0, newd wxGridCellChoiceEditor(key_choices.size(), key_choices.data(), true));
	attributes_grid->SetCellValue(row, 0, wxstr(key));
	attributes_grid->SetCellValue(row, 1, wxstr(value));
}

void EditItemTypeWindow::UpdateKeySuggestions() {
	std::vector<ItemXmlAttribute> attributes;
	for (int row = 0; row < attributes_grid->GetNumberRows(); ++row) {
		attributes.push_back({ nstr(attributes_grid->GetCellValue(row, 0).Trim(true).Trim(false)),
							   nstr(attributes_grid->GetCellValue(row, 1).Trim(true).Trim(false)) });
	}
	key_choices = buildKeySuggestions(detectEquipKind(attributes));

	// Value suggestions per key: a "shader" row offers the shaders discovered
	// from the client data (free text stays allowed).
	std::vector<wxString> shader_choices;
	for (const std::string& shaderName : g_itemShaders.getShaderNames()) {
		shader_choices.push_back(wxstr(shaderName));
	}

	for (int row = 0; row < attributes_grid->GetNumberRows(); ++row) {
		attributes_grid->SetCellEditor(row, 0, newd wxGridCellChoiceEditor(key_choices.size(), key_choices.data(), true));
		const bool isShaderRow = attributes_grid->GetCellValue(row, 0).Trim(true).Trim(false).CmpNoCase("shader") == 0;
		if (isShaderRow && !shader_choices.empty()) {
			attributes_grid->SetCellEditor(row, 1, newd wxGridCellChoiceEditor(shader_choices.size(), shader_choices.data(), true));
		} else {
			attributes_grid->SetCellEditor(row, 1, newd wxGridCellTextEditor());
		}
	}
}

void EditItemTypeWindow::OnGridCellChanged(wxGridEvent& event) {
	UpdateKeySuggestions();
	event.Skip();
}

int EditItemTypeWindow::FindAttributeRow(const wxString& key) const {
	for (int row = 0; row < attributes_grid->GetNumberRows(); ++row) {
		if (attributes_grid->GetCellValue(row, 0).Trim(true).Trim(false).CmpNoCase(key) == 0) {
			return row;
		}
	}
	return -1;
}

void EditItemTypeWindow::OnSelectPreset(wxCommandEvent& WXUNUSED(event)) {
	const int selection = preset_field->GetSelection();
	if (selection <= 0) {
		return;
	}

	attributes_grid->SaveEditControlValue();
	attributes_grid->HideCellEditControl();

	// Merge: only add keys the item does not already have, so applying a
	// preset never clobbers existing values.
	const ItemTypePreset& preset = getItemTypePresets()[selection - 1];
	for (const PresetAttribute& attribute : preset.attributes) {
		if (FindAttributeRow(attribute.key) == -1) {
			AppendAttributeRow(attribute.key, attribute.value);
		}
	}
	UpdateKeySuggestions();
}

void EditItemTypeWindow::OnClickSprite(wxCommandEvent& WXUNUSED(event)) {
	SpritePickerDialog picker(this, display_client_id);
	if (picker.ShowModal() != wxID_OK) {
		return;
	}
	const uint16_t picked = picker.GetPickedClientId();
	if (picked == 0 || picked == display_client_id) {
		return;
	}
	display_client_id = picked;
	sprite_button->SetSprite(picked);
	sprite_button->Refresh();
	UpdateItemLabel();
}

void EditItemTypeWindow::ApplySpriteTransform(int transform) {
	wxString error;
	if (!g_gui.gfx.transformSpriteImages(display_client_id, static_cast<GraphicManager::SpriteTransform>(transform), error)) {
		g_gui.PopupDialog("Error", error, wxOK);
		return;
	}
	sprite_button->Refresh();
	sprite_preview->Refresh();
	g_gui.RefreshView();
}

void EditItemTypeWindow::OnClickRotateSprite(wxCommandEvent& WXUNUSED(event)) {
	ApplySpriteTransform(GraphicManager::SPRITE_TRANSFORM_ROTATE_90_CW);
}

void EditItemTypeWindow::OnClickFlipSpriteHorizontal(wxCommandEvent& WXUNUSED(event)) {
	ApplySpriteTransform(GraphicManager::SPRITE_TRANSFORM_FLIP_HORIZONTAL);
}

void EditItemTypeWindow::OnClickFlipSpriteVertical(wxCommandEvent& WXUNUSED(event)) {
	ApplySpriteTransform(GraphicManager::SPRITE_TRANSFORM_FLIP_VERTICAL);
}

void EditItemTypeWindow::ImportSpriteFromFile(const wxString& path) {
	wxImage image;
	if (!image.LoadFile(path)) {
		g_gui.PopupDialog("Error", wxString::Format("Could not load image '%s'.", path), wxOK);
		return;
	}
	wxString error;
	if (!g_gui.gfx.importSpriteImage(display_client_id, image, error)) {
		g_gui.PopupDialog("Error", error, wxOK);
		return;
	}
	sprite_button->Refresh();
	sprite_preview->Refresh();
	g_gui.RefreshView();
}

void EditItemTypeWindow::UpdateItemLabel() {
	wxString label = wxString::Format("Server ID %u  (client ID %u)", static_cast<unsigned>(item_id), static_cast<unsigned>(display_client_id));
	if (display_client_id != orig_client_id) {
		label += "  [changed - save to apply]";
	}
	item_label->SetLabel(label);
	UpdateSpriteSidebar();
	Layout();
}

void EditItemTypeWindow::UpdateSpriteSidebar() {
	if (!sprite_preview || !sprite_info_label) {
		return; // sidebar not built yet
	}
	sprite_preview->SetClientId(display_client_id);
	GraphicManager::SpriteStructure structure;
	if (structure_fields[0] && g_gui.gfx.getSpriteStructure(display_client_id, structure)) {
		const int values[7] = { structure.width, structure.height, structure.layers, structure.pattern_x, structure.pattern_y, structure.pattern_z, structure.frames };
		for (int i = 0; i < 7; ++i) {
			structure_fields[i]->SetValue(values[i]);
		}
	}

	wxString info;
	GameSprite* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(display_client_id));
	GraphicManager::SpriteSheetLayout layout;
	if (!sprite || !g_gui.gfx.getSpriteSheetLayout(display_client_id, layout)) {
		info = wxString::Format("Client ID: %u\n(no sprite loaded)", static_cast<unsigned>(display_client_id));
	} else {
		info << wxString::Format("Client ID: %u\n", static_cast<unsigned>(display_client_id));
		info << wxString::Format("Tiles: %ux%u (%dx%d px)\n", static_cast<unsigned>(sprite->width), static_cast<unsigned>(sprite->height), layout.tile_w, layout.tile_h);
		info << wxString::Format("Layers: %u\n", static_cast<unsigned>(sprite->layers));
		info << wxString::Format("Frames: %u\n", static_cast<unsigned>(sprite->frames));
		info << wxString::Format("Patterns: %u x %u x %u\n", static_cast<unsigned>(sprite->pattern_x), static_cast<unsigned>(sprite->pattern_y), static_cast<unsigned>(sprite->pattern_z));
		info << wxString::Format("Sheet: %dx%d px\n", layout.width_px(), layout.height_px());

		if (sprite->animator && sprite->frames > 1) {
			int minDuration = -1;
			int maxDuration = -1;
			for (int i = 0; i < sprite->frames; ++i) {
				if (FrameDuration* duration = sprite->animator->getFrameDuration(i)) {
					minDuration = (minDuration < 0) ? duration->min : std::min(minDuration, duration->min);
					maxDuration = std::max(maxDuration, duration->max);
				}
			}
			info << wxString::Format("Animation: %d-%d ms/frame\n  start %d, loop %d%s\n", minDuration, maxDuration, sprite->animator->getStartFrame(), sprite->animator->getLoopCount(), sprite->animator->isAsync() ? ", async" : "");
		} else {
			info << "Animation: none\n";
		}

		// Sub-sprite ids (unique, in layout order), abbreviated.
		std::vector<uint32_t> ids;
		for (auto* image : sprite->spriteList) {
			if (!image || image->id == 0) {
				continue;
			}
			if (std::find(ids.begin(), ids.end(), image->id) == ids.end()) {
				ids.push_back(image->id);
			}
		}
		info << wxString::Format("Sub-sprites: %u (%u unique)\n", static_cast<unsigned>(sprite->spriteList.size()), static_cast<unsigned>(ids.size()));
		wxString idList;
		const size_t shown = std::min<size_t>(ids.size(), 12);
		for (size_t i = 0; i < shown; ++i) {
			if (i > 0) {
				idList << (i % 6 == 0 ? ",\n  " : ", ");
			}
			idList << ids[i];
		}
		if (ids.size() > shown) {
			idList << ", ...";
		}
		info << "Sprite IDs: " << idList << "\n";

		info << wxString::Format("Draw offset: %d, %d\n", static_cast<int>(sprite->drawoffset_x), static_cast<int>(sprite->drawoffset_y));
		info << wxString::Format("Elevation: %d\n", static_cast<int>(sprite->draw_height));
		info << wxString::Format("Minimap color: %d\n", static_cast<int>(sprite->minimap_color));
		if (sprite->has_light) {
			info << wxString::Format("Light: %u, color %u", static_cast<unsigned>(sprite->light.intensity), static_cast<unsigned>(sprite->light.color));
		} else {
			info << "Light: none";
		}
	}
	sprite_info_label->SetLabel(info);
	Layout();
}

void EditItemTypeWindow::OnClickExportSprite(wxCommandEvent& WXUNUSED(event)) {
	wxImage sheet;
	wxString error;
	if (!g_gui.gfx.exportSpriteSheet(display_client_id, sheet, error)) {
		g_gui.PopupDialog("Error", error, wxOK);
		return;
	}
	wxFileDialog dialog(this, wxString::Format("Export Sprite Sheet (%dx%d)", sheet.GetWidth(), sheet.GetHeight()), "", wxString::Format("sprite_%u.png", static_cast<unsigned>(display_client_id)), "PNG files (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	if (!sheet.SaveFile(dialog.GetPath(), wxBITMAP_TYPE_PNG)) {
		g_gui.PopupDialog("Error", wxString::Format("Could not write '%s'.", dialog.GetPath()), wxOK);
	}
}

void EditItemTypeWindow::OnClickImportSprite(wxCommandEvent& WXUNUSED(event)) {
	wxString title = "Import Sprite Image";
	GraphicManager::SpriteSheetLayout layout;
	if (g_gui.gfx.getSpriteSheetLayout(display_client_id, layout)) {
		if (layout.width_px() != layout.tile_w || layout.height_px() != layout.tile_h) {
			title += wxString::Format(" (%dx%d full sheet, or %dx%d single)", layout.width_px(), layout.height_px(), layout.tile_w, layout.tile_h);
		} else {
			title += wxString::Format(" (%dx%d)", layout.tile_w, layout.tile_h);
		}
	}
	wxFileDialog dialog(this, title, "", "", "Image files (*.png;*.jpg;*.tga)|*.png;*.jpg;*.jpeg;*.tga|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK) {
		ImportSpriteFromFile(dialog.GetPath());
	}
}

void EditItemTypeWindow::OnClickApplyStructure(wxCommandEvent& WXUNUSED(event)) {
	GraphicManager::SpriteStructure target;
	target.width = structure_fields[0]->GetValue();
	target.height = structure_fields[1]->GetValue();
	target.layers = structure_fields[2]->GetValue();
	target.pattern_x = structure_fields[3]->GetValue();
	target.pattern_y = structure_fields[4]->GetValue();
	target.pattern_z = structure_fields[5]->GetValue();
	target.frames = structure_fields[6]->GetValue();

	wxString error;
	if (!g_gui.gfx.restructureItemSprite(display_client_id, target, error)) {
		g_gui.PopupDialog("Error", error, wxOK);
		UpdateSpriteSidebar(); // snap the spinners back to the real structure
		return;
	}
	sprite_button->Refresh();
	UpdateSpriteSidebar();
	g_gui.RefreshView();
}

void EditItemTypeWindow::OnClickPickLightColor(wxCommandEvent& WXUNUSED(event)) {
	EightBitColorPickerDialog picker(this, light_color);
	if (picker.ShowModal() == wxID_OK) {
		light_color = picker.GetPickedColor();
		light_color_swatch->SetBackgroundColour(colorFromEightBit(light_color));
		light_color_swatch->SetToolTip(wxString::Format("%d", light_color));
		light_color_swatch->Refresh();
		light_check->SetValue(true); // picking a color implies wanting light
	}
}

void EditItemTypeWindow::OnClickCopyAttributes(wxCommandEvent& WXUNUSED(event)) {
	attributes_grid->SaveEditControlValue();
	attributes_grid->HideCellEditControl();

	s_attribute_clipboard.clear();
	for (int row = 0; row < attributes_grid->GetNumberRows(); ++row) {
		const std::string key = nstr(attributes_grid->GetCellValue(row, 0).Trim(true).Trim(false));
		if (key.empty()) {
			continue;
		}
		s_attribute_clipboard.push_back({ key, nstr(attributes_grid->GetCellValue(row, 1)) });
	}
	s_attribute_clipboard_set = true;
	paste_attributes_button->Enable(true);
}

void EditItemTypeWindow::OnClickPasteAttributes(wxCommandEvent& WXUNUSED(event)) {
	if (!s_attribute_clipboard_set) {
		return;
	}
	attributes_grid->SaveEditControlValue();
	attributes_grid->HideCellEditControl();

	// Replace, not merge: the pasted set becomes the item's attribute list.
	if (attributes_grid->GetNumberRows() > 0) {
		attributes_grid->DeleteRows(0, attributes_grid->GetNumberRows());
	}
	for (const ItemXmlAttribute& attribute : s_attribute_clipboard) {
		AppendAttributeRow(attribute.key, attribute.value);
	}
	UpdateKeySuggestions();
}

void EditItemTypeWindow::OnClickCopyFlags(wxCommandEvent& WXUNUSED(event)) {
	s_flag_clipboard.flags.resize(flag_boxes.size());
	for (size_t i = 0; i < flag_boxes.size(); ++i) {
		s_flag_clipboard.flags[i] = flag_boxes[i]->GetValue();
	}
	s_flag_clipboard.has_light = light_check->GetValue();
	s_flag_clipboard.light_intensity = light_intensity_field->GetValue();
	s_flag_clipboard.light_color = light_color;
	s_flag_clipboard_set = true;
	paste_flags_button->Enable(true);
}

void EditItemTypeWindow::OnClickPasteFlags(wxCommandEvent& WXUNUSED(event)) {
	if (!s_flag_clipboard_set) {
		return;
	}
	for (size_t i = 0; i < flag_boxes.size() && i < s_flag_clipboard.flags.size(); ++i) {
		flag_boxes[i]->SetValue(s_flag_clipboard.flags[i]);
	}
	light_check->SetValue(s_flag_clipboard.has_light);
	light_intensity_field->SetValue(s_flag_clipboard.light_intensity);
	light_color = s_flag_clipboard.light_color;
	light_color_swatch->SetBackgroundColour(colorFromEightBit(light_color));
	light_color_swatch->SetToolTip(wxString::Format("%d", light_color));
	light_color_swatch->Refresh();
}

void EditItemTypeWindow::OnClickAddAttribute(wxCommandEvent& WXUNUSED(event)) {
	AppendAttributeRow("", "");
	attributes_grid->GoToCell(attributes_grid->GetNumberRows() - 1, 0);
	attributes_grid->SetFocus();
}

void EditItemTypeWindow::OnClickRemoveAttribute(wxCommandEvent& WXUNUSED(event)) {
	attributes_grid->SaveEditControlValue();
	attributes_grid->HideCellEditControl();

	wxArrayInt rows = attributes_grid->GetSelectedRows();
	if (rows.IsEmpty()) {
		const int cursorRow = attributes_grid->GetGridCursorRow();
		if (cursorRow >= 0 && cursorRow < attributes_grid->GetNumberRows()) {
			rows.Add(cursorRow);
		}
	}
	rows.Sort([](int* a, int* b) { return *b - *a; }); // descending, so indices stay valid
	for (size_t i = 0; i < rows.GetCount(); ++i) {
		attributes_grid->DeleteRows(rows[i], 1);
	}
	UpdateKeySuggestions();
}

void EditItemTypeWindow::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	attributes_grid->SaveEditControlValue();
	attributes_grid->HideCellEditControl();

	ItemXmlDefinition def;
	def.name = nstr(name_field->GetValue().Trim(true).Trim(false));
	for (int row = 0; row < attributes_grid->GetNumberRows(); ++row) {
		const std::string key = nstr(attributes_grid->GetCellValue(row, 0).Trim(true).Trim(false));
		const std::string value = nstr(attributes_grid->GetCellValue(row, 1));
		if (key.empty() || value.empty()) {
			continue;
		}
		def.attributes.push_back({ key, value });
	}

	wxString error;
	if (!SaveItemXmlDefinition(items_xml_file, item_id, def, error)) {
		g_gui.PopupDialog("Error", error, wxOK);
		return;
	}

	// Flag and light changes go to items.otb (server side) and the client .dat.
	uint32_t setMask = 0;
	uint32_t clearMask = 0;
	std::vector<GraphicManager::DatFlagPatch> datChanges;
	const std::vector<ItemFlagSpec>& flagSpecs = getItemFlagSpecs();
	for (size_t i = 0; i < flagSpecs.size(); ++i) {
		const bool checked = flag_boxes[i]->GetValue();
		if (checked == original_flags[i]) {
			continue;
		}
		if (flagSpecs[i].otb_flag != 0) {
			(checked ? setMask : clearMask) |= flagSpecs[i].otb_flag;
		}
		if (flagSpecs[i].dat_flag >= 0) {
			datChanges.push_back({ static_cast<uint8_t>(flagSpecs[i].dat_flag), flagSpecs[i].invert_dat ? !checked : checked, {} });
		}
	}

	ItemDatabase::OtbLightPatch lightPatch;
	const bool hasLight = light_check->GetValue();
	const int lightIntensity = light_intensity_field->GetValue();
	if (hasLight != orig_has_light || (hasLight && (lightIntensity != orig_light_intensity || light_color != orig_light_color))) {
		lightPatch.apply = true;
		lightPatch.hasLight = hasLight;
		lightPatch.level = static_cast<uint16_t>(lightIntensity);
		lightPatch.color = static_cast<uint16_t>(light_color);
		datChanges.push_back({ static_cast<uint8_t>(DatFlagLight), hasLight, { static_cast<uint8_t>(lightIntensity & 0xFF), static_cast<uint8_t>((lightIntensity >> 8) & 0xFF), static_cast<uint8_t>(light_color & 0xFF), static_cast<uint8_t>((light_color >> 8) & 0xFF) } });
	}

	const uint16_t newClientId = (display_client_id != orig_client_id) ? display_client_id : 0;
	if (setMask != 0 || clearMask != 0 || lightPatch.apply || newClientId != 0 || !datChanges.empty()) {
		if (!g_items.patchOtbFlags(items_otb_file, item_id, setMask, clearMask, lightPatch, newClientId, error)) {
			g_gui.PopupDialog("Error", "items.xml was saved, but updating items.otb failed:\n" + error, wxOK);
			return;
		}
		if (!datChanges.empty()) {
			if (!g_gui.gfx.patchSpriteMetadataFlags(g_gui.gfx.getMetadataFileName().GetFullPath(), display_client_id, datChanges, error)) {
				g_gui.PopupDialog("Error", "items.xml and items.otb were saved, but updating the client .dat failed:\n" + error, wxOK);
				return;
			}
		}
		// Reflect the new flags, sprite, and light in the running editor.
		ItemType& target = g_items.getItemType(item_id);
		for (size_t i = 0; i < flagSpecs.size(); ++i) {
			target.*(flagSpecs[i].field) = flag_boxes[i]->GetValue();
		}
		if (newClientId != 0) {
			target.clientID = newClientId;
			target.sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(newClientId));
		}
		if (lightPatch.apply) {
			if (GameSprite* targetSprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(display_client_id))) {
				targetSprite->has_light = lightPatch.hasLight;
				targetSprite->light = SpriteLight { static_cast<uint8_t>(lightPatch.level), static_cast<uint8_t>(lightPatch.color) };
			}
		}
	}

	g_gui.RefreshView();
	EndModal(1);
}

void EditItemTypeWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(0);
}
