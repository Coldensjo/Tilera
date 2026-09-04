//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_THEME_H_
#define RME_THEME_H_

#include <wx/colour.h>

class wxWindow;
class wxAuiManager;
class wxAuiToolBar;
class wxAuiNotebook;

enum class ThemeMode {
	Dark = 0,
	Light = 1,
	System = 2,
};

struct ThemePalette {
	wxColour window;
	wxColour surface;
	wxColour control;
	wxColour border;
	wxColour text;
	wxColour mutedText;
	wxColour selection;
	wxColour hover;
	wxColour disabledText;
	wxColour accent;
};

class ThemeManager {
public:
	static ThemeManager& Get();

	static ThemeMode NormalizeMode(int mode);
	static ThemeMode ModeFromChoice(int choice);
	static ThemePalette PaletteFor(ThemeMode mode);

	ThemeMode GetMode() const;
	const ThemePalette& GetPalette() const;
	bool Apply(ThemeMode mode, wxWindow* root);

	// Flat chrome (Config::UI_FLAT_CHROME) swaps the stock AUI art for the
	// palette-driven flat variants in theme_art.h. Call these once when a
	// manager, toolbar or notebook is created; Apply() restyles existing ones.
	bool IsFlatChrome() const;
	void StyleAuiManager(wxAuiManager* manager) const;
	void StyleToolBar(wxAuiToolBar* toolbar) const;
	void StyleNotebook(wxAuiNotebook* notebook) const;

private:
	ThemeManager();

	ThemeMode mode;
	ThemePalette palette;
};

#endif
