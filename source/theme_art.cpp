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

#include "theme_art.h"

#include <wx/aui/framemanager.h>
#include <wx/dcclient.h>

namespace {

// Logical (DIP) metrics shared by both art providers.
constexpr int kCaptionHeight = 22;
constexpr int kPaneBorder = 1;
constexpr int kSashSize = 4;
constexpr int kPaneButtonSize = 14;
constexpr int kGripperSize = 8;
constexpr int kAccentHeight = 2;
constexpr int kHighlightRadius = 3;
constexpr int kToolPadding = 2;

int Dip(wxWindow* wnd, int value) {
	return wnd ? wnd->FromDIP(value) : value;
}

wxPen HairlinePen(wxWindow* wnd, const wxColour& colour) {
	return wxPen(colour, std::max(1, Dip(wnd, 1)));
}

} // namespace

// ============================================================================
// FlatDockArt

FlatDockArt::FlatDockArt(const ThemePalette& palette) :
	palette(palette) {
	ApplyPalette();
}

wxAuiDockArt* FlatDockArt::Clone() {
	return newd FlatDockArt(*this);
}

void FlatDockArt::ApplyPalette() {
	SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, kPaneBorder);
	SetMetric(wxAUI_DOCKART_SASH_SIZE, kSashSize);
	SetMetric(wxAUI_DOCKART_CAPTION_SIZE, kCaptionHeight);
	SetMetric(wxAUI_DOCKART_PANE_BUTTON_SIZE, kPaneButtonSize);
	SetMetric(wxAUI_DOCKART_GRIPPER_SIZE, kGripperSize);
	SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);

	SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, palette.window);
	SetColour(wxAUI_DOCKART_SASH_COLOUR, palette.window);
	SetColour(wxAUI_DOCKART_BORDER_COLOUR, palette.border);
	SetColour(wxAUI_DOCKART_GRIPPER_COLOUR, palette.mutedText);
	// Gradient colours equal the flat colour so anything still reading the
	// pair (the minimap's own caption bar) renders a solid fill.
	SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, palette.surface);
	SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, palette.surface);
	SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, palette.mutedText);
	SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, palette.control);
	SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, palette.control);
	SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, palette.text);
}

void FlatDockArt::UpdateColoursFromSystem() {
	wxAuiDefaultDockArt::UpdateColoursFromSystem();
	ApplyPalette();
}

void FlatDockArt::DrawCaption(wxDC& dc, wxWindow* window, const wxString& text, const wxRect& rect, wxAuiPaneInfo& pane) {
	const bool active = pane.HasFlag(wxAuiPaneInfo::optionActive);

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(active ? palette.control : palette.surface));
	dc.DrawRectangle(rect);

	if (active) {
		const int accent = Dip(window, kAccentHeight);
		dc.SetBrush(wxBrush(palette.accent));
		dc.DrawRectangle(rect.x, rect.y + rect.height - accent, rect.width, accent);
	}

	// Leave room for the pane buttons drawn to the right of the caption.
	int buttons = 0;
	if (pane.HasCloseButton()) {
		++buttons;
	}
	if (pane.HasMaximizeButton()) {
		++buttons;
	}
	if (pane.HasPinButton()) {
		++buttons;
	}
	const int padding = Dip(window, 6);
	const int reserved = buttons * (Dip(window, kPaneButtonSize) + padding);

	wxRect textRect(rect.x + padding, rect.y, std::max(0, rect.width - reserved - padding * 2), rect.height);
	if (textRect.width <= 0) {
		return;
	}

	dc.SetFont(m_captionFont);
	dc.SetTextForeground(active ? palette.text : palette.mutedText);

	wxCoord textWidth = 0;
	wxCoord textHeight = 0;
	dc.GetTextExtent("ABCDEFHXfgkj", &textWidth, &textHeight);

	wxDCClipper clipper(dc, textRect);
	dc.DrawText(text, textRect.x, textRect.y + (textRect.height - textHeight) / 2);
}

void FlatDockArt::DrawPaneButton(wxDC& dc, wxWindow* window, int button, int buttonState, const wxRect& rect, wxAuiPaneInfo& pane) {
	if (buttonState & wxAUI_BUTTON_STATE_HIDDEN) {
		return;
	}

	const bool active = pane.HasFlag(wxAuiPaneInfo::optionActive);
	const bool hover = (buttonState & (wxAUI_BUTTON_STATE_HOVER | wxAUI_BUTTON_STATE_PRESSED)) != 0;

	if (hover) {
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush((buttonState & wxAUI_BUTTON_STATE_PRESSED) ? palette.selection : palette.hover));
		dc.DrawRoundedRectangle(rect, Dip(window, kHighlightRadius));
	}

	const wxColour glyph = (active || hover) ? palette.text : palette.mutedText;
	dc.SetPen(HairlinePen(window, glyph));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	// Glyphs are drawn inside a square centred in the button rect.
	const int inset = Dip(window, 4);
	wxRect box = rect;
	box.Deflate(inset);
	if (box.width > box.height) {
		box.x += (box.width - box.height) / 2;
		box.width = box.height;
	} else if (box.height > box.width) {
		box.y += (box.height - box.width) / 2;
		box.height = box.width;
	}
	if (box.width <= 0) {
		return;
	}

	const int left = box.x;
	const int top = box.y;
	const int right = box.x + box.width - 1;
	const int bottom = box.y + box.height - 1;

	switch (button) {
		case wxAUI_BUTTON_CLOSE:
			dc.DrawLine(left, top, right + 1, bottom + 1);
			dc.DrawLine(left, bottom, right + 1, top - 1);
			break;
		case wxAUI_BUTTON_MAXIMIZE_RESTORE:
			if (pane.IsMaximized()) {
				// Two offset squares.
				const int step = std::max(1, box.width / 3);
				dc.DrawRectangle(left + step, top, box.width - step, box.height - step);
				dc.SetBrush(wxBrush(hover ? palette.hover : (active ? palette.control : palette.surface)));
				dc.DrawRectangle(left, top + step, box.width - step, box.height - step);
			} else {
				dc.DrawRectangle(box);
				dc.DrawLine(left, top + 1, right + 1, top + 1);
			}
			break;
		case wxAUI_BUTTON_PIN: {
			// Pin head above a needle.
			const int headHeight = std::max(2, box.height / 2);
			dc.DrawRectangle(left + 1, top, box.width - 2, headHeight);
			dc.DrawLine(left, top + headHeight, right + 1, top + headHeight);
			const int middle = left + box.width / 2;
			dc.DrawLine(middle, top + headHeight, middle, bottom + 1);
			break;
		}
		default:
			wxAuiDefaultDockArt::DrawPaneButton(dc, window, button, buttonState, rect, pane);
			break;
	}
}

void FlatDockArt::DrawGripper(wxDC& dc, wxWindow* window, const wxRect& rect, wxAuiPaneInfo& pane) {
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(palette.surface));
	dc.DrawRectangle(rect);

	const int dot = std::max(1, Dip(window, 2));
	const int gap = dot * 2;
	dc.SetBrush(wxBrush(palette.mutedText));

	if (pane.HasGripperTop()) {
		const int y = rect.y + (rect.height - dot) / 2;
		for (int x = rect.x + gap; x + dot <= rect.x + rect.width - gap; x += gap) {
			dc.DrawRectangle(x, y, dot, dot);
		}
	} else {
		const int x = rect.x + (rect.width - dot) / 2;
		for (int y = rect.y + gap; y + dot <= rect.y + rect.height - gap; y += gap) {
			dc.DrawRectangle(x, y, dot, dot);
		}
	}
}

// ============================================================================
// FlatToolBarArt

FlatToolBarArt::FlatToolBarArt(const ThemePalette& palette) :
	palette(palette) {
	ApplyPalette();
}

wxAuiToolBarArt* FlatToolBarArt::Clone() {
	return newd FlatToolBarArt(*this);
}

void FlatToolBarArt::ApplyPalette() {
	m_baseColour = palette.surface;
	m_highlightColour = palette.hover;
	m_gripperPen1 = wxPen(palette.mutedText);
	m_gripperPen2 = wxPen(palette.surface);
	m_gripperPen3 = wxPen(palette.surface);
}

void FlatToolBarArt::UpdateColoursFromSystem() {
	wxAuiGenericToolBarArt::UpdateColoursFromSystem();
	ApplyPalette();
}

void FlatToolBarArt::DrawBackground(wxDC& dc, wxWindow* WXUNUSED(wnd), const wxRect& rect) {
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(palette.surface));
	dc.DrawRectangle(rect);
}

void FlatToolBarArt::DrawPlainBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) {
	DrawBackground(dc, wnd, rect);
}

void FlatToolBarArt::DrawHighlight(wxDC& dc, wxWindow* wnd, const wxRect& rect, const wxColour& fill) const {
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(fill));
	dc.DrawRoundedRectangle(rect, Dip(wnd, kHighlightRadius));
}

void FlatToolBarArt::DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) {
	const int state = item.GetState();
	const bool disabled = (state & wxAUI_BUTTON_STATE_DISABLED) != 0;
	const bool checked = (state & wxAUI_BUTTON_STATE_CHECKED) != 0;
	const bool pressed = (state & wxAUI_BUTTON_STATE_PRESSED) != 0;
	const bool hover = (state & wxAUI_BUTTON_STATE_HOVER) != 0;

	wxRect buttonRect = rect;
	buttonRect.Deflate(std::max(1, Dip(wnd, 1)));

	if (!disabled) {
		if (pressed) {
			DrawHighlight(dc, wnd, buttonRect, palette.selection);
		} else if (checked && hover) {
			DrawHighlight(dc, wnd, buttonRect, palette.selection);
		} else if (checked) {
			DrawHighlight(dc, wnd, buttonRect, palette.selection);
			// A thin ring makes the checked state readable next to a hovered one.
			dc.SetPen(HairlinePen(wnd, palette.accent));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRoundedRectangle(buttonRect, Dip(wnd, kHighlightRadius));
		} else if (hover) {
			DrawHighlight(dc, wnd, buttonRect, palette.hover);
		}
	} else if (checked) {
		DrawHighlight(dc, wnd, buttonRect, palette.control);
	}

	// Resolves the DPI-appropriate bitmap and the disabled variant, exactly as wx does.
	const wxBitmap bitmap = item.GetCurrentBitmapFor(wnd);

	const bool hasLabel = (m_flags & wxAUI_TB_TEXT) && !item.GetLabel().empty();
	wxSize textSize;
	if (hasLabel) {
		dc.SetFont(m_font);
		textSize = dc.GetTextExtent(item.GetLabel());
	}

	const int padding = Dip(wnd, kToolPadding);
	const wxSize bitmapSize = bitmap.IsOk() ? bitmap.GetLogicalSize() : wxSize(0, 0);

	int bitmapX = rect.x + (rect.width - bitmapSize.x) / 2;
	int bitmapY = rect.y + (rect.height - bitmapSize.y) / 2;
	int textX = 0;
	int textY = 0;

	if (hasLabel) {
		if (m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM) {
			const int total = bitmapSize.y + padding + textSize.y;
			bitmapY = rect.y + (rect.height - total) / 2;
			textX = rect.x + (rect.width - textSize.x) / 2;
			textY = bitmapY + bitmapSize.y + padding;
		} else {
			const int total = bitmapSize.x + padding + textSize.x;
			bitmapX = rect.x + (rect.width - total) / 2;
			textX = bitmapX + bitmapSize.x + padding;
			textY = rect.y + (rect.height - textSize.y) / 2;
		}
	}

	if (bitmap.IsOk()) {
		dc.DrawBitmap(bitmap, bitmapX, bitmapY, true);
	}

	if (hasLabel) {
		dc.SetTextForeground(disabled ? palette.disabledText : palette.text);
		dc.DrawText(item.GetLabel(), textX, textY);
	}
}

void FlatToolBarArt::DrawSeparator(wxDC& dc, wxWindow* wnd, const wxRect& rect) {
	const bool horizontal = (m_flags & wxAUI_TB_VERTICAL) == 0;
	dc.SetPen(HairlinePen(wnd, palette.border));

	const int inset = Dip(wnd, 4);
	if (horizontal) {
		const int x = rect.x + rect.width / 2;
		dc.DrawLine(x, rect.y + inset, x, rect.y + rect.height - inset);
	} else {
		const int y = rect.y + rect.height / 2;
		dc.DrawLine(rect.x + inset, y, rect.x + rect.width - inset, y);
	}
}

void FlatToolBarArt::DrawGripper(wxDC& dc, wxWindow* wnd, const wxRect& rect) {
	const bool horizontal = (m_flags & wxAUI_TB_VERTICAL) == 0;
	const int dot = std::max(1, Dip(wnd, 2));
	const int gap = dot * 2;

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(palette.mutedText));

	if (horizontal) {
		const int x = rect.x + (rect.width - dot) / 2;
		for (int y = rect.y + gap; y + dot <= rect.y + rect.height - gap; y += gap) {
			dc.DrawRectangle(x, y, dot, dot);
		}
	} else {
		const int y = rect.y + (rect.height - dot) / 2;
		for (int x = rect.x + gap; x + dot <= rect.x + rect.width - gap; x += gap) {
			dc.DrawRectangle(x, y, dot, dot);
		}
	}
}

void FlatToolBarArt::DrawOverflowButton(wxDC& dc, wxWindow* wnd, const wxRect& rect, int state) {
	if (state & (wxAUI_BUTTON_STATE_HOVER | wxAUI_BUTTON_STATE_PRESSED)) {
		wxRect highlight = rect;
		highlight.Deflate(std::max(1, Dip(wnd, 1)));
		DrawHighlight(dc, wnd, highlight, (state & wxAUI_BUTTON_STATE_PRESSED) ? palette.selection : palette.hover);
	}

	// A small downward chevron.
	const int half = std::max(2, Dip(wnd, 3));
	const int centreX = rect.x + rect.width / 2;
	const int centreY = rect.y + rect.height / 2;
	dc.SetPen(HairlinePen(wnd, palette.text));
	dc.DrawLine(centreX - half, centreY - half / 2, centreX, centreY + half / 2);
	dc.DrawLine(centreX, centreY + half / 2, centreX + half + 1, centreY - half / 2 - 1);
}
