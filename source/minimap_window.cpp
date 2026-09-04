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

#include "graphics.h"
#include "editor.h"
#include "map.h"

#include "gui.h"
#include "gui_ids.h"
#include "live_socket.h"
#include "map_display.h"
#include "minimap_window.h"
#include <algorithm>
#include <wx/aui/dockart.h>
#include <wx/aui/framemanager.h>
#include <wx/rawbmp.h>

namespace {

void DrawLiveUserMarkers(wxDC& dc, Editor& editor, int start_x, int start_y, int floor, int window_width, int window_height, double zoom) {
	if (!editor.IsLive()) {
		return;
	}

	LiveSocket& live = editor.GetLive();
	const std::vector<LiveCursor> cursorList = live.getCursorList();
	if (cursorList.empty()) {
		return;
	}

	const std::vector<LiveParticipant>& participants = live.getParticipantList();
	const uint32_t ownClientId = live.getOwnClientId();

	wxFont label_font(*wxSWISS_FONT);
	label_font.SetPointSize(8);
	label_font.SetWeight(wxFONTWEIGHT_BOLD);
	dc.SetFont(label_font);

	for (const LiveCursor& cursor : cursorList) {
		if (cursor.id == ownClientId) {
			continue;
		}
		if (!isLiveMarkerVisibleOnFloor(floor, cursor.pos.z)) {
			continue;
		}

		const int screen_x = static_cast<int>((cursor.pos.x - start_x) * zoom);
		const int screen_y = static_cast<int>((cursor.pos.y - start_y) * zoom);
		if (screen_x < 0 || screen_y < 0 || screen_x >= window_width || screen_y >= window_height) {
			continue;
		}

		wxColor color = cursor.color;
		wxString user_name;
		for (const LiveParticipant& participant : participants) {
			if (participant.id == cursor.id) {
				color = participant.color;
				user_name = participant.name;
				break;
			}
		}

		constexpr int marker_radius = 4;

		dc.SetPen(wxPen(*wxBLACK, 2));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawCircle(screen_x, screen_y, marker_radius + 1);

		dc.SetPen(wxPen(*wxWHITE, 2));
		dc.DrawCircle(screen_x, screen_y, marker_radius);

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(color));
		dc.DrawCircle(screen_x, screen_y, marker_radius - 1);

		if (user_name.empty()) {
			continue;
		}

		const wxSize text_size = dc.GetTextExtent(user_name);
		const int pad_x = 3;
		const int pad_y = 1;

		int label_x = screen_x + marker_radius + 3;
		int label_y = screen_y - text_size.y / 2;

		if (label_x + text_size.x + pad_x * 2 > window_width) {
			label_x = screen_x - marker_radius - 3 - text_size.x - pad_x * 2;
		}
		if (label_y < 0) {
			label_y = screen_y + marker_radius + 2;
		}
		if (label_y + text_size.y + pad_y * 2 > window_height) {
			label_y = screen_y - marker_radius - text_size.y - 2;
		}

		label_x = std::max(0, label_x);
		label_y = std::max(0, label_y);

		const wxRect label_bg(label_x, label_y, text_size.x + pad_x * 2, text_size.y + pad_y * 2);
		dc.SetPen(wxPen(color, 1));
		dc.SetBrush(wxBrush(wxColor(16, 16, 16)));
		dc.DrawRectangle(label_bg);

		dc.SetTextForeground(*wxWHITE);
		dc.DrawText(user_name, label_x + pad_x, label_y + pad_y);
	}
}

} // namespace

BEGIN_EVENT_TABLE(MinimapCanvas, wxPanel)
EVT_LEFT_DOWN(MinimapCanvas::OnMouseClick)
EVT_LEFT_DCLICK(MinimapCanvas::OnMouseDoubleClick)
EVT_MIDDLE_DOWN(MinimapCanvas::OnMouseMiddleDown)
EVT_MIDDLE_UP(MinimapCanvas::OnMouseMiddleUp)
EVT_MOTION(MinimapCanvas::OnMouseMove)
EVT_MOUSE_CAPTURE_LOST(MinimapCanvas::OnMouseCaptureLost)
EVT_MOUSEWHEEL(MinimapCanvas::OnMouseWheel)
EVT_SIZE(MinimapCanvas::OnSize)
EVT_PAINT(MinimapCanvas::OnPaint)
EVT_ERASE_BACKGROUND(MinimapCanvas::OnEraseBackground)
EVT_TIMER(wxID_ANY, MinimapCanvas::OnDelayedUpdate)
EVT_KEY_DOWN(MinimapCanvas::OnKey)
END_EVENT_TABLE()

MinimapCanvas::MinimapCanvas(wxWindow* parent) :
    wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
    update_timer(this),
    last_start_x(0),
    last_start_y(0),
    cached_start_x(-1),
    cached_start_y(-1),
    cached_floor(-1),
    cached_width(0),
    cached_height(0),
    cached_zoom(0.0),
    cached_show_all_floors(false),
    bitmap_dirty(true),
    panning(false),
    pan_offset_x(0),
    pan_offset_y(0),
    pan_drag_last_x(0),
    pan_drag_last_y(0),
    last_canvas_center_x(-1),
    last_canvas_center_y(-1),
    minimap_zoom(MINIMAP_ZOOM_DEFAULT),
    last_paint_time(std::chrono::steady_clock::now() - std::chrono::milliseconds(80)) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

MinimapCanvas::~MinimapCanvas() {
}

void MinimapCanvas::ResetView() {
	minimap_zoom = MINIMAP_ZOOM_DEFAULT;
	pan_offset_x = 0;
	pan_offset_y = 0;
	MarkDirty();
}

void MinimapCanvas::ApplyZoomDelta(double diff, int anchor_x, int anchor_y) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	const double old_zoom = minimap_zoom;
	minimap_zoom += diff;
	minimap_zoom = std::clamp(minimap_zoom, MINIMAP_ZOOM_MIN, MINIMAP_ZOOM_MAX);
	if (minimap_zoom == old_zoom) {
		return;
	}

	const double map_x = last_start_x + anchor_x / old_zoom;
	const double map_y = last_start_y + anchor_y / old_zoom;

	MapCanvas* canvas = g_gui.GetCurrentMapTab()->GetCanvas();
	int center_x, center_y;
	canvas->GetScreenCenter(&center_x, &center_y);

	const wxSize client_size = GetClientSize();
	const int window_width = client_size.GetWidth();
	const int window_height = client_size.GetHeight();
	const double tiles_wide = std::max(1.0, window_width / minimap_zoom);
	const double tiles_high = std::max(1.0, window_height / minimap_zoom);

	const double new_start_x = map_x - anchor_x / minimap_zoom;
	const double new_start_y = map_y - anchor_y / minimap_zoom;

	pan_offset_x = static_cast<int>(new_start_x + tiles_wide / 2.0) - center_x;
	pan_offset_y = static_cast<int>(new_start_y + tiles_high / 2.0) - center_y;

	MarkDirty();
	Refresh();
}

void MinimapCanvas::ComputeViewBounds(Editor& editor, MapCanvas* canvas, int window_width, int window_height, int& start_x, int& start_y, int& end_x, int& end_y) {
	int center_x, center_y;
	canvas->GetScreenCenter(&center_x, &center_y);

	if (center_x != last_canvas_center_x || center_y != last_canvas_center_y) {
		pan_offset_x = 0;
		pan_offset_y = 0;
		last_canvas_center_x = center_x;
		last_canvas_center_y = center_y;
	}

	center_x += pan_offset_x;
	center_y += pan_offset_y;

	const int tiles_wide = std::max(1, static_cast<int>(window_width / minimap_zoom));
	const int tiles_high = std::max(1, static_cast<int>(window_height / minimap_zoom));

	start_x = center_x - tiles_wide / 2;
	start_y = center_y - tiles_high / 2;
	end_x = start_x + tiles_wide;
	end_y = start_y + tiles_high;

	if (start_x < 0) {
		start_x = 0;
		end_x = tiles_wide;
	} else if (end_x > editor.map.getWidth()) {
		start_x = editor.map.getWidth() - tiles_wide;
		end_x = editor.map.getWidth();
	}
	if (start_y < 0) {
		start_y = 0;
		end_y = tiles_high;
	} else if (end_y > editor.map.getHeight()) {
		start_y = editor.map.getHeight() - tiles_high;
		end_y = editor.map.getHeight();
	}

	start_x = max(start_x, 0);
	start_y = max(start_y, 0);
	end_x = min(end_x, editor.map.getWidth());
	end_y = min(end_y, editor.map.getHeight());
}

void MinimapCanvas::OnSize(wxSizeEvent& event) {
	MarkDirty();
	Refresh();
	event.Skip();
}

void MinimapCanvas::DelayedUpdate() {
	bitmap_dirty = true;

	constexpr int kDefaultFastIntervalMs = 40;
	constexpr int kMinTimerIntervalMs = 16;

	const int configured_delay = g_settings.getInteger(Config::MINIMAP_UPDATE_DELAY);
	int fast_interval = kDefaultFastIntervalMs;
	if (configured_delay > 0) {
		fast_interval = std::min(fast_interval, configured_delay);
	}
	fast_interval = std::max(fast_interval, kMinTimerIntervalMs);

	const auto now = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_paint_time).count();

	if (elapsed >= fast_interval) {
		if (update_timer.IsRunning()) {
			update_timer.Stop();
		}
		Refresh();
		return;
	}

	int wait_for = static_cast<int>(fast_interval - elapsed);
	if (configured_delay > 0) {
		wait_for = std::min(wait_for, configured_delay);
	}
	wait_for = std::max(wait_for, kMinTimerIntervalMs);
	update_timer.Start(wait_for, true);
}

void MinimapCanvas::MarkDirty() {
	bitmap_dirty = true;
}

void MinimapCanvas::OnDelayedUpdate(wxTimerEvent& event) {
	Refresh();
}

void MinimapCanvas::OnPaint(wxPaintEvent& event) {
	wxBufferedPaintDC pdc(this);

	pdc.SetBackground(*wxBLACK_BRUSH);
	pdc.Clear();

	if (!g_gui.IsEditorOpen()) {
		return;
	}
	Editor& editor = *g_gui.GetCurrentEditor();

	const wxSize client_size = GetClientSize();
	const int window_width = client_size.GetWidth();
	const int window_height = client_size.GetHeight();
	if (window_width <= 0 || window_height <= 0) {
		return;
	}

	MapCanvas* canvas = g_gui.GetCurrentMapTab()->GetCanvas();

	int start_x, start_y;
	int end_x, end_y;
	ComputeViewBounds(editor, canvas, window_width, window_height, start_x, start_y, end_x, end_y);

	last_start_x = start_x;
	last_start_y = start_y;

	int floor = g_gui.GetCurrentFloor();
	bool show_all_floors = g_settings.getBoolean(Config::SHOW_ALL_FLOORS);

	int floor_start, floor_end;
	if (show_all_floors && floor <= GROUND_LAYER) {
		floor_start = GROUND_LAYER;
		floor_end   = 0;
	} else {
		floor_start = floor;
		floor_end   = floor;
	}

	if (g_gui.IsRenderingEnabled()) {
		bool needs_rebuild = bitmap_dirty || !minimap_bitmap.IsOk()
			|| cached_floor != floor
			|| cached_show_all_floors != show_all_floors
			|| cached_start_x != start_x || cached_start_y != start_y
			|| cached_width != window_width || cached_height != window_height
			|| cached_zoom != minimap_zoom;

		if (needs_rebuild) {
			if (!minimap_bitmap.IsOk() || minimap_bitmap.GetWidth() != window_width || minimap_bitmap.GetHeight() != window_height) {
				minimap_bitmap = wxBitmap(window_width, window_height, 32);
			}

			wxAlphaPixelData pixel_data(minimap_bitmap);
			if (pixel_data) {
				wxAlphaPixelData::Iterator pixel_it(pixel_data);
				for (int window_y = 0; window_y < window_height; ++window_y) {
					wxAlphaPixelData::Iterator row_start = pixel_it;
					int map_y = start_y + static_cast<int>(window_y / minimap_zoom);
					for (int window_x = 0; window_x < window_width; ++window_x, ++pixel_it) {
						int map_x = start_x + static_cast<int>(window_x / minimap_zoom);
						uint8_t color_index = 0;
						if (map_x >= 0 && map_y >= 0 && map_x < editor.map.getWidth() && map_y < editor.map.getHeight()) {
							for (int z = floor_start; z >= floor_end; --z) {
								if (Tile* tile = editor.map.getTile(map_x, map_y, z)) {
									uint8_t c = tile->getMiniMapColor();
									if (c != 0) {
										color_index = c;
									}
								}
							}
						}

						const RGBQuad& color = minimap_color[color_index];
						pixel_it.Red() = color.red;
						pixel_it.Green() = color.green;
						pixel_it.Blue() = color.blue;
						pixel_it.Alpha() = 0xFF;
					}
					pixel_it = row_start;
					pixel_it.OffsetY(pixel_data, 1);
				}
				cached_start_x = start_x;
				cached_start_y = start_y;
				cached_floor = floor;
				cached_show_all_floors = show_all_floors;
				cached_width = window_width;
				cached_height = window_height;
				cached_zoom = minimap_zoom;
				bitmap_dirty = false;
			} else {
				bitmap_dirty = true;
			}
		}

		if (minimap_bitmap.IsOk()) {
			pdc.DrawBitmap(minimap_bitmap, 0, 0, false);
		}

		DrawLiveUserMarkers(pdc, editor, start_x, start_y, floor, window_width, window_height, minimap_zoom);

		if (g_settings.getInteger(Config::MINIMAP_VIEW_BOX)) {
			pdc.SetPen(*wxWHITE_PEN);
			pdc.SetBrush(*wxTRANSPARENT_BRUSH);

			int screensize_x, screensize_y;
			int view_scroll_x, view_scroll_y;

			canvas->GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

			int view_start_x, view_start_y;
			int view_end_x, view_end_y;

			int tile_size = int(TileSize / canvas->GetZoom());

			int floor_offset = (floor > GROUND_LAYER ? 0 : (GROUND_LAYER - floor));

			view_start_x = view_scroll_x / TileSize + floor_offset;
			view_start_y = view_scroll_y / TileSize + floor_offset;

			view_end_x = view_start_x + screensize_x / tile_size + 1;
			view_end_y = view_start_y + screensize_y / tile_size + 1;

			const int box_x = static_cast<int>((view_start_x - start_x) * minimap_zoom);
			const int box_y = static_cast<int>((view_start_y - start_y) * minimap_zoom);
			const int box_w = static_cast<int>((view_end_x - view_start_x) * minimap_zoom);
			const int box_h = static_cast<int>((view_end_y - view_start_y) * minimap_zoom);

			pdc.DrawRectangle(box_x, box_y, box_w, box_h);
		}
	}

	last_paint_time = std::chrono::steady_clock::now();
}

void MinimapCanvas::OnMouseClick(wxMouseEvent& event) {
	if (!g_gui.IsEditorOpen() || event.GetClickCount() > 1) {
		return;
	}
	int new_map_x = last_start_x + static_cast<int>(event.GetX() / minimap_zoom);
	int new_map_y = last_start_y + static_cast<int>(event.GetY() / minimap_zoom);
	g_gui.SetScreenCenterPosition(Position(new_map_x, new_map_y, g_gui.GetCurrentFloor()));
	Refresh();
	g_gui.RefreshView();
}

void MinimapCanvas::OnMouseDoubleClick(wxMouseEvent& event) {
	ResetView();
	Refresh();
	event.Skip();
}

void MinimapCanvas::OnMouseWheel(wxMouseEvent& event) {
	if (!g_gui.IsEditorOpen() || !g_settings.getInteger(Config::MINIMAP_SCROLL_ZOOM)) {
		return;
	}

	const double diff = -event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 320.0;
	ApplyZoomDelta(diff, event.GetX(), event.GetY());
}

void MinimapCanvas::OnMouseMiddleDown(wxMouseEvent& event) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}
	panning = true;
	pan_drag_last_x = event.GetX();
	pan_drag_last_y = event.GetY();
	CaptureMouse();
}

void MinimapCanvas::OnMouseMiddleUp(wxMouseEvent& event) {
	if (panning) {
		panning = false;
		if (HasCapture()) {
			ReleaseMouse();
		}
	}
}

void MinimapCanvas::OnMouseMove(wxMouseEvent& event) {
	if (!panning || !g_gui.IsEditorOpen()) {
		return;
	}

	const int dx = event.GetX() - pan_drag_last_x;
	const int dy = event.GetY() - pan_drag_last_y;
	if (dx == 0 && dy == 0) {
		return;
	}

	pan_offset_x -= static_cast<int>(dx / minimap_zoom);
	pan_offset_y -= static_cast<int>(dy / minimap_zoom);
	pan_drag_last_x = event.GetX();
	pan_drag_last_y = event.GetY();

	MarkDirty();
	Refresh();
}

void MinimapCanvas::OnMouseCaptureLost(wxMouseCaptureLostEvent& event) {
	panning = false;
}

void MinimapCanvas::OnKey(wxKeyEvent& event) {
	// Plain 'M' (no modifiers) is the global "toggle minimap window" menu
	// accelerator. While the minimap is floating (a separate top-level
	// frame), the main frame's accelerator table never sees it, so it
	// reaches this handler instead and would get forwarded to the map
	// canvas, which just Skip()s it back up looking for an accelerator --
	// round-tripping the very shortcut that's manipulating this window
	// while it has focus. Swallow it here instead of forwarding it.
	const int code = event.GetKeyCode();
	if ((code == 'M' || code == 'm') && !event.HasModifiers() && !event.ShiftDown()) {
		return;
	}

	if (g_gui.GetCurrentTab() != nullptr) {
		g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
	}
}

BEGIN_EVENT_TABLE(MinimapCaptionBar, wxPanel)
EVT_PAINT(MinimapCaptionBar::OnPaint)
EVT_SIZE(MinimapCaptionBar::OnSize)
EVT_LEFT_DOWN(MinimapCaptionBar::OnLeftDown)
END_EVENT_TABLE()

MinimapCaptionBar::MinimapCaptionBar(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
	pane_active(true),
	pane_floating(false) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

MinimapCaptionBar::~MinimapCaptionBar() {
}

int MinimapCaptionBar::GetCaptionHeight() const {
	if (!g_gui.aui_manager) {
		return FromDIP(22);
	}
	wxAuiDockArt* art = g_gui.aui_manager->GetArtProvider();
	if (!art) {
		return FromDIP(22);
	}
#if wxCHECK_VERSION(3, 3, 0)
	return art->GetMetricForWindow(wxAUI_DOCKART_CAPTION_SIZE, const_cast<MinimapCaptionBar*>(this));
#else
	return art->GetMetric(wxAUI_DOCKART_CAPTION_SIZE);
#endif
}

void MinimapCaptionBar::UpdateChrome(bool floating, bool active) {
	// wxEVT_AUI_RENDER fires continuously (effectively every idle/render
	// cycle) while ANY pane is floating, and MinimapWindow::OnAuiPaneEvent
	// calls UpdateCaptionChrome() -> here on every one of those. Calling
	// Refresh() unconditionally turned that into a self-sustaining loop
	// (render event -> Refresh -> repaint -> more render events -> ...)
	// that pegs the message loop just from the minimap being floated, with
	// no user interaction needed -- starving actual input processing and
	// making brush painting feel laggy. Only repaint when something
	// actually changed.
	if (floating == pane_floating && active == pane_active) {
		return;
	}
	pane_floating = floating;
	pane_active = active;
	Refresh();
}

void MinimapCaptionBar::OnSize(wxSizeEvent& event) {
	Refresh();
	event.Skip();
}

void MinimapCaptionBar::OnLeftDown(wxMouseEvent& event) {
	if (MinimapWindow* owner = dynamic_cast<MinimapWindow*>(GetParent())) {
		owner->StartCaptionDrag(event);
	}
}

void MinimapCaptionBar::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	wxColour top_colour(120, 120, 120);
	wxColour bottom_colour(90, 90, 90);

	if (g_gui.aui_manager) {
		wxAuiDockArt* art = g_gui.aui_manager->GetArtProvider();
		if (art) {
			if (pane_active) {
				top_colour = art->GetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR);
				bottom_colour = art->GetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR);
			} else {
				top_colour = art->GetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR);
				bottom_colour = art->GetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR);
			}
		}
	}

	const wxSize size = GetClientSize();
	wxRect rect(0, 0, size.GetWidth(), size.GetHeight());
	dc.GradientFillLinear(rect, top_colour, bottom_colour, wxSOUTH);
}

BEGIN_EVENT_TABLE(MinimapWindow, wxPanel)
EVT_BUTTON(MINIMAP_CTRL_ZOOM_IN, MinimapWindow::OnZoomIn)
EVT_BUTTON(MINIMAP_CTRL_ZOOM_OUT, MinimapWindow::OnZoomOut)
EVT_BUTTON(MINIMAP_CTRL_CLOSE, MinimapWindow::OnCaptionClose)
EVT_CHECKBOX(MINIMAP_CTRL_SCROLL_ZOOM, MinimapWindow::OnScrollZoomToggle)
EVT_CLOSE(MinimapWindow::OnClose)
EVT_SIZE(MinimapWindow::OnSize)
EVT_MOTION(MinimapWindow::OnCaptionDragMotion)
EVT_LEFT_UP(MinimapWindow::OnCaptionDragEnd)
EVT_MOUSE_CAPTURE_LOST(MinimapWindow::OnCaptionCaptureLost)
EVT_KEY_DOWN(MinimapWindow::OnKey)
END_EVENT_TABLE()

MinimapWindow::MinimapWindow(wxWindow* parent) :
    wxPanel(parent, wxID_ANY, wxDefaultPosition, wxWindow::FromDIP(wxSize(205, 150), parent)),
    aui_event_source(nullptr),
    caption_bar(nullptr),
    canvas(nullptr),
    title_label(nullptr),
    scroll_zoom_checkbox(nullptr),
    zoom_out_button(nullptr),
    zoom_in_button(nullptr),
    close_button(nullptr),
    last_floating_state(false),
    last_active_state(false),
    caption_drag_pending(false),
    caption_drag_start_screen(0, 0),
    caption_drag_offset(0, 0) {
	caption_bar = newd MinimapCaptionBar(this);

	wxBoxSizer* caption_sizer = newd wxBoxSizer(wxHORIZONTAL);

	wxFont title_font = caption_bar->GetFont();
	title_font.SetWeight(wxFONTWEIGHT_BOLD);
	title_label = newd wxStaticText(caption_bar, wxID_ANY, "Minimap");
	title_label->SetFont(title_font);
	title_label->Bind(wxEVT_LEFT_DOWN, &MinimapWindow::StartCaptionDrag, this);
	caption_sizer->Add(title_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);

	scroll_zoom_checkbox = newd wxCheckBox(caption_bar, MINIMAP_CTRL_SCROLL_ZOOM, "Scroll zoom");
	scroll_zoom_checkbox->SetValue(g_settings.getInteger(Config::MINIMAP_SCROLL_ZOOM) != 0);
	caption_sizer->Add(scroll_zoom_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
	caption_sizer->AddStretchSpacer(1);

	const wxSize button_size(caption_bar->FromDIP(20), caption_bar->FromDIP(20));
	zoom_out_button = newd wxButton(caption_bar, MINIMAP_CTRL_ZOOM_OUT, "-", wxDefaultPosition, button_size);
	zoom_in_button = newd wxButton(caption_bar, MINIMAP_CTRL_ZOOM_IN, "+", wxDefaultPosition, button_size);
	close_button = newd wxButton(caption_bar, MINIMAP_CTRL_CLOSE, wxString::FromUTF8("\xC3\x97"), wxDefaultPosition, button_size);
	caption_sizer->Add(zoom_out_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 1);
	caption_sizer->Add(zoom_in_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 1);
	caption_sizer->Add(close_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);

	caption_bar->SetSizer(caption_sizer);
	const int caption_height = caption_bar->GetCaptionHeight();
	caption_bar->SetMinSize(wxSize(-1, caption_height));

	wxBoxSizer* root_sizer = newd wxBoxSizer(wxVERTICAL);
	root_sizer->Add(caption_bar, 0, wxEXPAND);

	canvas = newd MinimapCanvas(this);
	root_sizer->Add(canvas, 1, wxEXPAND);

	SetSizer(root_sizer);

	if (parent) {
		aui_event_source = parent;
		parent->Bind(wxEVT_AUI_PANE_ACTIVATED, &MinimapWindow::OnAuiPaneEvent, this);
		parent->Bind(wxEVT_AUI_RENDER, &MinimapWindow::OnAuiPaneEvent, this);
		parent->Bind(wxEVT_AUI_PANE_CLOSE, &MinimapWindow::OnAuiPaneClose, this);
	}

	UpdateCaptionChrome();
}

MinimapWindow::~MinimapWindow() {
	if (aui_event_source) {
		aui_event_source->Unbind(wxEVT_AUI_PANE_ACTIVATED, &MinimapWindow::OnAuiPaneEvent, this);
		aui_event_source->Unbind(wxEVT_AUI_RENDER, &MinimapWindow::OnAuiPaneEvent, this);
		aui_event_source->Unbind(wxEVT_AUI_PANE_CLOSE, &MinimapWindow::OnAuiPaneClose, this);
	}
}

bool MinimapWindow::IsPaneFloating() const {
	if (!g_gui.aui_manager) {
		return false;
	}
	wxAuiPaneInfo& pane = g_gui.aui_manager->GetPane(const_cast<MinimapWindow*>(this));
	return pane.IsOk() && pane.IsFloating();
}

void MinimapWindow::StartCaptionDrag(wxMouseEvent& event) {
	if (!g_gui.aui_manager) {
		return;
	}

	wxAuiPaneInfo& pane = g_gui.aui_manager->GetPane(this);
	if (!pane.IsOk() || !pane.IsMovable()) {
		return;
	}

	if (!pane.IsFloating() && !pane.IsFloatable()) {
		return;
	}

	// Defer the actual AUI pane drag until the mouse has moved past the drag
	// threshold, for both the docked and the already-floating case. Starting
	// wxAuiManager::StartPaneDrag() unconditionally on left-down (as used to
	// happen for the floating case) fires a zero-movement AUI drag session
	// whenever a plain click lands on the caption bar (e.g. clicking where
	// the close button would be), which can leave AUI's drag/hint state
	// stuck and the app increasingly laggy until restarted.
	caption_drag_offset = ScreenToClient(wxGetMousePosition());
	caption_drag_pending = true;
	caption_drag_start_screen = wxGetMousePosition();
	if (!HasCapture()) {
		CaptureMouse();
	}
}

void MinimapWindow::OnCaptionDragMotion(wxMouseEvent& event) {
	if (!caption_drag_pending || !g_gui.aui_manager) {
		event.Skip();
		return;
	}

	const wxPoint now = wxGetMousePosition();
	const int drag_x_threshold = wxSystemSettings::GetMetric(wxSYS_DRAG_X, this);
	const int drag_y_threshold = wxSystemSettings::GetMetric(wxSYS_DRAG_Y, this);

	if (abs(now.x - caption_drag_start_screen.x) <= drag_x_threshold
		&& abs(now.y - caption_drag_start_screen.y) <= drag_y_threshold) {
		return;
	}

	caption_drag_pending = false;
	if (HasCapture()) {
		ReleaseMouse();
	}

	wxAuiPaneInfo& pane = g_gui.aui_manager->GetPane(this);
	if (!pane.IsOk()) {
		return;
	}

	wxPoint drag_offset = caption_drag_offset;

	if (!pane.IsFloating()) {
		pane.floating_pos = wxPoint(now.x - caption_drag_offset.x, now.y - caption_drag_offset.y);

		if (pane.IsMaximized()) {
			g_gui.aui_manager->RestorePane(pane);
		}
		pane.Float();
		g_gui.aui_manager->Update();
		UpdateCaptionChrome();

		if (pane.frame) {
			const wxSize frame_size = pane.frame->GetSize();
			if (frame_size.x <= drag_offset.x) {
				drag_offset.x = pane.frame->FromDIP(30);
			}
		}
	}

	g_gui.aui_manager->StartPaneDrag(this, drag_offset);
}

void MinimapWindow::OnCaptionDragEnd(wxMouseEvent& event) {
	if (caption_drag_pending) {
		caption_drag_pending = false;
		if (HasCapture()) {
			ReleaseMouse();
		}
	}
	event.Skip();
}

void MinimapWindow::OnCaptionCaptureLost(wxMouseCaptureLostEvent& event) {
	caption_drag_pending = false;
}

void MinimapWindow::UpdateCaptionChrome() {
	const bool floating = IsPaneFloating();
	bool active = true;
	if (g_gui.aui_manager) {
		wxAuiPaneInfo& pane = g_gui.aui_manager->GetPane(this);
		if (pane.IsOk()) {
			active = pane.HasFlag(wxAuiPaneInfo::optionActive);
		}
	}

	// wxEVT_AUI_RENDER (which drives most calls to this function) fires
	// continuously while any pane is floating. Bail out immediately when
	// nothing changed since the last call instead of re-applying colours
	// and pushing another Refresh() every single time -- otherwise this
	// alone is enough to keep the message loop busy just from floating the
	// minimap, with no user interaction at all.
	if (floating == last_floating_state && active == last_active_state) {
		return;
	}

	if (caption_bar) {
		caption_bar->UpdateChrome(floating, active);
	}

	if (title_label) {
		wxColour text_colour(*wxWHITE);
		if (g_gui.aui_manager) {
			wxAuiDockArt* art = g_gui.aui_manager->GetArtProvider();
			if (art) {
				text_colour = active ? art->GetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR)
				                     : art->GetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR);
			}
		}
		title_label->SetForegroundColour(text_colour);
	}

	last_active_state = active;

	if (floating != last_floating_state) {
		if (title_label) {
			title_label->Show(!floating);
		}
		// Keep the close button visible and clickable even while floating.
		// It used to be hidden here, which meant a click over its former
		// position fell through to the caption bar's StartCaptionDrag
		// instead of closing the pane -- the "X doesn't work while
		// floating" bug -- and could kick off a zero-movement AUI pane
		// drag that left the minimap laggy until the app was restarted.
		if (caption_bar) {
			caption_bar->Layout();
		}
		last_floating_state = floating;
	}
}

void MinimapWindow::OnSize(wxSizeEvent& event) {
	UpdateCaptionChrome();
	event.Skip();
}

void MinimapWindow::OnAuiPaneEvent(wxAuiManagerEvent& event) {
	if (event.GetPane() && event.GetPane()->window == this) {
		UpdateCaptionChrome();
	} else if (event.GetEventType() == wxEVT_AUI_RENDER) {
		UpdateCaptionChrome();
	}
}

void MinimapWindow::OnAuiPaneClose(wxAuiManagerEvent& event) {
	if (!event.GetPane() || event.GetPane()->window != this) {
		return;
	}

	// The pane's CloseButton(false) flag only disables AUI's own drawn
	// close button; the floating frame's native OS title-bar close (X)
	// still fires this event. Left to AUI's default handling, closing a
	// floating pane this way can leave the wxAuiPaneInfo bookkeeping out
	// of sync with the actual window (still marked shown/floating with no
	// live frame behind it), and every subsequent per-stroke minimap
	// update while painting then does increasingly expensive/failing work
	// against that stale pane -- which is what made painting feel laggy
	// after using this close button. Veto AUI's default handling and route
	// it through the same GUI::HideMinimap() path as our own close button.
	event.Veto();
	g_gui.HideMinimap();
}

void MinimapWindow::DelayedUpdate() {
	if (canvas) {
		canvas->DelayedUpdate();
	}
}

void MinimapWindow::MarkDirty() {
	if (canvas) {
		canvas->MarkDirty();
	}
}

void MinimapWindow::RefreshCanvas() {
	if (canvas) {
		canvas->Refresh();
	}
}

void MinimapWindow::OnClose(wxCloseEvent&) {
	g_gui.DestroyMinimap();
}

void MinimapWindow::OnZoomIn(wxCommandEvent& event) {
	if (!canvas) {
		return;
	}
	const wxSize size = canvas->GetClientSize();
	const double step = g_settings.getFloat(Config::ZOOM_SPEED) / 8.0;
	canvas->ApplyZoomDelta(step, size.GetWidth() / 2, size.GetHeight() / 2);
}

void MinimapWindow::OnZoomOut(wxCommandEvent& event) {
	if (!canvas) {
		return;
	}
	const wxSize size = canvas->GetClientSize();
	const double step = g_settings.getFloat(Config::ZOOM_SPEED) / 8.0;
	canvas->ApplyZoomDelta(-step, size.GetWidth() / 2, size.GetHeight() / 2);
}

void MinimapWindow::OnScrollZoomToggle(wxCommandEvent& event) {
	g_settings.setInteger(Config::MINIMAP_SCROLL_ZOOM, event.IsChecked() ? 1 : 0);
}

void MinimapWindow::OnCaptionClose(wxCommandEvent& event) {
	g_gui.HideMinimap();
}

void MinimapWindow::OnKey(wxKeyEvent& event) {
	if (canvas) {
		canvas->OnKey(event);
	}
}