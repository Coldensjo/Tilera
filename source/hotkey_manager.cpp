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

#include "hotkey_manager.h"
#include "settings.h"

#include <wx/accel.h>
#include <wx/tokenzr.h>

HotkeyManager g_hotkeys;

//=============================================================================
// HotkeyChord

wxString HotkeyChord::toString() const {
	if (!valid()) {
		return wxString();
	}
	wxAcceleratorEntry entry(modifiers == 0 ? wxACCEL_NORMAL : modifiers, keyCode);
	wxString str = entry.ToString();
	if (!str.empty()) {
		return str;
	}
	// Fallback for key codes wx cannot name; keeps round-tripping possible.
	wxString prefix;
	if (modifiers & wxACCEL_CTRL) {
		prefix << "Ctrl+";
	}
	if (modifiers & wxACCEL_ALT) {
		prefix << "Alt+";
	}
	if (modifiers & wxACCEL_SHIFT) {
		prefix << "Shift+";
	}
	return prefix << "Key-" << keyCode;
}

bool HotkeyChord::fromString(const wxString& str, HotkeyChord& out) {
	if (str.empty()) {
		return false;
	}

	// Handle the "Key-<code>" fallback form of toString().
	wxString rest = str;
	int modifiers = 0;
	for (bool again = true; again;) {
		again = false;
		if (rest.StartsWith("Ctrl+", &rest)) {
			modifiers |= wxACCEL_CTRL;
			again = true;
		} else if (rest.StartsWith("Alt+", &rest)) {
			modifiers |= wxACCEL_ALT;
			again = true;
		} else if (rest.StartsWith("Shift+", &rest)) {
			modifiers |= wxACCEL_SHIFT;
			again = true;
		}
	}
	wxString code;
	if (rest.StartsWith("Key-", &code)) {
		long value = 0;
		if (code.ToLong(&value) && value != 0) {
			out.modifiers = modifiers;
			out.keyCode = int(value);
			return true;
		}
		return false;
	}

	wxAcceleratorEntry entry;
	if (!entry.FromString(str)) {
		return false;
	}
	out.modifiers = entry.GetFlags();
	out.keyCode = entry.GetKeyCode();
	return out.valid();
}

HotkeyChord HotkeyChord::fromKeyEvent(const wxKeyEvent& event) {
	HotkeyChord chord;
	if (event.ControlDown()) {
		chord.modifiers |= wxACCEL_CTRL;
	}
	if (event.ShiftDown()) {
		chord.modifiers |= wxACCEL_SHIFT;
	}
	if (event.AltDown()) {
		chord.modifiers |= wxACCEL_ALT;
	}
	int code = event.GetKeyCode();
	if (code >= 'a' && code <= 'z') {
		code = code - 'a' + 'A';
	}
	chord.keyCode = code;
	return chord;
}

//=============================================================================
// HotkeyAction

std::vector<HotkeyChord> HotkeyAction::chords() const {
	if (overridden) {
		std::vector<HotkeyChord> result;
		if (override_.valid()) {
			result.push_back(override_);
		}
		return result;
	}
	return defaults;
}

wxString HotkeyAction::shortcutString() const {
	if (!fixedDisplay.empty()) {
		return fixedDisplay;
	}
	const std::vector<HotkeyChord> current = chords();
	return current.empty() ? wxString() : current.front().toString();
}

wxString HotkeyAction::defaultString() const {
	if (!fixedDisplay.empty()) {
		return fixedDisplay;
	}
	wxString result;
	for (const HotkeyChord& chord : defaults) {
		if (!result.empty()) {
			result << ", ";
		}
		result << chord.toString();
	}
	return result;
}

//=============================================================================
// HotkeyManager

HotkeyManager::HotkeyManager() {
	registerBuiltinActions();
}

HotkeyManager::~HotkeyManager() {
	for (auto& pair : actions) {
		delete pair.second;
	}
}

namespace {
	HotkeyChord Chord(int modifiers, int keyCode) {
		return HotkeyChord(modifiers, keyCode);
	}
}

HotkeyAction* HotkeyManager::addAction(const std::string& id, const wxString& name, const wxString& category, const wxString& description, CanvasHotkey canvasAction, std::vector<HotkeyChord> defaults, bool rebindable) {
	HotkeyAction* action = newd HotkeyAction();
	action->id = id;
	action->name = name;
	action->category = category;
	action->description = description;
	action->canvasAction = canvasAction;
	action->defaults = std::move(defaults);
	action->rebindable = rebindable;
	actions[id] = action;
	ordered_actions.push_back(action);
	applyStoredOverride(action);
	return action;
}

void HotkeyManager::addGesture(const std::string& id, const wxString& name, const wxString& display, const wxString& description) {
	HotkeyAction* action = addAction(id, name, "Mouse & Modifiers", description, CanvasHotkey::None, {}, false);
	action->fixedDisplay = display;
}

void HotkeyManager::registerBuiltinActions() {
	const wxString canvas = "Map Canvas";

	addAction("canvas:floor_up", "Floor Up", canvas, "Go one floor up.", CanvasHotkey::FloorUp, { Chord(0, WXK_PAGEUP), Chord(0, WXK_NUMPAD_ADD) });
	addAction("canvas:floor_down", "Floor Down", canvas, "Go one floor down.", CanvasHotkey::FloorDown, { Chord(0, WXK_PAGEDOWN), Chord(0, WXK_NUMPAD_SUBTRACT) });
	addAction("canvas:zoom_in", "Zoom In (Canvas)", canvas, "Zoom in around the mouse cursor.", CanvasHotkey::ZoomIn, { Chord(0, WXK_NUMPAD_MULTIPLY) });
	addAction("canvas:zoom_out", "Zoom Out (Canvas)", canvas, "Zoom out around the mouse cursor.", CanvasHotkey::ZoomOut, { Chord(0, WXK_NUMPAD_DIVIDE) });
	addAction("canvas:brush_size_increase", "Increase Brush Size", canvas, "Make the current brush one step larger.", CanvasHotkey::BrushSizeIncrease, { Chord(0, '+'), Chord(0, '[') });
	addAction("canvas:brush_size_decrease", "Decrease Brush Size", canvas, "Make the current brush one step smaller.", CanvasHotkey::BrushSizeDecrease, { Chord(0, '-'), Chord(0, ']') });
	addAction("canvas:scroll_north", "Scroll North", canvas, "Scroll the map view north. Hold Ctrl to scroll faster.", CanvasHotkey::ScrollNorth, { Chord(0, WXK_UP), Chord(0, WXK_NUMPAD_UP) });
	addAction("canvas:scroll_south", "Scroll South", canvas, "Scroll the map view south. Hold Ctrl to scroll faster.", CanvasHotkey::ScrollSouth, { Chord(0, WXK_DOWN), Chord(0, WXK_NUMPAD_DOWN) });
	addAction("canvas:scroll_west", "Scroll West", canvas, "Scroll the map view west. Hold Ctrl to scroll faster.", CanvasHotkey::ScrollWest, { Chord(0, WXK_LEFT), Chord(0, WXK_NUMPAD_LEFT) });
	addAction("canvas:scroll_east", "Scroll East", canvas, "Scroll the map view east. Hold Ctrl to scroll faster.", CanvasHotkey::ScrollEast, { Chord(0, WXK_RIGHT), Chord(0, WXK_NUMPAD_RIGHT) });
	addAction("canvas:switch_mode", "Switch Selection/Drawing Mode", canvas, "Toggle between selection mode and drawing mode.", CanvasHotkey::SwitchMode, { Chord(0, WXK_SPACE) });
	addAction("canvas:refresh_doodad_preview", "Refresh Doodad Preview", canvas, "Regenerate the random preview of the current doodad brush.", CanvasHotkey::RefreshDoodadPreview, { Chord(wxACCEL_CTRL, WXK_SPACE) });
	addAction("canvas:next_tab", "Next Map Tab", canvas, "Switch to the next open map tab.", CanvasHotkey::NextTab, { Chord(0, WXK_TAB) }, false);
	addAction("canvas:previous_tab", "Previous Map Tab", canvas, "Switch to the previous open map tab.", CanvasHotkey::PreviousTab, { Chord(wxACCEL_SHIFT, WXK_TAB) }, false);
	addAction("canvas:delete_selection", "Delete Selection", canvas, "Delete everything in the current selection.", CanvasHotkey::DeleteSelection, { Chord(0, WXK_DELETE) });
	addAction("canvas:rotate_brush_ccw", "Rotate Brush/Paste Counter-Clockwise", canvas, "Rotate a pending paste, or cycle the current doodad brush variation backwards.", CanvasHotkey::RotateBrushCCW, { Chord(0, 'Z'), Chord(wxACCEL_SHIFT, 'Z') });
	addAction("canvas:rotate_brush_cw", "Rotate Brush/Paste Clockwise", canvas, "Rotate a pending paste, or cycle the current doodad brush variation forwards.", CanvasHotkey::RotateBrushCW, { Chord(0, 'X'), Chord(wxACCEL_SHIFT, 'X') });
	addAction("canvas:previous_brush", "Select Previous Brush", canvas, "Re-select the brush that was active before the current one.", CanvasHotkey::PreviousBrush, { Chord(0, 'Q') });
	addAction("canvas:fill_key", "Flood Fill Key (hold)", canvas, "Hold this key and Ctrl+Click with a ground brush to flood fill.", CanvasHotkey::FillKeyHold, { Chord(0, 'D'), Chord(wxACCEL_CTRL, 'D') });
	addAction("canvas:add_map_comment", "Add Map Comment", canvas, "Add a comment note at the tile under the mouse cursor.", CanvasHotkey::AddMapComment, { Chord(wxACCEL_CTRL | wxACCEL_SHIFT, 'M') });
	addAction("canvas:live_ping", "Send Live Ping", canvas, "Ping the tile under the mouse cursor for other live collaborators.", CanvasHotkey::LivePing, { Chord(wxACCEL_CTRL | wxACCEL_SHIFT, 'P') });

	{
		// The ten numerical quick slots; the digit pressed selects the slot,
		// so these are fixed ranges rather than single rebindable chords.
		std::vector<HotkeyChord> use, assign;
		for (int digit = '0'; digit <= '9'; ++digit) {
			use.push_back(Chord(0, digit));
			assign.push_back(Chord(wxACCEL_CTRL, digit));
		}
		HotkeyAction* action = addAction("canvas:quick_slot_use", "Use Quick Slot 0-9", canvas, "Jump to the position or select the brush stored in the pressed slot.", CanvasHotkey::QuickSlotUse, std::move(use), false);
		action->fixedDisplay = "0 - 9";
		action = addAction("canvas:quick_slot_assign", "Assign Quick Slot 0-9", canvas, "Store the current brush (drawing mode) or screen center position (selection mode) in the pressed slot.", CanvasHotkey::QuickSlotAssign, std::move(assign), false);
		action->fixedDisplay = "Ctrl+0 - Ctrl+9";
	}

	addAction("canvas:fit_view_to_map", "Fit View to Map", canvas, "Zoom and scroll so the whole map fits in the view. Not reachable from the menus.", CanvasHotkey::FitViewToMap, {});

	// Brush shape and size presets; unbound by default, same behavior as the
	// Sizes toolbar buttons.
	{
		const wxString brushes = "Brushes";
		addAction("brush:toggle_shape", "Toggle Brush Shape", brushes, "Switch the brush between rectangular and circular.", CanvasHotkey::ToggleBrushShape, {});
		addAction("brush:size_1", "Brush Size 1 (1x1)", brushes, "Set the brush to a single tile.", CanvasHotkey::BrushSize1, {});
		addAction("brush:size_2", "Brush Size 2 (2x2)", brushes, "Set the brush to a 2x2 square.", CanvasHotkey::BrushSize2x2, {});
		addAction("brush:size_3", "Brush Size 3 (3x3)", brushes, "Set the brush size to 3 (radius 1).", CanvasHotkey::BrushSize3, {});
		addAction("brush:size_4", "Brush Size 4 (5x5)", brushes, "Set the brush size to 4 (radius 2).", CanvasHotkey::BrushSize4, {});
		addAction("brush:size_5", "Brush Size 5 (9x9)", brushes, "Set the brush size to 5 (radius 4).", CanvasHotkey::BrushSize5, {});
		addAction("brush:size_6", "Brush Size 6 (13x13)", brushes, "Set the brush size to 6 (radius 6).", CanvasHotkey::BrushSize6, {});
		addAction("brush:size_7", "Brush Size 7 (17x17)", brushes, "Set the brush size to 7 (radius 8).", CanvasHotkey::BrushSize7, {});
		addAction("brush:size_8", "Brush Size 8 (23x23)", brushes, "Set the brush size to 8 (radius 11).", CanvasHotkey::BrushSize8, {});

		// Tool brushes; unbound by default, same behavior as the Brushes
		// toolbar buttons.
		addAction("brush:optional_border", "Select Optional Border Tool", brushes, "Select the optional border (gravel) tool.", CanvasHotkey::SelectOptionalBorder, {});
		addAction("brush:eraser", "Select Eraser", brushes, "Select the eraser brush.", CanvasHotkey::SelectEraser, {});
		addAction("brush:pz_zone", "Select Protected Zone Brush", brushes, "Select the protected zone brush.", CanvasHotkey::SelectPzBrush, {});
		addAction("brush:nopvp_zone", "Select No PvP Zone Brush", brushes, "Select the no-PvP zone brush.", CanvasHotkey::SelectNoPvpBrush, {});
		addAction("brush:nologout_zone", "Select No Logout Zone Brush", brushes, "Select the no-logout zone brush.", CanvasHotkey::SelectNoLogoutBrush, {});
		addAction("brush:pvp_zone", "Select PvP Zone Brush", brushes, "Select the PvP zone brush.", CanvasHotkey::SelectPvpBrush, {});
		addAction("brush:refresh_zone", "Select Refresh Zone Brush", brushes, "Select the refresh zone brush.", CanvasHotkey::SelectRefreshBrush, {});
		addAction("brush:normal_door", "Select Normal Door Tool", brushes, "Select the normal door tool.", CanvasHotkey::SelectNormalDoor, {});
		addAction("brush:locked_door", "Select Locked Door Tool", brushes, "Select the locked door tool.", CanvasHotkey::SelectLockedDoor, {});
		addAction("brush:magic_door", "Select Magic Door Tool", brushes, "Select the magic door tool.", CanvasHotkey::SelectMagicDoor, {});
		addAction("brush:quest_door", "Select Quest Door Tool", brushes, "Select the quest door tool.", CanvasHotkey::SelectQuestDoor, {});
		addAction("brush:normal_alt_door", "Select Normal Door (alt) Tool", brushes, "Select the alternative normal door tool.", CanvasHotkey::SelectNormalAltDoor, {});
		addAction("brush:archway_door", "Select Archway Tool", brushes, "Select the archway tool.", CanvasHotkey::SelectArchwayDoor, {});
		addAction("brush:hatch_window", "Select Hatch Window Tool", brushes, "Select the hatch window tool.", CanvasHotkey::SelectHatchWindow, {});
		addAction("brush:window", "Select Window Tool", brushes, "Select the window tool.", CanvasHotkey::SelectWindow, {});
	}

	addAction("global:toggle_script_menu", "Toggle Make Script Menu", "Global", "Show or hide the tileset make-script context menu entries.", CanvasHotkey::ToggleScriptMenu, { Chord(wxACCEL_CTRL, WXK_F12) });

	// Read-only reference entries for the mouse-and-modifier gestures that
	// live alongside the keyboard hotkeys.
	addGesture("gesture:pan_view", "Pan View", "Middle Mouse Drag", "Drag the map view. Uses the right button instead when 'Switch mousebuttons' is enabled in Preferences.");
	addGesture("gesture:properties_menu", "Tile Context Menu / Properties", "Right-Click", "Open the context menu for the tile under the cursor. Uses the middle button instead when 'Switch mousebuttons' is enabled in Preferences.");
	addGesture("gesture:erase", "Erase with Brush", "Ctrl+Click / Ctrl+Drag", "Erase with the current brush instead of drawing.");
	addGesture("gesture:straight_line", "Draw Straight Line", "Alt while dragging", "Constrain a brush drag to a straight line.");
	addGesture("gesture:box_select", "Rectangle / Lasso Select", "Shift+Drag", "Select an area in selection mode. Lasso Select in Selection > Selection Mode switches the shape.");
	addGesture("gesture:pick_brush", "Pick Brush from Tile", "Ctrl+Alt+Click", "Select the brush of the topmost creature or item on the clicked tile.");
	addGesture("gesture:flood_fill", "Flood Fill Ground", "Hold D + Ctrl+Click", "Flood fill with the current ground brush. The held key follows the 'Flood Fill Key' binding.");
	addGesture("gesture:live_ping_mouse", "Send Live Ping (Mouse)", "Alt+Shift+Click", "Ping the clicked tile for other live collaborators.");
}

void HotkeyManager::applyStoredOverride(HotkeyAction* action) {
	auto it = stored_overrides.find(action->id);
	if (it == stored_overrides.end() || !action->rebindable) {
		return;
	}

	if (it->second.empty()) {
		action->overridden = true;
		action->override_ = HotkeyChord();
		return;
	}

	HotkeyChord chord;
	if (HotkeyChord::fromString(wxstr(it->second), chord)) {
		action->overridden = true;
		action->override_ = chord;
	}
}

void HotkeyManager::load() {
	if (loaded) {
		return;
	}
	loaded = true;

	stored_overrides.clear();
	wxStringTokenizer tokenizer(wxstr(g_settings.getString(Config::CUSTOM_HOTKEYS)), "\n");
	while (tokenizer.HasMoreTokens()) {
		const wxString line = tokenizer.GetNextToken();
		const int separator = line.Find('=');
		if (separator == wxNOT_FOUND) {
			continue;
		}
		const std::string id = nstr(line.Left(separator));
		if (id.empty()) {
			continue;
		}
		stored_overrides[id] = nstr(line.Mid(separator + 1));
	}

	// Builtin actions registered before load(); menu actions register later
	// and pick their override up in addAction().
	for (HotkeyAction* action : ordered_actions) {
		applyStoredOverride(action);
	}
}

void HotkeyManager::save() const {
	// Rebuild the stored map from live state, but keep entries for ids that
	// were never registered this run (e.g. actions from a newer/older
	// menubar.xml) instead of silently dropping them.
	std::map<std::string, std::string> result = stored_overrides;
	for (const HotkeyAction* action : ordered_actions) {
		if (action->overridden) {
			result[action->id] = nstr(action->override_.toString());
		} else {
			result.erase(action->id);
		}
	}

	std::string serialized;
	for (const auto& pair : result) {
		serialized += pair.first;
		serialized += '=';
		serialized += pair.second;
		serialized += '\n';
	}
	g_settings.setString(Config::CUSTOM_HOTKEYS, serialized);
	g_settings.save();
}

void HotkeyManager::registerMenuAction(const std::string& actionName, const wxString& itemName, const wxString& category, const wxString& help, const wxString& defaultHotkey) {
	const std::string id = "menu:" + actionName;
	auto it = actions.find(id);
	if (it != actions.end()) {
		return; // already known (duplicate item or menubar reload)
	}

	// Menu item names carry mnemonics and trailing ellipses; strip both.
	wxString name = itemName;
	name.Replace("&", "");
	if (name.EndsWith("...")) {
		name.RemoveLast(3);
	}

	std::vector<HotkeyChord> defaults;
	HotkeyChord chord;
	if (HotkeyChord::fromString(defaultHotkey, chord)) {
		defaults.push_back(chord);
	}
	addAction(id, name, category.empty() ? wxString("Menu") : category, help, CanvasHotkey::None, std::move(defaults));
}

wxString HotkeyManager::getMenuHotkeyString(const std::string& actionName) const {
	auto it = actions.find("menu:" + actionName);
	if (it == actions.end()) {
		return wxString();
	}
	return it->second->shortcutString();
}

CanvasHotkey HotkeyManager::matchCanvas(const wxKeyEvent& event, bool& fastScroll) const {
	fastScroll = false;
	const HotkeyChord pressed = HotkeyChord::fromKeyEvent(event);
	if (!pressed.valid()) {
		return CanvasHotkey::None;
	}

	for (const HotkeyAction* action : ordered_actions) {
		if (action->canvasAction == CanvasHotkey::None) {
			continue;
		}
		for (const HotkeyChord& chord : action->chords()) {
			if (chord == pressed) {
				return action->canvasAction;
			}
		}
	}

	// Ctrl on top of a scroll binding means "scroll faster".
	if (pressed.modifiers & wxACCEL_CTRL) {
		const HotkeyChord stripped(pressed.modifiers & ~wxACCEL_CTRL, pressed.keyCode);
		for (const HotkeyAction* action : ordered_actions) {
			switch (action->canvasAction) {
				case CanvasHotkey::ScrollNorth:
				case CanvasHotkey::ScrollSouth:
				case CanvasHotkey::ScrollWest:
				case CanvasHotkey::ScrollEast:
					break;
				default:
					continue;
			}
			for (const HotkeyChord& chord : action->chords()) {
				if (chord == stripped) {
					fastScroll = true;
					return action->canvasAction;
				}
			}
		}
	}

	return CanvasHotkey::None;
}

bool HotkeyManager::matches(const std::string& id, const wxKeyEvent& event) const {
	auto it = actions.find(id);
	if (it == actions.end()) {
		return false;
	}
	const HotkeyChord pressed = HotkeyChord::fromKeyEvent(event);
	for (const HotkeyChord& chord : it->second->chords()) {
		if (chord == pressed) {
			return true;
		}
	}
	return false;
}

const HotkeyAction* HotkeyManager::findConflict(const HotkeyChord& chord, const std::string& excludeId) const {
	if (!chord.valid()) {
		return nullptr;
	}
	for (const HotkeyAction* action : ordered_actions) {
		if (action->id == excludeId) {
			continue;
		}
		for (const HotkeyChord& bound : action->chords()) {
			if (bound == chord) {
				return action;
			}
		}
	}
	return nullptr;
}

HotkeyAction* HotkeyManager::getAction(const std::string& id) {
	auto it = actions.find(id);
	return it == actions.end() ? nullptr : it->second;
}

bool HotkeyManager::setOverride(const std::string& id, const HotkeyChord& chord) {
	HotkeyAction* action = getAction(id);
	if (!action || !action->rebindable) {
		return false;
	}

	// Re-assigning the primary default is the same as a reset.
	if (chord.valid() && !action->defaults.empty() && chord == action->defaults.front()) {
		action->overridden = false;
		action->override_ = HotkeyChord();
	} else {
		action->overridden = true;
		action->override_ = chord;
	}
	save();
	return true;
}

void HotkeyManager::resetOverride(const std::string& id) {
	HotkeyAction* action = getAction(id);
	if (!action || !action->overridden) {
		return;
	}
	action->overridden = false;
	action->override_ = HotkeyChord();
	save();
}

void HotkeyManager::resetAll() {
	for (HotkeyAction* action : ordered_actions) {
		action->overridden = false;
		action->override_ = HotkeyChord();
	}
	stored_overrides.clear();
	save();
}
