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

#ifndef RME_THEME_ART_H_
#define RME_THEME_ART_H_

#include <wx/aui/dockart.h>
#include <wx/aui/auibar.h>

#include "theme.h"

/*
 * Flat replacement for wxAuiDefaultDockArt: solid caption bars with a thin
 * accent line on the active pane, one-pixel borders, no gradients or bevels,
 * and vector-drawn pane buttons that follow the theme palette. All metrics
 * are logical (DIP) values; wx scales them per window.
 */
class FlatDockArt : public wxAuiDefaultDockArt {
public:
	explicit FlatDockArt(const ThemePalette& palette);

	wxAuiDockArt* Clone() override;

	void DrawCaption(wxDC& dc, wxWindow* window, const wxString& text, const wxRect& rect, wxAuiPaneInfo& pane) override;
	void DrawPaneButton(wxDC& dc, wxWindow* window, int button, int buttonState, const wxRect& rect, wxAuiPaneInfo& pane) override;
	void DrawGripper(wxDC& dc, wxWindow* window, const wxRect& rect, wxAuiPaneInfo& pane) override;

	// The default implementation resets every colour from the system theme
	// whenever the appearance changes; keep the palette instead.
	void UpdateColoursFromSystem() override;

private:
	void ApplyPalette();

	ThemePalette palette;
};

/*
 * Flat replacement for wxAuiGenericToolBarArt: solid background, rounded
 * hover / pressed / checked highlights, hairline separators and a dotted
 * gripper, all in palette colours.
 */
class FlatToolBarArt : public wxAuiGenericToolBarArt {
public:
	explicit FlatToolBarArt(const ThemePalette& palette);

	wxAuiToolBarArt* Clone() override;

	void DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) override;
	void DrawPlainBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) override;
	void DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) override;
	void DrawSeparator(wxDC& dc, wxWindow* wnd, const wxRect& rect) override;
	void DrawGripper(wxDC& dc, wxWindow* wnd, const wxRect& rect) override;
	void DrawOverflowButton(wxDC& dc, wxWindow* wnd, const wxRect& rect, int state) override;

	void UpdateColoursFromSystem() override;

private:
	void ApplyPalette();
	void DrawHighlight(wxDC& dc, wxWindow* wnd, const wxRect& rect, const wxColour& fill) const;

	ThemePalette palette;
};

#endif // RME_THEME_ART_H_
