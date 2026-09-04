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

#include "settings.h"

#include "dcbutton.h"
#include "sprites.h"
#include "gui.h"
#include "theme.h"

BEGIN_EVENT_TABLE(DCButton, wxPanel)
EVT_PAINT(DCButton::OnPaint)
EVT_LEFT_DOWN(DCButton::OnClick)
EVT_ENTER_WINDOW(DCButton::OnMouseEnter)
EVT_LEAVE_WINDOW(DCButton::OnMouseLeave)
END_EVENT_TABLE()

IMPLEMENT_DYNAMIC_CLASS(DCButton, wxPanel)

DCButton::DCButton() :
	wxPanel(nullptr, wxID_ANY, wxDefaultPosition, wxSize(36, 36)),
	type(DC_BTN_NORMAL),
	state(false),
	isHovered(false),
	size(RENDER_SIZE_16x16),
	sprite(nullptr),
	overlay(nullptr) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetSprite(0);
}

static wxSize GetDefaultButtonSize(RenderSize sz) {
	switch (sz) {
		case RENDER_SIZE_16x16:
			return wxSize(20, 20);
		case RENDER_SIZE_32x32:
			return wxSize(36, 36);
		case RENDER_SIZE_ACTUAL:
		default:
			return wxSize(68, 68);
	}
}

DCButton::DCButton(wxWindow* parent, wxWindowID id, wxPoint pos, int type, RenderSize sz, int sprite_id) :
	wxPanel(parent, id, pos, GetDefaultButtonSize(sz)),
	type(type),
	state(false),
	isHovered(false),
	size(sz),
	sprite(nullptr),
	overlay(nullptr) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(GetDefaultButtonSize(sz));
	SetMaxSize(GetDefaultButtonSize(sz));
	SetSprite(sprite_id, false); // Don't refresh during construction
}

DCButton::~DCButton() {
	////
}

void DCButton::SetButtonSize(const wxSize& newSize) {
	SetMinSize(newSize);
	SetMaxSize(newSize);
	SetInitialSize(newSize);
	SetSize(newSize);
}

void DCButton::SetSprite(int _sprid, bool refresh) {
	if (_sprid != 0) {
		sprite = g_gui.gfx.getSprite(_sprid);
	} else {
		sprite = nullptr;
	}
	if (refresh) {
		Refresh();
	}
}

void DCButton::SetOverlay(Sprite* espr) {
	overlay = espr;
	Refresh();
}

void DCButton::SetValue(bool val) {
	ASSERT(type == DC_BTN_TOGGLE);
	bool oldval = val;
	state = val;
	if (state == oldval) {
		// Cheap to change value to the old one (which is done ALOT).
		// The selection look is painted in OnPaint (tinted background and a
		// border) - no marker overlay covering the sprite anymore.
		Refresh();
	}
}

bool DCButton::GetValue() const {
	ASSERT(type == DC_BTN_TOGGLE);
	return state;
}

void DCButton::OnPaint(wxPaintEvent& event) {
	wxBufferedPaintDC pdc(this);

	// Note: wxBufferedPaintDC hands out an *uninitialised* bitmap, and this window
	// uses wxBG_STYLE_PAINT so nothing else clears it. Bailing out before the
	// background is filled would blit garbage (or nothing) over the button, so the
	// "graphics not loaded" check now sits next to the sprite drawing below.
	const ThemePalette& palette = ThemeManager::Get().GetPalette();
	const wxPen highlight_pen(palette.surface, 1, wxSOLID);
	const wxPen dark_highlight_pen(palette.hover, 1, wxSOLID);
	const wxPen light_shadow_pen(palette.border, 1, wxSOLID);
	const wxPen shadow_pen(palette.window, 1, wxSOLID);

	int size_x, size_y;
	GetClientSize(&size_x, &size_y);
	if (size_x <= 0 || size_y <= 0) {
		return;
	}

	const int bgshade = g_settings.getInteger(Config::ICON_BACKGROUND);
	const bool selected = (type == DC_BTN_TOGGLE && GetValue());

	wxColour fillColour;
	if (selected) {
		fillColour = palette.selection;
		if (isHovered) {
			fillColour = fillColour.ChangeLightness(12);
		}
	} else if (bgshade < 0) {
		fillColour = isHovered ? palette.hover : palette.control;
	} else {
		fillColour = wxColour(bgshade, bgshade, bgshade);
		if (isHovered) {
			fillColour = fillColour.ChangeLightness(10);
		}
	}

	pdc.SetBrush(wxBrush(fillColour));
	pdc.SetPen(wxPen(fillColour));
	pdc.DrawRectangle(0, 0, size_x, size_y);

	if (!selected) {
		pdc.SetPen(highlight_pen);
		pdc.DrawLine(0, 0, size_x - 1, 0);
		pdc.DrawLine(0, 1, 0, size_y - 1);
		pdc.SetPen(dark_highlight_pen);
		pdc.DrawLine(1, 1, size_x - 2, 1);
		pdc.DrawLine(1, 2, 1, size_y - 2);
		pdc.SetPen(light_shadow_pen);
		pdc.DrawLine(size_x - 2, 1, size_x - 2, size_y - 2);
		pdc.DrawLine(1, size_y - 2, size_x - 1, size_y - 2);
		pdc.SetPen(shadow_pen);
		pdc.DrawLine(size_x - 1, 0, size_x - 1, size_y - 1);
		pdc.DrawLine(0, size_y - 1, size_y, size_y - 1);
	} else {
		// With the icon selection highlight enabled, make the border clearly
		// distinct (2px, brighter and bluer than the theme selection color);
		// the tinted background above already shows through the sprite's
		// transparency.
		pdc.SetBrush(*wxTRANSPARENT_BRUSH);
		if (g_settings.getInteger(Config::USE_GUI_SELECTION_SHADOW)) {
			const wxColour& sel = palette.selection;
			const wxColour borderColor(wxMin(255, sel.Red() + 30), wxMin(255, sel.Green() + 60), wxMin(255, sel.Blue() + 120));
			pdc.SetPen(wxPen(borderColor, 1));
			pdc.DrawRectangle(0, 0, size_x, size_y);
			pdc.DrawRectangle(1, 1, size_x - 2, size_y - 2);
		} else {
			pdc.SetPen(wxPen(palette.selection.ChangeLightness(150), 1));
			pdc.DrawRectangle(0, 0, size_x, size_y);
		}
	}

	if (sprite && !g_gui.gfx.isUnloaded()) {
		int draw_width = std::max(0, size_x - 4);
		int draw_height = std::max(0, size_y - 4);
		SpriteSize sprite_size = SPRITE_SIZE_32x32;
		if (size == RENDER_SIZE_16x16) {
			sprite_size = SPRITE_SIZE_16x16;
		} else if (size == RENDER_SIZE_ACTUAL) {
			sprite_size = SPRITE_SIZE_ACTUAL;
		}
		// Draw the picture!
		sprite->DrawTo(&pdc, sprite_size, 2, 2, draw_width, draw_height);

		if (overlay && type == DC_BTN_TOGGLE && GetValue()) {
			// Use appropriate overlay size based on sprite size
			// For actual size sprites (64x64+), use 32x32 and scale it up
			SpriteSize overlay_size = sprite_size;
			if (sprite_size == SPRITE_SIZE_ACTUAL) {
				overlay_size = SPRITE_SIZE_32x32;
			}
			overlay->DrawTo(&pdc, overlay_size, 2, 2, draw_width, draw_height);
		}
	}
}

void DCButton::OnClick(wxMouseEvent& WXUNUSED(evt)) {
	wxCommandEvent event(type == DC_BTN_TOGGLE ? wxEVT_COMMAND_TOGGLEBUTTON_CLICKED : wxEVT_COMMAND_BUTTON_CLICKED, GetId());
	event.SetEventObject(this);

	if (type == DC_BTN_TOGGLE) {
		SetValue(!GetValue());
	}
	SetFocus();

	GetEventHandler()->ProcessEvent(event);
}

void DCButton::OnMouseEnter(wxMouseEvent& event) {
	isHovered = true;
	Refresh();
	event.Skip();
}

void DCButton::OnMouseLeave(wxMouseEvent& event) {
	isHovered = false;
	Refresh();
	event.Skip();
}
