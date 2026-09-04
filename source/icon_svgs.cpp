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

#include "icon_svgs.h"

#include "artprovider.h"

namespace {

// Wraps the inner drawing in the shared 24x24 stroke frame.
#define SVG_ICON(body) \
	"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" " \
	"stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">" body "</svg>"

struct SvgIcon {
	const char* id;
	const char* svg;
};

// clang-format off
const SvgIcon kIcons[] = {
	// Files
	{ "wxART_NEW",          SVG_ICON("<path d=\"M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z\"/><path d=\"M14 3v6h6\"/><path d=\"M12 12v6M9 15h6\"/>") },
	{ "wxART_FILE_OPEN",    SVG_ICON("<path d=\"M3 6a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z\"/>") },
	{ "wxART_FILE_SAVE",    SVG_ICON("<path d=\"M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z\"/><path d=\"M17 21v-8H7v8\"/><path d=\"M7 3v5h8\"/>") },
	{ "wxART_FILE_SAVE_AS", SVG_ICON("<path d=\"M11 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v3\"/><path d=\"M7 3v5h8\"/><path d=\"M7 21v-6h4\"/><path d=\"m16 13 3 3-5 5h-3v-3z\"/>") },
	{ "wxART_CLOSE",        SVG_ICON("<path d=\"M18 6 6 18\"/><path d=\"m6 6 12 12\"/>") },
	{ "wxART_QUIT",         SVG_ICON("<path d=\"M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4\"/><path d=\"m16 17 5-5-5-5\"/><path d=\"M21 12H9\"/>") },
	{ "ART_IMPORT",         SVG_ICON("<path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/><path d=\"m7 10 5 5 5-5\"/><path d=\"M12 15V3\"/>") },
	{ "ART_EXPORT",         SVG_ICON("<path d=\"M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4\"/><path d=\"m17 8-5-5-5 5\"/><path d=\"M12 3v12\"/>") },
	{ "ART_RELOAD",         SVG_ICON("<path d=\"M21 12a9 9 0 0 0-9-9 9.75 9.75 0 0 0-6.74 2.74L3 8\"/><path d=\"M3 3v5h5\"/><path d=\"M3 12a9 9 0 0 0 9 9 9.75 9.75 0 0 0 6.74-2.74L21 16\"/><path d=\"M16 16h5v5\"/>") },
	{ "ART_GENERATE",       SVG_ICON("<path d=\"M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M5.6 18.4l2.1-2.1M16.3 7.7l2.1-2.1\"/>") },
	{ "ART_PREFERENCES",    SVG_ICON("<path d=\"M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3\"/><path d=\"M1 14h6M9 8h6M17 16h6\"/>") },

	// Editing
	{ "wxART_UNDO",         SVG_ICON("<path d=\"M9 14 4 9l5-5\"/><path d=\"M4 9h10a6 6 0 0 1 0 12h-3\"/>") },
	{ "wxART_REDO",         SVG_ICON("<path d=\"m15 14 5-5-5-5\"/><path d=\"M20 9H10a6 6 0 0 0 0 12h3\"/>") },
	{ "wxART_CUT",          SVG_ICON("<circle cx=\"6\" cy=\"6\" r=\"3\"/><circle cx=\"6\" cy=\"18\" r=\"3\"/><path d=\"M20 4 8.1 15.9\"/><path d=\"M14.5 14.5 20 20\"/><path d=\"M8.1 8.1 12 12\"/>") },
	{ "wxART_COPY",         SVG_ICON("<rect x=\"9\" y=\"9\" width=\"12\" height=\"12\" rx=\"2\"/><path d=\"M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1\"/>") },
	{ "wxART_PASTE",        SVG_ICON("<rect x=\"8\" y=\"2\" width=\"8\" height=\"4\" rx=\"1\"/><path d=\"M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2\"/>") },
	{ "wxART_FIND",         SVG_ICON("<circle cx=\"11\" cy=\"11\" r=\"7\"/><path d=\"m21 21-4.3-4.3\"/>") },
	{ "wxART_FIND_AND_REPLACE", SVG_ICON("<path d=\"M4 8h13l-3-3\"/><path d=\"M20 16H7l3 3\"/>") },
	{ "ART_CLEANUP",        SVG_ICON("<path d=\"M3 6h18\"/><path d=\"M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6\"/><path d=\"M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2\"/>") },
	{ "ART_FLIP_HORIZONTAL", SVG_ICON("<path d=\"M12 3v18\"/><path d=\"M8 7 4 12l4 5\"/><path d=\"m16 7 4 5-4 5\"/>") },
	{ "ART_FLIP_VERTICAL",  SVG_ICON("<path d=\"M3 12h18\"/><path d=\"m7 8 5-4 5 4\"/><path d=\"m7 16 5 4 5-4\"/>") },
	{ "ART_ROTATE_CW",      SVG_ICON("<path d=\"M21 12a9 9 0 1 1-9-9 9.75 9.75 0 0 1 6.74 2.74L21 8\"/><path d=\"M21 3v5h-5\"/>") },
	{ "ART_ROTATE_CCW",     SVG_ICON("<path d=\"M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8\"/><path d=\"M3 3v5h5\"/>") },
	{ "ART_MOVE_UP",        SVG_ICON("<path d=\"M12 19V5\"/><path d=\"m5 12 7-7 7 7\"/>") },
	{ "ART_MOVE_DOWN",      SVG_ICON("<path d=\"M12 5v14\"/><path d=\"m19 12-7 7-7-7\"/>") },

	// Navigation and view
	{ "ART_POSITION_GO",    SVG_ICON("<path d=\"M5 12h14\"/><path d=\"m12 5 7 7-7 7\"/>") },
	{ "ART_GOTO_POSITION",  SVG_ICON("<circle cx=\"12\" cy=\"12\" r=\"7\"/><path d=\"M12 2v4M12 18v4M2 12h4M18 12h4\"/>") },
	{ "ART_GOTO_PREVIOUS",  SVG_ICON("<path d=\"M19 12H5\"/><path d=\"m12 19-7-7 7-7\"/>") },
	{ "ART_JUMP_TO_BRUSH",  SVG_ICON("<path d=\"m9.06 11.9 8.07-8.06a2.85 2.85 0 1 1 4.03 4.03l-8.06 8.08\"/><path d=\"M7.07 14.94c-1.66 0-3 1.35-3 3.02 0 1.33-2.5 1.52-2 2.02 1.08 1.1 2.49 2.02 4 2.02 2.2 0 4-1.8 4-4.04a3.01 3.01 0 0 0-3-3.02z\"/>") },
	{ "ART_ZOOM_IN",        SVG_ICON("<circle cx=\"11\" cy=\"11\" r=\"7\"/><path d=\"m21 21-4.3-4.3\"/><path d=\"M11 8v6M8 11h6\"/>") },
	{ "ART_ZOOM_OUT",       SVG_ICON("<circle cx=\"11\" cy=\"11\" r=\"7\"/><path d=\"m21 21-4.3-4.3\"/><path d=\"M8 11h6\"/>") },
	{ "ART_ZOOM_RESET",     SVG_ICON("<circle cx=\"11\" cy=\"11\" r=\"7\"/><path d=\"m21 21-4.3-4.3\"/><path d=\"M9.5 9.5 11 8v6\"/>") },
	{ "wxART_FULL_SCREEN",  SVG_ICON("<path d=\"M8 3H5a2 2 0 0 0-2 2v3M21 8V5a2 2 0 0 0-2-2h-3M3 16v3a2 2 0 0 0 2 2h3M16 21h3a2 2 0 0 0 2-2v-3\"/>") },
	{ "ART_NEW_VIEW",       SVG_ICON("<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><path d=\"M9 3v18\"/>") },
	{ "ART_MAP",            SVG_ICON("<path d=\"M3 6l6-3 6 3 6-3v15l-6 3-6-3-6 3z\"/><path d=\"M9 3v15M15 6v15\"/>") },
	{ "ART_SCREENSHOT",     SVG_ICON("<path d=\"M4 8a2 2 0 0 1 2-2h2l2-2h4l2 2h2a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2z\"/><circle cx=\"12\" cy=\"13\" r=\"3\"/>") },

	// Map and tools
	{ "ART_MAP_PROPERTIES", SVG_ICON("<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><path d=\"M7 8h10M7 12h10M7 16h6\"/>") },
	{ "ART_STATISTICS",     SVG_ICON("<path d=\"M3 3v18h18\"/><path d=\"M8 17V9M13 17V5M18 17v-7\"/>") },
	{ "ART_TOWNS",          SVG_ICON("<rect x=\"4\" y=\"2\" width=\"16\" height=\"20\" rx=\"2\"/><path d=\"M9 22v-4h6v4\"/><path d=\"M8 6h.01M12 6h.01M16 6h.01M8 10h.01M12 10h.01M16 10h.01M8 14h.01M12 14h.01M16 14h.01\"/>") },
	{ "ART_LIVE_CONNECT",   SVG_ICON("<path d=\"M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.5 1.5\"/><path d=\"M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.5-1.5\"/>") },
	{ "ART_LIVE_DISCONNECT", SVG_ICON("<path d=\"m18.84 12.25 1.72-1.71a5 5 0 0 0-7.07-7.07l-1.72 1.71\"/><path d=\"m5.17 11.75-1.71 1.71a5 5 0 0 0 7.07 7.07l1.71-1.71\"/><path d=\"M8 2v3M2 8h3M16 22v-3M22 16h-3\"/>") },
	{ "ART_HOTKEYS",        SVG_ICON("<rect x=\"2\" y=\"6\" width=\"20\" height=\"12\" rx=\"2\"/><path d=\"M6 10h.01M10 10h.01M14 10h.01M18 10h.01M8 14h8\"/>") },
	{ "ART_EXTENSIONS",     SVG_ICON("<path d=\"M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z\"/><path d=\"M3.3 7 12 12l8.7-5M12 22V12\"/>") },
	{ "ART_WEBSITE",        SVG_ICON("<circle cx=\"12\" cy=\"12\" r=\"10\"/><path d=\"M2 12h20\"/><path d=\"M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z\"/>") },
	{ "wxART_INFORMATION",  SVG_ICON("<circle cx=\"12\" cy=\"12\" r=\"10\"/><path d=\"M12 16v-4\"/><path d=\"M12 8h.01\"/>") },
};
// clang-format on

} // namespace

const char* FindIconSvg(const wxArtID& id) {
	for (const SvgIcon& icon : kIcons) {
		if (id == icon.id) {
			return icon.svg;
		}
	}
	return nullptr;
}
