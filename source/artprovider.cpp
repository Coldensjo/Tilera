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
#include "artprovider.h"

#include "pngfiles.h"
#include "theme.h"

#include <string>

namespace {

ArtProvider* g_installedProvider = nullptr;

#ifdef wxHAS_SVG
// Rasterises a "currentColor" SVG in the theme text colour.
wxBitmapBundle TintedSvgBundle(const char* svg, const wxSize& size) {
	std::string source(svg);
	const std::string colour = ThemeManager::Get().GetPalette().text.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
	const std::string placeholder = "currentColor";
	for (std::string::size_type pos = source.find(placeholder); pos != std::string::npos; pos = source.find(placeholder, pos + colour.size())) {
		source.replace(pos, placeholder.size(), colour);
	}
	return wxBitmapBundle::FromSVG(source.c_str(), size);
}
#endif

} // namespace

ArtProvider::ArtProvider() {
	g_installedProvider = this;
}

ArtProvider::~ArtProvider() {
	if (g_installedProvider == this) {
		g_installedProvider = nullptr;
	}
}

void ArtProvider::Refresh() {
	if (!g_installedProvider) {
		return;
	}
	// Push() clears the shared bitmap cache; Remove() first keeps a single entry.
	wxArtProvider::Remove(g_installedProvider);
	wxArtProvider::Push(g_installedProvider);
}

// A 16 px base bitmap plus its 32 px twin; wx serves whichever matches the DPI.
#define PNG_BUNDLE(small, large) wxBitmapBundle::FromBitmaps(PNG_BITMAP(small), PNG_BITMAP(large))

// Only a single resolution exists for this art; wx scales it as needed.
#define PNG_BUNDLE_SINGLE(name) wxBitmapBundle::FromBitmap(PNG_BITMAP(name))

wxBitmapBundle ArtProvider::CreateBitmapBundle(const wxArtID& id, const wxArtClient& client, const wxSize& size) {
#ifdef wxHAS_SVG
	// The monochrome action family serves toolbars, menus and buttons alike.
	if (client == wxART_TOOLBAR || client == wxART_MENU || client == wxART_BUTTON || client == wxART_OTHER) {
		if (const char* svg = FindIconSvg(id)) {
			const wxSize logical = (size.IsFullySpecified() && size.x > 0) ? size : wxSize(16, 16);
			return TintedSvgBundle(svg, logical);
		}
	}
#endif

	if (client != wxART_TOOLBAR) {
		return wxBitmapBundle();
	}

	// Brush shapes and sizes
	if (id == ART_CIRCULAR || id == ART_CIRCULAR_4) {
		return PNG_BUNDLE(circular_4_small_png, circular_4_png);
	} else if (id == ART_CIRCULAR_1) {
		return PNG_BUNDLE(circular_1_small_png, circular_1_png);
	} else if (id == ART_CIRCULAR_2) {
		return PNG_BUNDLE(circular_2_small_png, circular_2_png);
	} else if (id == ART_CIRCULAR_3) {
		return PNG_BUNDLE(circular_3_small_png, circular_3_png);
	} else if (id == ART_CIRCULAR_5) {
		return PNG_BUNDLE(circular_5_small_png, circular_5_png);
	} else if (id == ART_CIRCULAR_6) {
		return PNG_BUNDLE(circular_6_small_png, circular_6_png);
	} else if (id == ART_CIRCULAR_7) {
		return PNG_BUNDLE(circular_7_small_png, circular_7_png);
	} else if (id == ART_RECTANGULAR || id == ART_RECTANGULAR_4) {
		return PNG_BUNDLE(rectangular_4_small_png, rectangular_4_png);
	} else if (id == ART_RECTANGULAR_1) {
		return PNG_BUNDLE(rectangular_1_small_png, rectangular_1_png);
	} else if (id == ART_RECTANGULAR_2) {
		return PNG_BUNDLE(rectangular_2_small_png, rectangular_2_png);
	} else if (id == ART_RECTANGULAR_3) {
		return PNG_BUNDLE(rectangular_3_small_png, rectangular_3_png);
	} else if (id == ART_RECTANGULAR_5) {
		return PNG_BUNDLE(rectangular_5_small_png, rectangular_5_png);
	} else if (id == ART_RECTANGULAR_6) {
		return PNG_BUNDLE(rectangular_6_small_png, rectangular_6_png);
	} else if (id == ART_RECTANGULAR_7) {
		return PNG_BUNDLE(rectangular_7_small_png, rectangular_7_png);
	}

	// Zone and terrain tool brushes
	if (id == ART_NOLOOUT_BRUSH) {
		return PNG_BUNDLE(no_logout_small_png, no_logout_png);
	} else if (id == ART_NOPVP_BRUSH) {
		return PNG_BUNDLE(no_pvp_small_png, no_pvp_png);
	} else if (id == ART_PVP_BRUSH) {
		return PNG_BUNDLE(pvp_zone_small_png, pvp_zone_png);
	} else if (id == ART_PZ_BRUSH) {
		return PNG_BUNDLE(protection_zone_small_png, protection_zone_png);
	} else if (id == ART_REFRESH_BRUSH) {
		return PNG_BUNDLE(refresh_small_png, refresh_png);
	} else if (id == ART_OPTIONAL_BORDER_BRUSH) {
		return PNG_BUNDLE(optional_border_small_png, optional_border_png);
	} else if (id == ART_ERASER_BRUSH) {
		return PNG_BUNDLE(eraser_small_png, eraser_png);
	} else if (id == ART_TERRAFORM_RAISE) {
		return PNG_BUNDLE(terraform_raise_small_png, terraform_raise_png);
	} else if (id == ART_TERRAFORM_LOWER) {
		return PNG_BUNDLE(terraform_lower_small_png, terraform_lower_png);
	} else if (id == ART_TERRAFORM_FLATTEN) {
		return PNG_BUNDLE(terraform_flatten_small_png, terraform_flatten_png);
	}

	// Doors and windows
	if (id == ART_DOOR_NORMAL_SMALL) {
		return PNG_BUNDLE(door_normal_small_png, door_normal_png);
	} else if (id == ART_DOOR_LOCKED_SMALL) {
		return PNG_BUNDLE(door_locked_small_png, door_locked_png);
	} else if (id == ART_DOOR_MAGIC_SMALL) {
		return PNG_BUNDLE(door_magic_small_png, door_magic_png);
	} else if (id == ART_DOOR_QUEST_SMALL) {
		return PNG_BUNDLE(door_quest_small_png, door_quest_png);
	} else if (id == ART_DOOR_NORMAL_ALT_SMALL) {
		return PNG_BUNDLE(door_normal_alt_small_png, door_normal_alt_png);
	} else if (id == ART_DOOR_ARCHWAY_SMALL) {
		return PNG_BUNDLE(door_archway_small_png, door_archway_png);
	} else if (id == ART_DOOR_NORMAL) {
		return PNG_BUNDLE_SINGLE(door_normal_png);
	} else if (id == ART_DOOR_LOCKED) {
		return PNG_BUNDLE_SINGLE(door_locked_png);
	} else if (id == ART_DOOR_MAGIC) {
		return PNG_BUNDLE_SINGLE(door_magic_png);
	} else if (id == ART_DOOR_QUEST) {
		return PNG_BUNDLE_SINGLE(door_quest_png);
	} else if (id == ART_DOOR_NORMAL_ALT) {
		return PNG_BUNDLE_SINGLE(door_normal_alt_png);
	} else if (id == ART_DOOR_ARCHWAY) {
		return PNG_BUNDLE_SINGLE(door_archway_png);
	} else if (id == ART_WINDOW_HATCH) {
		return PNG_BUNDLE(window_hatch_small_png, window_hatch_png);
	} else if (id == ART_WINDOW_NORMAL) {
		return PNG_BUNDLE(window_normal_small_png, window_normal_png);
	}

	return wxBitmapBundle();
}
