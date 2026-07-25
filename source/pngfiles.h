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

#ifndef RME_PNG_HEADER_FILE_H_
#define RME_PNG_HEADER_FILE_H_

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/mstream.h>

/**
 * Decodes one of the embedded PNG blobs declared below into a bitmap.
 *
 * Prefer the PNG_BITMAP() macro, which supplies the length automatically.
 * Returns wxNullBitmap if the data is not a valid PNG.
 */
inline wxBitmap wxBitmapFromPNG(const unsigned char* data, size_t length) {
	wxMemoryInputStream stream(data, length);
	wxImage image(stream, "image/png");
	if (!image.IsOk()) {
		return wxNullBitmap;
	}
	return wxBitmap(image, -1);
}

// Builds a wxBitmap from an embedded PNG array, e.g. PNG_BITMAP(icon_circular_4_png).
#define PNG_BITMAP(name) wxBitmapFromPNG(name, sizeof(name))

extern unsigned char circular_1_png[410];
extern unsigned char circular_1_small_png[168];
extern unsigned char circular_2_png[560];
extern unsigned char circular_2_small_png[193];
extern unsigned char circular_3_png[718];
extern unsigned char circular_3_small_png[390];
extern unsigned char circular_4_png[830];
extern unsigned char circular_4_small_png[467];
extern unsigned char circular_5_png[990];
extern unsigned char circular_5_small_png[532];
extern unsigned char circular_6_png[1110];
extern unsigned char circular_6_small_png[607];
extern unsigned char circular_7_png[1154];
extern unsigned char circular_7_small_png[591];
extern unsigned char door_locked_png[738];
extern unsigned char door_locked_small_png[596];
extern unsigned char door_magic_png[943];
extern unsigned char door_magic_small_png[580];
extern unsigned char door_normal_png[821];
extern unsigned char door_normal_small_png[497];
extern unsigned char door_quest_png[842];
extern unsigned char door_quest_small_png[558];
extern unsigned char eraser_png[878];
extern unsigned char eraser_small_png[667];
extern unsigned char exclamation_png[256];
extern unsigned char gem_edit_png[697];
extern unsigned char gem_move_png[662];
extern unsigned char no_logout_png[782];
extern unsigned char no_logout_small_png[670];
extern unsigned char no_pvp_png[726];
extern unsigned char no_pvp_small_png[630];
extern unsigned char optional_border_png[768];
extern unsigned char optional_border_small_png[544];
extern unsigned char protection_zone_png[961];
extern unsigned char protection_zone_small_png[687];
extern unsigned char pvp_zone_png[677];
extern unsigned char pvp_zone_small_png[620];
extern unsigned char rectangular_1_png[226];
extern unsigned char rectangular_1_small_png[169];
extern unsigned char rectangular_2_png[275];
extern unsigned char rectangular_2_small_png[182];
extern unsigned char rectangular_3_png[323];
extern unsigned char rectangular_3_small_png[207];
extern unsigned char rectangular_4_png[371];
extern unsigned char rectangular_4_small_png[228];
extern unsigned char rectangular_5_png[406];
extern unsigned char rectangular_5_small_png[252];
extern unsigned char rectangular_6_png[433];
extern unsigned char rectangular_6_small_png[273];
extern unsigned char rectangular_7_png[446];
extern unsigned char rectangular_7_small_png[284];
extern unsigned char refresh_png[595];
extern unsigned char refresh_small_png[563];
extern unsigned char window_hatch_png[910];
extern unsigned char window_hatch_small_png[641];
extern unsigned char window_normal_png[947];
extern unsigned char window_normal_small_png[641];
extern unsigned char icon_circular_1_png[168];
extern unsigned char icon_rectangular_1_png[169];
extern unsigned char icon_circular_2_png[193];
extern unsigned char icon_rectangular_2_png[182];
extern unsigned char icon_circular_3_png[390];
extern unsigned char icon_rectangular_3_png[207];
extern unsigned char icon_circular_4_png[467];
extern unsigned char icon_rectangular_4_png[228];
extern unsigned char icon_circular_5_png[532];
extern unsigned char icon_rectangular_5_png[252];
extern unsigned char icon_circular_6_png[607];
extern unsigned char icon_rectangular_6_png[273];
extern unsigned char icon_circular_7_png[591];
extern unsigned char icon_rectangular_7_png[284];
extern unsigned char icon_nologout_zone_png[671];
extern unsigned char icon_nopvp_zone_png[651];
extern unsigned char icon_position_go_png[410];
extern unsigned char icon_protected_zone_png[672];
extern unsigned char icon_pvp_zone_png[665];
extern unsigned char door_archway_png[6042];
extern unsigned char door_archway_small_png[5481];
extern unsigned char door_normal_alt_png[6713];
extern unsigned char door_normal_alt_small_png[5773];
extern unsigned char editor_icon_png[776];
extern unsigned char icon_terrain_png[2499];
extern unsigned char icon_doodad_png[975];
extern unsigned char icon_item_png[1713];
extern unsigned char icon_house_png[1429];
extern unsigned char icon_creature_png[896];
extern unsigned char icon_raw_png[1600];
extern unsigned char icon_comments_png[1474];
#endif //_RME_PNG_HEADER_FILE_H_
