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

wxBitmap ArtProvider::CreateBitmap(const wxArtID& id, const wxArtClient& client, const wxSize& WXUNUSED(size)) {
	if (client == wxART_TOOLBAR) {
		if (id == ART_CIRCULAR) {
			return PNG_BITMAP(icon_circular_4_png);
		} else if (id == ART_CIRCULAR_1) {
			return PNG_BITMAP(icon_circular_1_png);
		} else if (id == ART_CIRCULAR_2) {
			return PNG_BITMAP(icon_circular_2_png);
		} else if (id == ART_CIRCULAR_3) {
			return PNG_BITMAP(icon_circular_3_png);
		} else if (id == ART_CIRCULAR_4) {
			return PNG_BITMAP(icon_circular_4_png);
		} else if (id == ART_CIRCULAR_5) {
			return PNG_BITMAP(icon_circular_5_png);
		} else if (id == ART_CIRCULAR_6) {
			return PNG_BITMAP(icon_circular_6_png);
		} else if (id == ART_CIRCULAR_7) {
			return PNG_BITMAP(icon_circular_7_png);
		} else if (id == ART_NOLOOUT_BRUSH) {
			return PNG_BITMAP(icon_nologout_zone_png);
		} else if (id == ART_NOPVP_BRUSH) {
			return PNG_BITMAP(icon_nopvp_zone_png);
		} else if (id == ART_POSITION_GO) {
			return PNG_BITMAP(icon_position_go_png);
		} else if (id == ART_PVP_BRUSH) {
			return PNG_BITMAP(icon_pvp_zone_png);
		} else if (id == ART_PZ_BRUSH) {
			return PNG_BITMAP(icon_protected_zone_png);
		} else if (id == ART_RECTANGULAR) {
			return PNG_BITMAP(icon_rectangular_4_png);
		} else if (id == ART_RECTANGULAR_1) {
			return PNG_BITMAP(icon_rectangular_1_png);
		} else if (id == ART_RECTANGULAR_2) {
			return PNG_BITMAP(icon_rectangular_2_png);
		} else if (id == ART_RECTANGULAR_3) {
			return PNG_BITMAP(icon_rectangular_3_png);
		} else if (id == ART_RECTANGULAR_4) {
			return PNG_BITMAP(icon_rectangular_4_png);
		} else if (id == ART_RECTANGULAR_5) {
			return PNG_BITMAP(icon_rectangular_5_png);
		} else if (id == ART_RECTANGULAR_6) {
			return PNG_BITMAP(icon_rectangular_6_png);
		} else if (id == ART_RECTANGULAR_7) {
			return PNG_BITMAP(icon_rectangular_7_png);
		}

		else if (id == ART_DOOR_NORMAL_SMALL) {
			return PNG_BITMAP(door_normal_small_png);
		} else if (id == ART_DOOR_LOCKED_SMALL) {
			return PNG_BITMAP(door_locked_small_png);
		} else if (id == ART_DOOR_MAGIC_SMALL) {
			return PNG_BITMAP(door_magic_small_png);
		} else if (id == ART_DOOR_QUEST_SMALL) {
			return PNG_BITMAP(door_quest_small_png);
		} else if (id == ART_DOOR_NORMAL_ALT_SMALL) {
			return PNG_BITMAP(door_normal_alt_small_png);
		} else if (id == ART_DOOR_ARCHWAY_SMALL) {
			return PNG_BITMAP(door_archway_small_png);
		}
	}

	return wxNullBitmap;
}
