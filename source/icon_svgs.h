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

#ifndef RME_ICON_SVGS_H_
#define RME_ICON_SVGS_H_

#include <wx/artprov.h>

// Editor-specific ids for the monochrome action icon family. Standard
// actions reuse the stock wxART_* ids so menus and toolbars stay consistent.
#define ART_GOTO_POSITION wxART_MAKE_ART_ID(ART_GOTO_POSITION)
#define ART_GOTO_PREVIOUS wxART_MAKE_ART_ID(ART_GOTO_PREVIOUS)
#define ART_JUMP_TO_BRUSH wxART_MAKE_ART_ID(ART_JUMP_TO_BRUSH)
#define ART_ZOOM_IN wxART_MAKE_ART_ID(ART_ZOOM_IN)
#define ART_ZOOM_OUT wxART_MAKE_ART_ID(ART_ZOOM_OUT)
#define ART_ZOOM_RESET wxART_MAKE_ART_ID(ART_ZOOM_RESET)
#define ART_IMPORT wxART_MAKE_ART_ID(ART_IMPORT)
#define ART_EXPORT wxART_MAKE_ART_ID(ART_EXPORT)
#define ART_RELOAD wxART_MAKE_ART_ID(ART_RELOAD)
#define ART_PREFERENCES wxART_MAKE_ART_ID(ART_PREFERENCES)
#define ART_MAP wxART_MAKE_ART_ID(ART_MAP)
#define ART_MAP_PROPERTIES wxART_MAKE_ART_ID(ART_MAP_PROPERTIES)
#define ART_STATISTICS wxART_MAKE_ART_ID(ART_STATISTICS)
#define ART_SCREENSHOT wxART_MAKE_ART_ID(ART_SCREENSHOT)
#define ART_LIVE_CONNECT wxART_MAKE_ART_ID(ART_LIVE_CONNECT)
#define ART_LIVE_DISCONNECT wxART_MAKE_ART_ID(ART_LIVE_DISCONNECT)
#define ART_TOWNS wxART_MAKE_ART_ID(ART_TOWNS)
#define ART_CLEANUP wxART_MAKE_ART_ID(ART_CLEANUP)
#define ART_HOTKEYS wxART_MAKE_ART_ID(ART_HOTKEYS)
#define ART_EXTENSIONS wxART_MAKE_ART_ID(ART_EXTENSIONS)
#define ART_WEBSITE wxART_MAKE_ART_ID(ART_WEBSITE)
#define ART_GENERATE wxART_MAKE_ART_ID(ART_GENERATE)
#define ART_NEW_VIEW wxART_MAKE_ART_ID(ART_NEW_VIEW)
#define ART_FLIP_HORIZONTAL wxART_MAKE_ART_ID(ART_FLIP_HORIZONTAL)
#define ART_FLIP_VERTICAL wxART_MAKE_ART_ID(ART_FLIP_VERTICAL)
#define ART_ROTATE_CW wxART_MAKE_ART_ID(ART_ROTATE_CW)
#define ART_ROTATE_CCW wxART_MAKE_ART_ID(ART_ROTATE_CCW)
#define ART_MOVE_UP wxART_MAKE_ART_ID(ART_MOVE_UP)
#define ART_MOVE_DOWN wxART_MAKE_ART_ID(ART_MOVE_DOWN)

/*
 * Returns the SVG source for a monochrome action icon, or nullptr when the
 * id has no vector icon. Every icon is a 24x24 stroke drawing that uses
 * "currentColor", which ArtProvider substitutes with the theme text colour
 * before rasterising, so one source serves both light and dark themes at
 * every DPI.
 */
const char* FindIconSvg(const wxArtID& id);

#endif // RME_ICON_SVGS_H_
