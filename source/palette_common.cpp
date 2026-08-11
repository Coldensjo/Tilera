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

#include "palette_common.h"
#include "brush.h"
#include "raw_brush.h"
#include "terraform.h"
#include "terraform_brush.h"
#include "items.h"
#include "sprites.h"
#include "gui.h"
#include "common_windows.h"
#include "application.h"
#include "theme.h"
#include "settings.h"

#include <cctype>

#ifdef __WXMSW__
	#include <windows.h>
#endif

namespace {
class PaletteHoverTooltip : public wxFrame {
public:
	PaletteHoverTooltip(wxWindow* parent) :
		wxFrame(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
			wxBORDER_SIMPLE | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxFRAME_TOOL_WINDOW) {
		wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
		label = newd wxStaticText(this, wxID_ANY, wxEmptyString);
		const ThemePalette& palette = ThemeManager::Get().GetPalette();
		const wxColour background = palette.control;
		label->SetBackgroundColour(background);
		label->SetForegroundColour(palette.text);
		SetBackgroundColour(background);
		sizer->Add(label, 1, wxALL, 3);
		SetSizer(sizer);

		// The tooltip is a wxSTAY_ON_TOP tool window, so it doesn't get hidden
		// automatically when the app loses focus (e.g. alt-tabbing away) -
		// without this it can be left floating over other windows.
		parent->Bind(wxEVT_ACTIVATE, [this](wxActivateEvent& event) {
			if (!event.GetActive()) {
				HideTooltip();
			}
			event.Skip();
		});
	}

	void Update(const wxString& text, const wxPoint& screenPos) {
		if (text.IsEmpty()) {
			HideTooltip();
			return;
		}

		label->SetLabel(text);
		Layout();
		Fit();

		const wxSize size = GetSize();
		// Anchor at the cursor tip and extend leftward. Offset down 8px so the
		// tooltip doesn't cover the item being hovered.
		int x = screenPos.x - size.GetWidth();
		int y = screenPos.y + 15;
		const wxRect display = wxGetClientDisplayRect();
		if (x < display.GetLeft()) {
			x = display.GetLeft();
		}
		if (y + size.GetHeight() > display.GetBottom()) {
			y = display.GetBottom() - size.GetHeight();
		}
		SetPosition(wxPoint(x, y));
		if (!IsShown()) {
			ShowWithoutActivating();
		}
		// NOTE: Do not call Raise() here. On wxMSW Raise() calls
		// ::SetForegroundWindow(), which steals activation to this tooltip frame
		// and deactivates the main frame - that disables the main frame's menu
		// accelerators (e.g. Ctrl+Z) while the hover tooltip is visible. The
		// wxSTAY_ON_TOP style already keeps the tooltip above other windows.
	}

	void HideTooltip() {
		if (IsShown()) {
			Hide();
		}
	}

#ifdef __WXMSW__
	WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override {
		if (message == WM_NCHITTEST) {
			return HTTRANSPARENT;
		}
		return wxFrame::MSWWindowProc(message, wParam, lParam);
	}
#endif

private:
	wxStaticText* label;
};

PaletteHoverTooltip* palette_hover_tooltip = nullptr;
const Brush* cached_hover_brush = nullptr;
wxString cached_hover_label;

PaletteHoverTooltip* GetPaletteHoverTooltip() {
	if (!palette_hover_tooltip && g_gui.root) {
		palette_hover_tooltip = newd PaletteHoverTooltip(g_gui.root);
	}
	return palette_hover_tooltip;
}

wxString BrushHoverIdText(const Brush* brush) {
	if (!brush || brush->isPaletteSeparator()) {
		return wxEmptyString;
	}
	if (brush == cached_hover_brush) {
		return cached_hover_label;
	}

	cached_hover_brush = brush;
	cached_hover_label = wxEmptyString;

	if (brush->isRaw()) {
		const ItemType* itemtype = static_cast<const RAWBrush*>(brush)->getItemType();
		if (itemtype) {
			cached_hover_label = wxstr(itemtype->name) + wxT(" (") + i2ws(itemtype->id) + wxT(")");
			return cached_hover_label;
		}
	}

	const std::string& name = brush->getName();
	if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
		const size_t separator = name.find(" - ");
		if (separator != std::string::npos) {
			cached_hover_label = wxstr(name.substr(separator + 3)) + wxT(" (") + wxstr(name.substr(0, separator)) + wxT(")");
			return cached_hover_label;
		}
	}

	cached_hover_label = wxstr(name);
	return cached_hover_label;
}
} // namespace

namespace {
bool ShouldShowPaletteToolPanel(const wxString& name) {
	if (name == "Tools") {
		return g_settings.getBoolean(Config::SHOW_PALETTE_TOOLS);
	}
	if (name == "Brush Size") {
		return g_settings.getBoolean(Config::SHOW_PALETTE_BRUSH_SIZE);
	}
	return true;
}
} // namespace

void ShowPaletteBrushHoverTooltip(const Brush* brush, const wxPoint& screenPos) {
	PaletteHoverTooltip* tooltip = GetPaletteHoverTooltip();
	if (!tooltip) {
		return;
	}
	tooltip->Update(BrushHoverIdText(brush), screenPos);
}

void HidePaletteBrushHoverTooltip() {
	cached_hover_brush = nullptr;
	cached_hover_label = wxEmptyString;
	if (palette_hover_tooltip) {
		palette_hover_tooltip->HideTooltip();
	}
}

// ============================================================================
// Palette Panel

BEGIN_EVENT_TABLE(PalettePanel, wxPanel)
END_EVENT_TABLE()

PalettePanel::PalettePanel(wxWindow* parent, wxWindowID id, long style) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, style),
	refresh_timer(this, PALETTE_DELAYED_REFRESH_TIMER),
	last_brush_size(0),
	last_brush_even(false) {
	////
}

PalettePanel::~PalettePanel() {
	////
}

PaletteWindow* PalettePanel::GetParentPalette() const {
	const wxWindow* w = this;
	while ((w = w->GetParent()) && dynamic_cast<const PaletteWindow*>(w) == nullptr)
		;
	return const_cast<PaletteWindow*>(static_cast<const PaletteWindow*>(w));
}

void PalettePanel::InvalidateContents() {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->InvalidateContents();
	}
}

void PalettePanel::LoadCurrentContents() {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->OnSwitchIn();
	}
	Fit();
}

void PalettePanel::LoadAllContents() {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->LoadAllContents();
	}
}

void PalettePanel::AddToolPanel(PalettePanel* panel) {
	wxSizer* sp_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, panel->GetName());
	sp_sizer->Add(panel, 0, wxEXPAND);
	sp_sizer->Show(ShouldShowPaletteToolPanel(panel->GetName()));
	GetSizer()->Add(sp_sizer, 0, wxEXPAND);

	Fit();

	tool_bars.push_back({ panel, sp_sizer });
}

void PalettePanel::UpdateToolPanelVisibility() {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		if (iter->box_sizer) {
			iter->box_sizer->Show(ShouldShowPaletteToolPanel(iter->panel->GetName()));
		}
	}
	if (GetSizer()) {
		GetSizer()->Layout();
	}
	Layout();
	Fit();
}

void PalettePanel::SetToolbarIconSize(bool large_icons) {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->SetToolbarIconSize(large_icons);
	}
}

wxString PalettePanel::GetName() const {
	switch (GetType()) {
		case TILESET_TERRAIN:
			return "Terrain Palette";
		case TILESET_DOODAD:
			return "Doodad Palette";
		case TILESET_ITEM:
			return "Item Palette";
		case TILESET_CREATURE:
			return "Creature Palette";
		case TILESET_HOUSE:
			return "House Palette";
		case TILESET_RAW:
			return "RAW Palette";
		case TILESET_UNKNOWN:
			return "Unknown";
	}
	return wxEmptyString;
}

PaletteType PalettePanel::GetType() const {
	return TILESET_UNKNOWN;
}

Brush* PalettePanel::GetSelectedBrush() const {
	return nullptr;
}

int PalettePanel::GetSelectedBrushSize() const {
	return 0;
}

void PalettePanel::SelectFirstBrush() {
	// Do nothing
}

bool PalettePanel::SelectBrush(const Brush* whatbrush) {
	return false;
}

void PalettePanel::OnUpdateBrushSize(BrushShape shape, int size) {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->OnUpdateBrushSize(shape, size);
	}
}

void PalettePanel::OnSwitchIn() {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->OnSwitchIn();
	}
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSize(last_brush_even ? GUI::BRUSH_SIZE_2X2 : last_brush_size);
}

void PalettePanel::OnSwitchOut() {
	last_brush_size = g_gui.GetBrushSize();
	last_brush_even = g_gui.IsBrushEvenSize();
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->OnSwitchOut();
	}
}

void PalettePanel::OnUpdate() {
	for (ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
		iter->panel->OnUpdate();
	}
}

void PalettePanel::RefreshOtherPalettes() {
	refresh_timer.Start(100, true);
}

void PalettePanel::OnRefreshTimer(wxTimerEvent&) {
	g_gui.RefreshOtherPalettes(GetParentPalette());
}

// ============================================================================
// Size Page

BEGIN_EVENT_TABLE(BrushSizePanel, wxPanel)
EVT_TOGGLEBUTTON(PALETTE_BRUSHSHAPE_SQUARE, BrushSizePanel::OnClickSquareBrush)
EVT_TOGGLEBUTTON(PALETTE_BRUSHSHAPE_CIRCLE, BrushSizePanel::OnClickCircleBrush)

EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_0, BrushSizePanel::OnClickBrushSize0)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_2X2, BrushSizePanel::OnClickBrushSize2x2)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_1, BrushSizePanel::OnClickBrushSize1)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_2, BrushSizePanel::OnClickBrushSize2)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_4, BrushSizePanel::OnClickBrushSize4)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_6, BrushSizePanel::OnClickBrushSize6)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_8, BrushSizePanel::OnClickBrushSize8)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_BRUSHSIZE_11, BrushSizePanel::OnClickBrushSize11)
END_EVENT_TABLE()

BrushSizePanel::BrushSizePanel(wxWindow* parent) :
	PalettePanel(parent, wxID_ANY),
	loaded(false),
	large_icons(true),
	brushshapeSquareButton(nullptr),
	brushshapeCircleButton(nullptr),
	brushsize0Button(nullptr),
	brushsize2x2Button(nullptr),
	brushsize1Button(nullptr),
	brushsize2Button(nullptr),
	brushsize4Button(nullptr),
	brushsize6Button(nullptr),
	brushsize8Button(nullptr),
	brushsize11Button(nullptr) {
	////
}

void BrushSizePanel::InvalidateContents() {
	if (loaded) {
		DestroyChildren();
		SetSizer(nullptr);

		brushshapeSquareButton = brushshapeCircleButton = brushsize0Button = brushsize2x2Button = brushsize1Button = brushsize2Button = brushsize4Button = brushsize6Button = brushsize8Button = brushsize11Button = nullptr;

		loaded = false;
	}
}

void BrushSizePanel::LoadCurrentContents() {
	LoadAllContents();
}

void BrushSizePanel::LoadAllContents() {
	if (loaded) {
		return;
	}

	wxSizer* size_sizer = newd wxBoxSizer(wxVERTICAL);
	;
	wxSizer* sub_sizer = newd wxBoxSizer(wxHORIZONTAL);
	RenderSize render_size;

	if (large_icons) {
		// 32x32
		render_size = RENDER_SIZE_32x32;
	} else {
		// 16x16
		render_size = RENDER_SIZE_16x16;
	}

	sub_sizer->Add(brushshapeSquareButton = newd DCButton(this, PALETTE_BRUSHSHAPE_SQUARE, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_9x9));
	brushshapeSquareButton->SetToolTip("Square brush");

	sub_sizer->Add(brushshapeCircleButton = newd DCButton(this, PALETTE_BRUSHSHAPE_CIRCLE, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_CD_9x9));
	brushshapeCircleButton->SetToolTip("Circle brush");
	brushshapeSquareButton->SetValue(true);

	if (large_icons) {
		sub_sizer->AddSpacer(36);
	} else {
		sub_sizer->AddSpacer(18);
	}

	sub_sizer->Add(brushsize0Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_0, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_CD_1x1));
	brushsize0Button->SetToolTip("1x1");
	brushsize0Button->SetValue(true);

	sub_sizer->Add(brushsize2x2Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_2X2, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_2x2));
	brushsize2x2Button->SetToolTip("2x2");

	sub_sizer->Add(brushsize1Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_1, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_3x3));
	brushsize1Button->SetToolTip("3x3");

	if (large_icons) {
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);
	}

	sub_sizer->Add(brushsize2Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_2, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_5x5));
	brushsize2Button->SetToolTip("5x5");

	sub_sizer->Add(brushsize4Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_4, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_7x7));
	brushsize4Button->SetToolTip("9x9");

	sub_sizer->Add(brushsize6Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_6, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_9x9));
	brushsize6Button->SetToolTip("13x13");

	sub_sizer->Add(brushsize8Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_8, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_15x15));
	brushsize8Button->SetToolTip("17x17");

	sub_sizer->Add(brushsize11Button = newd DCButton(this, PALETTE_TERRAIN_BRUSHSIZE_11, wxDefaultPosition, DC_BTN_TOGGLE, render_size, EDITOR_SPRITE_BRUSH_SD_19x19));
	brushsize11Button->SetToolTip("23x23");

	size_sizer->Add(sub_sizer);
	SetSizerAndFit(size_sizer);

	loaded = true;
}

wxString BrushSizePanel::GetName() const {
	return "Brush Size";
}

void BrushSizePanel::SetToolbarIconSize(bool d) {
	InvalidateContents();
	large_icons = d;
}

void BrushSizePanel::OnSwitchIn() {
	LoadCurrentContents();
}

void BrushSizePanel::OnUpdateBrushSize(BrushShape shape, int size) {
	if (shape == BRUSHSHAPE_SQUARE) {
		brushshapeCircleButton->SetValue(false);
		brushshapeSquareButton->SetValue(true);

		brushsize0Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_1x1);
		if (brushsize2x2Button) {
			brushsize2x2Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_2x2);
		}
		brushsize1Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_3x3);
		brushsize2Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_5x5);
		brushsize4Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_7x7);
		brushsize6Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_9x9);
		brushsize8Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_15x15);
		brushsize11Button->SetSprite(EDITOR_SPRITE_BRUSH_SD_19x19);
		if (brushsize2x2Button) {
			brushsize2x2Button->Enable(true);
		}
	} else {
		brushshapeSquareButton->SetValue(false);
		brushshapeCircleButton->SetValue(true);

		brushsize0Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_1x1);
		if (brushsize2x2Button) {
			brushsize2x2Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_3x3);
		}
		brushsize1Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_3x3);
		brushsize2Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_5x5);
		brushsize4Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_7x7);
		brushsize6Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_9x9);
		brushsize8Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_15x15);
		brushsize11Button->SetSprite(EDITOR_SPRITE_BRUSH_CD_19x19);
		if (brushsize2x2Button) {
			brushsize2x2Button->Enable(false);
			brushsize2x2Button->SetValue(false);
		}
	}

	if (brushsize0Button) {
		brushsize0Button->SetValue(false);
	}
	if (brushsize2x2Button) {
		brushsize2x2Button->SetValue(false);
	}
	if (brushsize1Button) {
		brushsize1Button->SetValue(false);
	}
	if (brushsize2Button) {
		brushsize2Button->SetValue(false);
	}
	if (brushsize4Button) {
		brushsize4Button->SetValue(false);
	}
	if (brushsize6Button) {
		brushsize6Button->SetValue(false);
	}
	if (brushsize8Button) {
		brushsize8Button->SetValue(false);
	}
	if (brushsize11Button) {
		brushsize11Button->SetValue(false);
	}

	switch (size) {
		case 0:
			if (brushsize0Button) {
				brushsize0Button->SetValue(true);
			}
			break;
		case GUI::BRUSH_SIZE_2X2:
			if (brushsize2x2Button) {
				brushsize2x2Button->SetValue(true);
			}
			break;
		case 1:
			if (brushsize1Button) {
				brushsize1Button->SetValue(true);
			}
			break;
		case 2:
			if (brushsize2Button) {
				brushsize2Button->SetValue(true);
			}
			break;
		case 4:
			if (brushsize4Button) {
				brushsize4Button->SetValue(true);
			}
			break;
		case 6:
			if (brushsize6Button) {
				brushsize6Button->SetValue(true);
			}
			break;
		case 8:
			if (brushsize8Button) {
				brushsize8Button->SetValue(true);
			}
			break;
		case 11:
			if (brushsize11Button) {
				brushsize11Button->SetValue(true);
			}
			break;
		default:
			if (brushsize0Button) {
				brushsize0Button->SetValue(true);
			}
			break;
	}
}

void BrushSizePanel::OnClickCircleBrush(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushShape(BRUSHSHAPE_CIRCLE);
}

void BrushSizePanel::OnClickSquareBrush(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushShape(BRUSHSHAPE_SQUARE);
}

void BrushSizePanel::OnClickBrushSize2x2(wxCommandEvent& event) {
	OnClickBrushSize(GUI::BRUSH_SIZE_2X2);
}

void BrushSizePanel::OnClickBrushSize(int which) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSize(which);
}

// ============================================================================
// Tool Brush Panel

BEGIN_EVENT_TABLE(BrushToolPanel, PalettePanel)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_OPTIONAL_BORDER_TOOL, BrushToolPanel::OnClickGravelButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_ERASER, BrushToolPanel::OnClickEraserButton)

EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NORMAL_DOOR, BrushToolPanel::OnClickNormalDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_LOCKED_DOOR, BrushToolPanel::OnClickLockedDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_MAGIC_DOOR, BrushToolPanel::OnClickMagicDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_QUEST_DOOR, BrushToolPanel::OnClickQuestDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_HATCH_DOOR, BrushToolPanel::OnClickHatchDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_WINDOW_DOOR, BrushToolPanel::OnClickWindowDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NORMAL_ALT_DOOR, BrushToolPanel::OnClickNormalAltDoorButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_ARCHWAY_DOOR, BrushToolPanel::OnClickArchwayDoorButton)

EVT_TOGGLEBUTTON(PALETTE_TERRAIN_PZ_TOOL, BrushToolPanel::OnClickPZBrushButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NOPVP_TOOL, BrushToolPanel::OnClickNOPVPBrushButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_NOLOGOUT_TOOL, BrushToolPanel::OnClickNoLogoutBrushButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_PVPZONE_TOOL, BrushToolPanel::OnClickPVPZoneBrushButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_REFRESH_TOOL, BrushToolPanel::OnClickRefreshBrushButton)

EVT_TOGGLEBUTTON(PALETTE_TERRAIN_RAISE_TOOL, BrushToolPanel::OnClickRaiseButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_LOWER_TOOL, BrushToolPanel::OnClickLowerButton)
EVT_TOGGLEBUTTON(PALETTE_TERRAIN_FLATTEN_TOOL, BrushToolPanel::OnClickFlattenButton)
EVT_CHOICE(PALETTE_TERRAIN_TERRAFORM_PAIR_CHOICE, BrushToolPanel::OnSelectTerraformPair)

EVT_CHECKBOX(PALETTE_TERRAIN_LOCK_DOOR, BrushToolPanel::OnClickLockDoorCheckbox)
END_EVENT_TABLE()

BrushToolPanel::BrushToolPanel(wxWindow* parent) :
	PalettePanel(parent, wxID_ANY),
	loaded(false),
	large_icons(true),
	optionalBorderButton(nullptr),
	eraserButton(nullptr),
	normalDoorButton(nullptr),
	lockedDoorButton(nullptr),
	magicDoorButton(nullptr),
	questDoorButton(nullptr),
	hatchDoorButton(nullptr),
	windowDoorButton(nullptr),
	normalDoorAltButton(nullptr),
	archwayDoorButton(nullptr),
	pzBrushButton(nullptr),
	nopvpBrushButton(nullptr),
	nologBrushButton(nullptr),
	pvpzoneBrushButton(nullptr),
	refreshBrushButton(nullptr),
	raiseButton(nullptr),
	lowerButton(nullptr),
	flattenButton(nullptr),
	terraformPairChoice(nullptr) {
	////
}

BrushToolPanel::~BrushToolPanel() {
	////
}

void BrushToolPanel::InvalidateContents() {
	if (loaded) {
		DestroyChildren();
		SetSizer(nullptr);

		optionalBorderButton = eraserButton = normalDoorButton = lockedDoorButton = magicDoorButton = questDoorButton = hatchDoorButton = windowDoorButton = normalDoorAltButton = archwayDoorButton = pzBrushButton = nopvpBrushButton = nologBrushButton = pvpzoneBrushButton = refreshBrushButton = raiseButton = lowerButton = flattenButton = nullptr;
		terraformPairChoice = nullptr;

		loaded = false;
	}
}

void BrushToolPanel::LoadCurrentContents() {
	LoadAllContents();
}

void BrushToolPanel::LoadAllContents() {
	if (loaded) {
		return;
	}

	wxSizer* size_sizer = newd wxBoxSizer(wxVERTICAL);
	;
	wxSizer* sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

	/*RenderSize render_size;
	if(large_icons) {
		// 32x32
		render_size = RENDER_SIZE_32x32;
	} else {
		// 16x16
		render_size = RENDER_SIZE_16x16;
	}*/

	if (large_icons) {
		// Create the tool page with 32x32 icons

		ASSERT(g_gui.optional_brush);
		sub_sizer->Add(optionalBorderButton = newd BrushButton(this, g_gui.optional_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_OPTIONAL_BORDER_TOOL));
		optionalBorderButton->SetToolTip("Optional Border Tool");

		ASSERT(g_gui.eraser);
		sub_sizer->Add(eraserButton = newd BrushButton(this, g_gui.eraser, RENDER_SIZE_32x32, PALETTE_TERRAIN_ERASER));
		eraserButton->SetToolTip("Eraser");

		ASSERT(g_gui.pz_brush);
		sub_sizer->Add(pzBrushButton = newd BrushButton(this, g_gui.pz_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_PZ_TOOL));
		pzBrushButton->SetToolTip("PZ Tool");

		ASSERT(g_gui.rook_brush);
		sub_sizer->Add(nopvpBrushButton = newd BrushButton(this, g_gui.rook_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NOPVP_TOOL));
		nopvpBrushButton->SetToolTip("NO PVP Tool");

		ASSERT(g_gui.nolog_brush);
		sub_sizer->Add(nologBrushButton = newd BrushButton(this, g_gui.nolog_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NOLOGOUT_TOOL));
		nologBrushButton->SetToolTip("No Logout Tool");

		ASSERT(g_gui.pvp_brush);
		sub_sizer->Add(pvpzoneBrushButton = newd BrushButton(this, g_gui.pvp_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_PVPZONE_TOOL));
		pvpzoneBrushButton->SetToolTip("PVP Zone Tool");

		ASSERT(g_gui.refresh_brush);
		sub_sizer->Add(refreshBrushButton = newd BrushButton(this, g_gui.refresh_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_REFRESH_TOOL));
		refreshBrushButton->SetToolTip("Refresh Zone Tool");

		// New row
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

		ASSERT(g_gui.normal_door_brush);
		sub_sizer->Add(normalDoorButton = newd BrushButton(this, g_gui.normal_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NORMAL_DOOR));
		normalDoorButton->SetToolTip("Normal Door Tool");

		ASSERT(g_gui.locked_door_brush);
		sub_sizer->Add(lockedDoorButton = newd BrushButton(this, g_gui.locked_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_LOCKED_DOOR));
		lockedDoorButton->SetToolTip("Locked Door Tool");

		ASSERT(g_gui.magic_door_brush);
		sub_sizer->Add(magicDoorButton = newd BrushButton(this, g_gui.magic_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_MAGIC_DOOR));
		magicDoorButton->SetToolTip("Magic Door Tool");

		ASSERT(g_gui.quest_door_brush);
		sub_sizer->Add(questDoorButton = newd BrushButton(this, g_gui.quest_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_QUEST_DOOR));
		questDoorButton->SetToolTip("Quest Door Tool");

		ASSERT(g_gui.hatch_door_brush);
		sub_sizer->Add(hatchDoorButton = newd BrushButton(this, g_gui.hatch_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_HATCH_DOOR));
		hatchDoorButton->SetToolTip("Hatch Window Tool");

		ASSERT(g_gui.window_door_brush);
		sub_sizer->Add(windowDoorButton = newd BrushButton(this, g_gui.window_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_WINDOW_DOOR));
		windowDoorButton->SetToolTip("Window Tool");

		// New row
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

		ASSERT(g_gui.normal_door_alt_brush);
		sub_sizer->Add(normalDoorAltButton = newd BrushButton(this, g_gui.normal_door_alt_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_NORMAL_ALT_DOOR));
		normalDoorAltButton->SetToolTip("Normal Door (alt)");

		ASSERT(g_gui.archway_door_brush);
		sub_sizer->Add(archwayDoorButton = newd BrushButton(this, g_gui.archway_door_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_ARCHWAY_DOOR));
		archwayDoorButton->SetToolTip("Archway Tool");

		ASSERT(g_gui.raise_brush);
		sub_sizer->Add(raiseButton = newd BrushButton(this, g_gui.raise_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_RAISE_TOOL));
		raiseButton->SetToolTip("Raise Ground - top uses the last selected ground brush (Ctrl inverts)");

		ASSERT(g_gui.lower_brush);
		sub_sizer->Add(lowerButton = newd BrushButton(this, g_gui.lower_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_LOWER_TOOL));
		lowerButton->SetToolTip("Lower Ground - top uses the last selected ground brush (Ctrl inverts)");

		ASSERT(g_gui.flatten_brush);
		sub_sizer->Add(flattenButton = newd BrushButton(this, g_gui.flatten_brush, RENDER_SIZE_32x32, PALETTE_TERRAIN_FLATTEN_TOOL));
		flattenButton->SetToolTip("Flatten Ground - top uses the last selected ground brush");
	} else {
		// Create the tool page with 16x16 icons
		// Create tool window #1

		ASSERT(g_gui.optional_brush);
		sub_sizer->Add(optionalBorderButton = newd BrushButton(this, g_gui.optional_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_OPTIONAL_BORDER_TOOL));
		optionalBorderButton->SetToolTip("Optional Border Tool");

		ASSERT(g_gui.eraser);
		sub_sizer->Add(eraserButton = newd BrushButton(this, g_gui.eraser, RENDER_SIZE_16x16, PALETTE_TERRAIN_ERASER));
		eraserButton->SetToolTip("Eraser");

		// sub_sizer->AddSpacer(20);
		ASSERT(g_gui.normal_door_alt_brush);
		sub_sizer->Add(normalDoorAltButton = newd BrushButton(this, g_gui.normal_door_alt_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NORMAL_ALT_DOOR));
		normalDoorAltButton->SetToolTip("Normal Door (alt)");

		ASSERT(g_gui.normal_door_brush);
		sub_sizer->Add(normalDoorButton = newd BrushButton(this, g_gui.normal_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NORMAL_DOOR));
		normalDoorButton->SetToolTip("Normal Door Tool");

		ASSERT(g_gui.locked_door_brush);
		sub_sizer->Add(lockedDoorButton = newd BrushButton(this, g_gui.locked_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_LOCKED_DOOR));
		lockedDoorButton->SetToolTip("Locked Door Tool");

		ASSERT(g_gui.magic_door_brush);
		sub_sizer->Add(magicDoorButton = newd BrushButton(this, g_gui.magic_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_MAGIC_DOOR));
		magicDoorButton->SetToolTip("Magic Door Tool");

		ASSERT(g_gui.quest_door_brush);
		sub_sizer->Add(questDoorButton = newd BrushButton(this, g_gui.quest_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_QUEST_DOOR));
		questDoorButton->SetToolTip("Quest Door Tool");

		ASSERT(g_gui.hatch_door_brush);
		sub_sizer->Add(hatchDoorButton = newd BrushButton(this, g_gui.hatch_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_HATCH_DOOR));
		hatchDoorButton->SetToolTip("Hatch Window Tool");

		ASSERT(g_gui.window_door_brush);
		sub_sizer->Add(windowDoorButton = newd BrushButton(this, g_gui.window_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_WINDOW_DOOR));
		windowDoorButton->SetToolTip("Window Tool");

		ASSERT(g_gui.archway_door_brush);
		sub_sizer->Add(archwayDoorButton = newd BrushButton(this, g_gui.archway_door_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_ARCHWAY_DOOR));
		archwayDoorButton->SetToolTip("Archway Tool");

		// Next row
		size_sizer->Add(sub_sizer);
		sub_sizer = newd wxBoxSizer(wxHORIZONTAL);

		ASSERT(g_gui.pz_brush);
		sub_sizer->Add(pzBrushButton = newd BrushButton(this, g_gui.pz_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_PZ_TOOL));
		pzBrushButton->SetToolTip("PZ Tool");

		ASSERT(g_gui.rook_brush);
		sub_sizer->Add(nopvpBrushButton = newd BrushButton(this, g_gui.rook_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NOPVP_TOOL));
		nopvpBrushButton->SetToolTip("NO PVP Tool");

		ASSERT(g_gui.nolog_brush);
		sub_sizer->Add(nologBrushButton = newd BrushButton(this, g_gui.nolog_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_NOLOGOUT_TOOL));
		nologBrushButton->SetToolTip("No Logout Tool");

		ASSERT(g_gui.pvp_brush);
		sub_sizer->Add(pvpzoneBrushButton = newd BrushButton(this, g_gui.pvp_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_PVPZONE_TOOL));
		pvpzoneBrushButton->SetToolTip("PVP Zone Tool");

		ASSERT(g_gui.refresh_brush);
		sub_sizer->Add(refreshBrushButton = newd BrushButton(this, g_gui.refresh_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_REFRESH_TOOL));
		refreshBrushButton->SetToolTip("Refresh Zone Tool");

		ASSERT(g_gui.raise_brush);
		sub_sizer->Add(raiseButton = newd BrushButton(this, g_gui.raise_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_RAISE_TOOL));
		raiseButton->SetToolTip("Raise Ground - top uses the last selected ground brush (Ctrl inverts)");

		ASSERT(g_gui.lower_brush);
		sub_sizer->Add(lowerButton = newd BrushButton(this, g_gui.lower_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_LOWER_TOOL));
		lowerButton->SetToolTip("Lower Ground - top uses the last selected ground brush (Ctrl inverts)");

		ASSERT(g_gui.flatten_brush);
		sub_sizer->Add(flattenButton = newd BrushButton(this, g_gui.flatten_brush, RENDER_SIZE_16x16, PALETTE_TERRAIN_FLATTEN_TOOL));
		flattenButton->SetToolTip("Flatten Ground - top uses the last selected ground brush");
	}

	sub_sizer->AddSpacer(large_icons ? 42 : 24);

	wxSizer* checkbox_sub_sizer = newd wxBoxSizer(wxVERTICAL);
	checkbox_sub_sizer->AddSpacer(large_icons ? 12 : 3);

	lockDoorCheckbox = newd wxCheckBox(this, PALETTE_TERRAIN_LOCK_DOOR, "Lock door");
	lockDoorCheckbox->SetToolTip("Prefer to draw \"locked\" variant of selected door brush if applicable.");
	lockDoorCheckbox->SetValue(g_settings.getInteger(Config::DRAW_LOCKED_DOOR));
	checkbox_sub_sizer->Add(lockDoorCheckbox);

	sub_sizer->Add(checkbox_sub_sizer);

	size_sizer->Add(sub_sizer);

	// Terraform pair selector - only relevant when the data files define a choice
	if (g_terraform_pairs.getPairs().size() > 1) {
		wxSizer* choice_sizer = newd wxBoxSizer(wxHORIZONTAL);
		terraformPairChoice = newd wxChoice(this, PALETTE_TERRAIN_TERRAFORM_PAIR_CHOICE);
		for (const TerraformPair& pair : g_terraform_pairs.getPairs()) {
			terraformPairChoice->Append(wxstr(pair.name));
		}
		terraformPairChoice->SetSelection(static_cast<int>(g_terraform_pairs.getActiveIndex()));
		terraformPairChoice->SetToolTip("Terraform brush pair (fill/top grounds used by raise, lower and flatten)");
		choice_sizer->Add(terraformPairChoice);
		size_sizer->Add(choice_sizer);
	}

	SetSizerAndFit(size_sizer);

	loaded = true;
}

wxString BrushToolPanel::GetName() const {
	return "Tools";
}

void BrushToolPanel::SetToolbarIconSize(bool d) {
	InvalidateContents();
	large_icons = d;
}

void BrushToolPanel::DeselectAll() {
	if (loaded) {
		optionalBorderButton->SetValue(false);
		eraserButton->SetValue(false);
		normalDoorButton->SetValue(false);
		lockedDoorButton->SetValue(false);
		magicDoorButton->SetValue(false);
		questDoorButton->SetValue(false);
		hatchDoorButton->SetValue(false);
		windowDoorButton->SetValue(false);
		normalDoorAltButton->SetValue(false);
		archwayDoorButton->SetValue(false);
		pzBrushButton->SetValue(false);
		nopvpBrushButton->SetValue(false);
		nologBrushButton->SetValue(false);
		pvpzoneBrushButton->SetValue(false);
		refreshBrushButton->SetValue(false);
		raiseButton->SetValue(false);
		lowerButton->SetValue(false);
		flattenButton->SetValue(false);
	}
}

Brush* BrushToolPanel::GetSelectedBrush() const {
	if (optionalBorderButton->GetValue()) {
		return g_gui.optional_brush;
	}
	if (eraserButton->GetValue()) {
		return g_gui.eraser;
	}
	if (normalDoorButton->GetValue()) {
		return g_gui.normal_door_brush;
	}
	if (lockedDoorButton->GetValue()) {
		return g_gui.locked_door_brush;
	}
	if (magicDoorButton->GetValue()) {
		return g_gui.magic_door_brush;
	}
	if (questDoorButton->GetValue()) {
		return g_gui.quest_door_brush;
	}
	if (hatchDoorButton->GetValue()) {
		return g_gui.hatch_door_brush;
	}
	if (windowDoorButton->GetValue()) {
		return g_gui.window_door_brush;
	}
	if (normalDoorAltButton->GetValue()) {
		return g_gui.normal_door_alt_brush;
	}
	if (archwayDoorButton->GetValue()) {
		return g_gui.archway_door_brush;
	}
	if (pzBrushButton->GetValue()) {
		return g_gui.pz_brush;
	}
	if (nopvpBrushButton->GetValue()) {
		return g_gui.rook_brush;
	}
	if (nologBrushButton->GetValue()) {
		return g_gui.nolog_brush;
	}
	if (pvpzoneBrushButton->GetValue()) {
		return g_gui.pvp_brush;
	}
	if (refreshBrushButton->GetValue()) {
		return g_gui.refresh_brush;
	}
	if (raiseButton->GetValue()) {
		return g_gui.raise_brush;
	}
	if (lowerButton->GetValue()) {
		return g_gui.lower_brush;
	}
	if (flattenButton->GetValue()) {
		return g_gui.flatten_brush;
	}
	return nullptr;
}

bool BrushToolPanel::SelectBrush(const Brush* whatbrush) {
	BrushButton* button = nullptr;
	if (whatbrush == g_gui.optional_brush) {
		button = optionalBorderButton;
	} else if (whatbrush == g_gui.eraser) {
		button = eraserButton;
	} else if (whatbrush == g_gui.normal_door_brush) {
		button = normalDoorButton;
	} else if (whatbrush == g_gui.locked_door_brush) {
		button = lockedDoorButton;
	} else if (whatbrush == g_gui.magic_door_brush) {
		button = magicDoorButton;
	} else if (whatbrush == g_gui.quest_door_brush) {
		button = questDoorButton;
	} else if (whatbrush == g_gui.hatch_door_brush) {
		button = hatchDoorButton;
	} else if (whatbrush == g_gui.window_door_brush) {
		button = windowDoorButton;
	} else if (whatbrush == g_gui.normal_door_alt_brush) {
		button = normalDoorAltButton;
	} else if (whatbrush == g_gui.archway_door_brush) {
		button = archwayDoorButton;
	} else if (whatbrush == g_gui.pz_brush) {
		button = pzBrushButton;
	} else if (whatbrush == g_gui.rook_brush) {
		button = nopvpBrushButton;
	} else if (whatbrush == g_gui.nolog_brush) {
		button = nologBrushButton;
	} else if (whatbrush == g_gui.pvp_brush) {
		button = pvpzoneBrushButton;
	} else if (whatbrush == g_gui.refresh_brush) {
		button = refreshBrushButton;
	} else if (whatbrush == g_gui.raise_brush) {
		button = raiseButton;
	} else if (whatbrush == g_gui.lower_brush) {
		button = lowerButton;
	} else if (whatbrush == g_gui.flatten_brush) {
		button = flattenButton;
	}

	DeselectAll();
	if (button) {
		button->SetValue(true);
		return true;
	}

	return false;
}

void BrushToolPanel::OnSwitchIn() {
	LoadCurrentContents();
}

void BrushToolPanel::OnClickGravelButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.optional_brush);
}

void BrushToolPanel::OnClickEraserButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.eraser);
}

void BrushToolPanel::OnClickNormalDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.normal_door_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickLockedDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.locked_door_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickMagicDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.magic_door_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickQuestDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.quest_door_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickHatchDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.hatch_door_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickWindowDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.window_door_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickNormalAltDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.normal_door_alt_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickArchwayDoorButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.archway_door_brush);

	// read checkbox settings
	g_gui.SetDoorLocked(lockDoorCheckbox->GetValue());
}

void BrushToolPanel::OnClickPZBrushButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.pz_brush);
}

void BrushToolPanel::OnClickNOPVPBrushButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.rook_brush);
}

void BrushToolPanel::OnClickNoLogoutBrushButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.nolog_brush);
}

void BrushToolPanel::OnClickPVPZoneBrushButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.pvp_brush);
}

void BrushToolPanel::OnClickRefreshBrushButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.refresh_brush);
}

void BrushToolPanel::OnClickRaiseButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.raise_brush);
}

void BrushToolPanel::OnClickLowerButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.lower_brush);
}

void BrushToolPanel::OnClickFlattenButton(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush(g_gui.flatten_brush);
}

void BrushToolPanel::OnSelectTerraformPair(wxCommandEvent& event) {
	const int selection = event.GetSelection();
	if (selection >= 0) {
		g_terraform_pairs.setActiveIndex(static_cast<size_t>(selection));
	}
}

void BrushToolPanel::OnClickLockDoorCheckbox(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());

	// apply to current brush
	g_gui.SetDoorLocked(event.IsChecked());

	// save user preference
	g_settings.setInteger(Config::DRAW_LOCKED_DOOR, event.IsChecked());
}

// ============================================================================
// Brush Button

BEGIN_EVENT_TABLE(BrushButton, ItemToggleButton)
EVT_KEY_DOWN(BrushButton::OnKey)
EVT_ENTER_WINDOW(BrushButton::OnMouseEnter)
EVT_MOTION(BrushButton::OnMouseMotion)
END_EVENT_TABLE()

BrushButton::BrushButton(wxWindow* parent, Brush* _brush, RenderSize sz, uint32_t id) :
	ItemToggleButton(parent, sz, uint16_t(0), id),
	brush(_brush) {
	ASSERT(brush);
	SetSprite(brush->getLookID());
}

BrushButton::~BrushButton() {
	////
}

void BrushButton::OnKey(wxKeyEvent& event) {
	g_gui.AddPendingCanvasEvent(event);
}

void BrushButton::OnMouseEnter(wxMouseEvent& event) {
	ShowPaletteBrushHoverTooltip(brush, wxGetMousePosition());
	event.Skip();
}

void BrushButton::OnMouseMotion(wxMouseEvent& event) {
	ShowPaletteBrushHoverTooltip(brush, wxGetMousePosition());
	event.Skip();
}

// ============================================================================
// Brush Thickness Panel

BEGIN_EVENT_TABLE(BrushThicknessPanel, PalettePanel)
#ifdef __WINDOWS__
// This only works in wxmsw
EVT_COMMAND_SCROLL_CHANGED(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
#else
EVT_COMMAND_SCROLL_TOP(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_BOTTOM(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_LINEUP(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_LINEDOWN(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_PAGEUP(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_PAGEDOWN(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
EVT_COMMAND_SCROLL_THUMBRELEASE(PALETTE_DOODAD_SLIDER, BrushThicknessPanel::OnScroll)
#endif

EVT_CHECKBOX(PALETTE_DOODAD_USE_THICKNESS, BrushThicknessPanel::OnClickCustomThickness)
END_EVENT_TABLE()

BrushThicknessPanel::BrushThicknessPanel(wxWindow* parent) :
	PalettePanel(parent, wxID_ANY) {
	wxSizer* thickness_sizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* thickness_sub_sizer = newd wxBoxSizer(wxHORIZONTAL);
	thickness_sub_sizer->Add(20, 10);
	use_button = newd wxCheckBox(this, PALETTE_DOODAD_USE_THICKNESS, "Use custom thickness");
	thickness_sub_sizer->Add(use_button);
	thickness_sizer->Add(thickness_sub_sizer, 1, wxEXPAND);

	slider = newd wxSlider(this, PALETTE_DOODAD_SLIDER, 5, 1, 10, wxDefaultPosition);
	thickness_sizer->Add(slider, 1, wxEXPAND);

	SetSizerAndFit(thickness_sizer);
}

BrushThicknessPanel::~BrushThicknessPanel() {
	////
}

wxString BrushThicknessPanel::GetName() const {
	return "Brush Thickness";
}

void BrushThicknessPanel::OnScroll(wxScrollEvent& event) {
	static const int lookup_table[10] = { 1, 2, 3, 5, 8, 13, 23, 35, 50, 80 };
	use_button->SetValue(true);

	ASSERT(event.GetPosition() >= 1);
	ASSERT(event.GetPosition() <= 10);

	// printf("SELECT[%d] = %d\n", event.GetPosition()-1, lookup_table[event.GetPosition()-1]);
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushThickness(true, lookup_table[event.GetPosition() - 1], 100);
}

void BrushThicknessPanel::OnClickCustomThickness(wxCommandEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushThickness(event.IsChecked());
}

void BrushThicknessPanel::OnSwitchIn() {
	static const int lookup_table[10] = { 1, 2, 3, 5, 8, 13, 23, 35, 50, 80 };
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushThickness(lookup_table[slider->GetValue() - 1], 100);
}
