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

#ifndef RME_EDIT_ITEM_TYPE_WINDOW_H_
#define RME_EDIT_ITEM_TYPE_WINDOW_H_

#include "main.h"

#include <array>

#include "common_windows.h"

class wxGrid;
class wxGridEvent;
class wxChoice;
class wxCheckBox;
class wxSpinCtrl;
class wxPanel;
class SpritePreviewPanel;

// One <attribute key="..." value="..."/> row of an <item> node in items.xml.
struct ItemXmlAttribute {
	std::string key;
	std::string value;
};

// The editable slice of an <item> definition in items.xml.
struct ItemXmlDefinition {
	std::string name;
	std::vector<ItemXmlAttribute> attributes;
};

// Reads the definition of server id 'id' from items.xml (the last matching
// node wins; fromid/toid ranges count as matches). Returns false only when the
// file itself cannot be read — an item without a node is not an error, 'out'
// is simply left empty.
bool LoadItemXmlDefinition(const std::string& file, uint16_t id, ItemXmlDefinition& out, wxString& error);

// Writes 'def' as the standalone <item id="..."> node for 'id' — creating the
// node in id order if it is missing, or splitting any fromid/toid range the id
// was part of — then re-applies the node to g_items so the editor reflects the
// change without a restart.
bool SaveItemXmlDefinition(const std::string& file, uint16_t id, const ItemXmlDefinition& def, wxString& error);

// Duplicates an item type completely: a new server id with copies of the
// items.xml and items.otb entries, plus a brand-new client sprite (own .dat
// entry and own .spr pixel copies), so the duplicate looks identical but
// shares nothing with the original. Returns the new server id, or 0 with
// 'error' set.
uint16_t DuplicateItemType(uint16_t sourceServerId, wxString& error);

// ============================================================================
// Edit Item window
// Edit an item type's name and its items.xml <attribute> list; saving writes
// the changes back to items.xml.

class EditItemTypeWindow : public ObjectPropertiesWindowBase {
public:
	EditItemTypeWindow(wxWindow* parent, uint16_t itemId, wxPoint pos = wxDefaultPosition);

	// Replaces the previewed sprite's pixels with an image file (used by the
	// Import button and by dropping a file onto the sprite box).
	void ImportSpriteFromFile(const wxString& path);

private:
	void AppendAttributeRow(const std::string& key, const std::string& value);
	// Row index of an attribute key already in the grid (case-insensitive), or -1.
	int FindAttributeRow(const wxString& key) const;
	// Re-detects the item kind from the grid (weaponType / slotType / ...) and
	// narrows every key cell's dropdown to the keys that kind can carry.
	void UpdateKeySuggestions();

	void OnGridCellChanged(wxGridEvent&);
	void OnSelectPreset(wxCommandEvent&);
	void OnClickPickLightColor(wxCommandEvent&);
	void OnClickSprite(wxCommandEvent&);
	void ApplySpriteTransform(int transform); // GraphicManager::SpriteTransform
	void OnClickRotateSprite(wxCommandEvent&);
	void OnClickFlipSpriteHorizontal(wxCommandEvent&);
	void OnClickFlipSpriteVertical(wxCommandEvent&);
	void OnClickImportSprite(wxCommandEvent&);
	void OnClickExportSprite(wxCommandEvent&);
	// Refreshes the id/sprite-structure label above the preview.
	void UpdateItemLabel();
	// Refreshes the right-hand sprite sidebar (preview + structure info).
	void UpdateSpriteSidebar();
	void OnClickAddAttribute(wxCommandEvent&);
	void OnClickRemoveAttribute(wxCommandEvent&);
	void OnClickCopyAttributes(wxCommandEvent&);
	void OnClickPasteAttributes(wxCommandEvent&);
	void OnClickCopyFlags(wxCommandEvent&);
	void OnClickPasteFlags(wxCommandEvent&);
	void OnClickApplyStructure(wxCommandEvent&);
	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);

	uint16_t item_id;
	std::string items_xml_file;
	std::string items_otb_file;

	wxTextCtrl* name_field;
	wxChoice* preset_field;
	wxGrid* attributes_grid;
	std::vector<wxString> key_choices; // current key-column suggestions
	std::vector<wxCheckBox*> flag_boxes; // one per entry of getItemFlagSpecs()
	std::vector<bool> original_flags; // checkbox states at dialog open
	wxButton* paste_attributes_button; // enabled once an attribute set was copied
	wxButton* paste_flags_button; // enabled once a flag set was copied

	class DCButton* sprite_button;
	wxStaticText* item_label;
	SpritePreviewPanel* sprite_preview; // right sidebar: live preview
	wxStaticText* sprite_info_label; // right sidebar: structure info
	std::array<wxSpinCtrl*, 7> structure_fields {}; // sidebar: tiles w/h, layers, patterns x/y/z, frames
	uint16_t orig_client_id;
	uint16_t display_client_id; // client id currently previewed (may differ from orig until saved)

	wxCheckBox* light_check;
	wxSpinCtrl* light_intensity_field;
	wxPanel* light_color_swatch;
	int light_color; // Tibia 8-bit palette index (0-215)
	bool orig_has_light;
	int orig_light_intensity;
	int orig_light_color;
};

#endif
