//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "theme.h"
#include "theme_art.h"

#include "map_window.h"
#include "settings.h"

#include <wx/settings.h>
#include <wx/aui/aui.h>

namespace {

ThemePalette DarkPalette() {
	return {
		wxColour(30, 30, 30),
		wxColour(37, 37, 38),
		wxColour(45, 45, 48),
		wxColour(63, 63, 70),
		wxColour(220, 220, 220),
		wxColour(160, 160, 165),
		wxColour(9, 71, 113),
		wxColour(62, 62, 66),
		wxColour(120, 120, 125),
		wxColour(0, 122, 204),
	};
}

#if wxCHECK_VERSION(3, 3, 0)
wxApp::Appearance AppearanceFor(ThemeMode mode) {
	switch (mode) {
		case ThemeMode::Light:
			return wxApp::Appearance::Light;
		case ThemeMode::System:
			return wxApp::Appearance::System;
		case ThemeMode::Dark:
		default:
			return wxApp::Appearance::Dark;
	}
}
#endif

// Stock AUI chrome: keep wx's own art providers and only recolour them.
void RecolourDefaultDockArt(wxAuiDockArt* art, const ThemePalette& palette) {
	art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, palette.surface);
	art->SetColour(wxAUI_DOCKART_SASH_COLOUR, palette.control);
	art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, palette.border);
	art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, palette.surface);
	art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, palette.control);
	art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, palette.mutedText);
	art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, palette.selection);
	art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, palette.hover);
	art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, palette.text);
	art->SetColour(wxAUI_DOCKART_GRIPPER_COLOUR, palette.mutedText);
}

void ApplyPaletteToWindow(wxWindow* window, const ThemePalette& palette, bool isRoot) {
	if (dynamic_cast<MapWindow*>(window)) {
		return;
	}

	window->SetForegroundColour(palette.text);
	window->SetBackgroundColour(isRoot ? palette.window : palette.surface);

	// Style the dock manager once, from the window it manages, rather than
	// from every descendant that happens to resolve to the same manager.
	if (wxAuiManager* manager = wxAuiManager::GetManager(window)) {
		if (manager->GetManagedWindow() == window) {
			ThemeManager::Get().StyleAuiManager(manager);
		}
	}

	if (auto* toolbar = dynamic_cast<wxAuiToolBar*>(window)) {
		ThemeManager::Get().StyleToolBar(toolbar);
	}

	if (auto* notebook = dynamic_cast<wxAuiNotebook*>(window)) {
		ThemeManager::Get().StyleNotebook(notebook);
	}

	for (wxWindow* child : window->GetChildren()) {
		ApplyPaletteToWindow(child, palette, false);
	}

	if (wxAuiManager* manager = wxAuiManager::GetManager(window)) {
		if (manager->GetManagedWindow() == window) {
			manager->Update();
		}
	}
}

} // namespace

ThemeManager& ThemeManager::Get() {
	static ThemeManager manager;
	return manager;
}

ThemeMode ThemeManager::NormalizeMode(int mode) {
	switch (mode) {
		case static_cast<int>(ThemeMode::Light):
			return ThemeMode::Light;
		case static_cast<int>(ThemeMode::System):
			return ThemeMode::System;
		case static_cast<int>(ThemeMode::Dark):
		default:
			return ThemeMode::Dark;
	}
}

ThemeMode ThemeManager::ModeFromChoice(int choice) {
	return NormalizeMode(choice);
}

ThemePalette ThemeManager::PaletteFor(ThemeMode mode) {
	if (mode == ThemeMode::System) {
#if wxCHECK_VERSION(3, 3, 0)
		mode = wxSystemSettings::GetAppearance().AreAppsDark() ? ThemeMode::Dark : ThemeMode::Light;
#else
		mode = wxSystemSettings::GetAppearance().IsDark() ? ThemeMode::Dark : ThemeMode::Light;
#endif
	}

	if (mode != ThemeMode::Light) {
		return DarkPalette();
	}

	return {
		wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW),
		wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE),
		wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE),
		wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW),
		wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT),
		wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT),
		wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT),
		wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT),
		wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT),
		wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT),
	};
}

ThemeMode ThemeManager::GetMode() const {
	return mode;
}

const ThemePalette& ThemeManager::GetPalette() const {
	return palette;
}

bool ThemeManager::IsFlatChrome() const {
	return g_settings.getBoolean(Config::UI_FLAT_CHROME);
}

void ThemeManager::StyleAuiManager(wxAuiManager* manager) const {
	if (!manager) {
		return;
	}
	if (IsFlatChrome()) {
		manager->SetArtProvider(newd FlatDockArt(palette));
		return;
	}
	if (!dynamic_cast<FlatDockArt*>(manager->GetArtProvider())) {
		// Still the stock art: recolour in place.
		RecolourDefaultDockArt(manager->GetArtProvider(), palette);
		return;
	}
	manager->SetArtProvider(newd wxAuiDefaultDockArt());
	RecolourDefaultDockArt(manager->GetArtProvider(), palette);
}

void ThemeManager::StyleToolBar(wxAuiToolBar* toolbar) const {
	if (!toolbar) {
		return;
	}
	if (IsFlatChrome()) {
		toolbar->SetArtProvider(newd FlatToolBarArt(palette));
	} else if (dynamic_cast<FlatToolBarArt*>(toolbar->GetArtProvider())) {
		toolbar->SetArtProvider(newd wxAuiGenericToolBarArt());
	} else if (wxAuiToolBarArt* art = toolbar->GetArtProvider()) {
		art->UpdateColoursFromSystem();
	}
	toolbar->Refresh();
}

void ThemeManager::StyleNotebook(wxAuiNotebook* notebook) const {
	if (!notebook) {
		return;
	}
#if wxCHECK_VERSION(3, 3, 0)
	if (IsFlatChrome()) {
		if (!dynamic_cast<wxAuiFlatTabArt*>(notebook->GetArtProvider())) {
			notebook->SetArtProvider(newd wxAuiFlatTabArt());
		}
		// wxAuiFlatTabArt takes its backgrounds from the system appearance; the
		// two colour setters control the tab *text* (normal and current tab).
		if (wxAuiTabArt* art = notebook->GetArtProvider()) {
			art->SetColour(palette.mutedText);
			art->SetActiveColour(palette.text);
		}
		notebook->Refresh();
		return;
	}
	if (dynamic_cast<wxAuiFlatTabArt*>(notebook->GetArtProvider())) {
		notebook->SetArtProvider(newd wxAuiGenericTabArt());
	}
#endif
	if (wxAuiTabArt* art = notebook->GetArtProvider()) {
		art->SetColour(palette.surface);
		art->SetActiveColour(palette.control);
	}
	notebook->Refresh();
}

bool ThemeManager::Apply(ThemeMode newMode, wxWindow* root) {
	const ThemeMode requestedMode = NormalizeMode(static_cast<int>(newMode));
#if wxCHECK_VERSION(3, 3, 0)
	if (wxTheApp && wxTheApp->SetAppearance(AppearanceFor(requestedMode)) == wxApp::AppearanceResult::Failure) {
		return false;
	}
#endif

	const ThemePalette requestedPalette = PaletteFor(requestedMode);
	mode = requestedMode;
	palette = requestedPalette;

	if (!root) {
		return true;
	}

	ApplyPaletteToWindow(root, palette, true);

	root->Layout();
	root->Refresh();
	return true;
}

ThemeManager::ThemeManager() : mode(ThemeMode::Dark), palette(PaletteFor(ThemeMode::Dark)) {
}
