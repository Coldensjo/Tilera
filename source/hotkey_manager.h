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

#ifndef RME_HOTKEY_MANAGER_H_
#define RME_HOTKEY_MANAGER_H_

#include <map>
#include <string>
#include <vector>

// A single keyboard chord: modifier flags (wxACCEL_*) + a wx key code.
// Letter key codes are always stored uppercase, matching wxKeyEvent.
struct HotkeyChord {
	int modifiers = 0; // combination of wxACCEL_CTRL / wxACCEL_SHIFT / wxACCEL_ALT
	int keyCode = 0; // 0 means "no chord"

	HotkeyChord() = default;
	HotkeyChord(int modifiers, int keyCode) : modifiers(modifiers), keyCode(keyCode) { }

	bool valid() const {
		return keyCode != 0;
	}
	bool operator==(const HotkeyChord& other) const {
		return modifiers == other.modifiers && keyCode == other.keyCode;
	}
	bool operator!=(const HotkeyChord& other) const {
		return !(*this == other);
	}

	// Human-readable / persistable form, e.g. "Ctrl+Shift+Z".
	wxString toString() const;
	// Parses the form produced by toString(). Returns false on garbage.
	static bool fromString(const wxString& str, HotkeyChord& out);
	// Builds a chord from a live key event (Ctrl/Shift/Alt + key code).
	static HotkeyChord fromKeyEvent(const wxKeyEvent& event);
};

// Editor actions handled outside the menubar (map canvas / main frame),
// i.e. the hotkeys that are NOT visible in menubar.xml.
enum class CanvasHotkey {
	None,
	FloorUp,
	FloorDown,
	ZoomIn,
	ZoomOut,
	BrushSizeIncrease,
	BrushSizeDecrease,
	ScrollNorth,
	ScrollSouth,
	ScrollWest,
	ScrollEast,
	SwitchMode,
	RefreshDoodadPreview,
	NextTab,
	PreviousTab,
	DeleteSelection,
	RotateBrushCCW,
	RotateBrushCW,
	PreviousBrush,
	QuickSlotUse, // digit keys; the pressed digit selects the slot
	QuickSlotAssign, // Ctrl+digit
	FillKeyHold, // held while Ctrl+clicking a ground brush to flood fill
	AddMapComment,
	LivePing,
	ToggleScriptMenu, // handled in MainFrame::OnCharHook
	FitViewToMap,
	// Brush shape / size presets (mirror the Sizes toolbar)
	ToggleBrushShape,
	BrushSize1,
	BrushSize2x2,
	BrushSize3,
	BrushSize4,
	BrushSize5,
	BrushSize6,
	BrushSize7,
	BrushSize8,
	// Tool brushes (mirror the Brushes toolbar)
	SelectOptionalBorder,
	SelectEraser,
	SelectPzBrush,
	SelectNoPvpBrush,
	SelectNoLogoutBrush,
	SelectPvpBrush,
	SelectRefreshBrush,
	SelectNormalDoor,
	SelectLockedDoor,
	SelectMagicDoor,
	SelectQuestDoor,
	SelectNormalAltDoor,
	SelectArchwayDoor,
	SelectHatchWindow,
	SelectWindow,
};

// One bindable (or at least listable) action known to the hotkey manager.
class HotkeyAction {
public:
	std::string id; // stable identifier used for persistence ("menu:SAVE", "canvas:floor_up")
	wxString name; // human-readable name shown in the manager
	wxString category; // grouping ("File", "Map Canvas", ...)
	wxString description; // longer help text
	bool rebindable = true; // fixed entries are listed but cannot be changed
	wxString fixedDisplay; // display text for entries without real chords (mouse gestures)
	CanvasHotkey canvasAction = CanvasHotkey::None; // None for menu actions
	std::vector<HotkeyChord> defaults; // first is primary, the rest are aliases

	// User override state. When overridden is true the defaults are ignored;
	// an invalid override chord means "explicitly unbound".
	bool overridden = false;
	HotkeyChord override_;

	// Effective bindings (override if set, defaults otherwise).
	std::vector<HotkeyChord> chords() const;
	// Effective primary binding as text ("" when unbound).
	wxString shortcutString() const;
	// Default binding(s) as text, aliases joined with ", ".
	wxString defaultString() const;
	bool isMenuAction() const {
		return canvasAction == CanvasHotkey::None && fixedDisplay.empty();
	}
};

// Central registry of every hotkey in the editor: the menubar accelerators
// (registered while menubar.xml loads) plus all hardcoded canvas/frame keys
// and a read-only list of mouse-modifier gestures. Handles persistence of
// user overrides and chord lookup for the canvas key handler.
class HotkeyManager {
public:
	HotkeyManager();
	~HotkeyManager();

	// Reads user overrides from settings; call once after g_settings loads,
	// before the main frame (and thus the menubar) is created.
	void load();
	// Writes user overrides to settings and flushes the config.
	void save() const;

	// Called from MainMenuBar::LoadItem for every <item> with an action.
	// Registration is idempotent; the first call fixes the defaults.
	void registerMenuAction(const std::string& actionName, const wxString& itemName, const wxString& category, const wxString& help, const wxString& defaultHotkey);

	// Effective accelerator text for a menubar action ("" when unbound).
	wxString getMenuHotkeyString(const std::string& actionName) const;

	// Resolves a canvas key event to an action. When the event carries Ctrl
	// on top of a scroll binding, the scroll action is returned with
	// fastScroll set (Ctrl+Arrow = fast scroll).
	CanvasHotkey matchCanvas(const wxKeyEvent& event, bool& fastScroll) const;
	// True if the event matches the effective binding of the given action.
	bool matches(const std::string& id, const wxKeyEvent& event) const;

	// Returns the action a chord is currently bound to, or nullptr.
	// Actions whose id equals excludeId are ignored.
	const HotkeyAction* findConflict(const HotkeyChord& chord, const std::string& excludeId) const;

	HotkeyAction* getAction(const std::string& id);
	const std::vector<HotkeyAction*>& getActions() const {
		return ordered_actions;
	}

	// chord.valid() == false unbinds the action. Returns false for unknown
	// or non-rebindable actions. Persists immediately.
	bool setOverride(const std::string& id, const HotkeyChord& chord);
	// Restores the default binding of one action / of everything.
	void resetOverride(const std::string& id);
	void resetAll();

private:
	HotkeyAction* addAction(const std::string& id, const wxString& name, const wxString& category, const wxString& description, CanvasHotkey canvasAction, std::vector<HotkeyChord> defaults, bool rebindable = true);
	void addGesture(const std::string& id, const wxString& name, const wxString& display, const wxString& description);
	void registerBuiltinActions();
	void applyStoredOverride(HotkeyAction* action);

	std::map<std::string, HotkeyAction*> actions;
	std::vector<HotkeyAction*> ordered_actions; // registration order, for display
	std::map<std::string, std::string> stored_overrides; // raw values from settings
	bool loaded = false;
};

extern HotkeyManager g_hotkeys;

#endif
