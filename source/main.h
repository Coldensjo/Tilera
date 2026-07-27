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

#ifndef RME_MAIN_H_
#define RME_MAIN_H_

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#ifdef _WIN32_WINNT
		#undef _WIN32_WINNT
	#endif
	#define _WIN32_WINNT 0x0501
#endif

#ifdef DEBUG_MEM

	#define _CRTDBG_MAP_ALLOC

	#include <stdlib.h>
	#include <crtdbg.h>

	#pragma warning(disable : 4291)
_Ret_bytecap_(_Size) inline void* __CRTDECL operator new(size_t _Size, const char* file, int line) {
	return ::operator new(_Size, _NORMAL_BLOCK, file, line);
}
_Ret_bytecap_(_Size) inline void* __CRTDECL operator new[](size_t _Size, const char* file, int line) {
	return ::operator new[](_Size, _NORMAL_BLOCK, file, line);
}
	#define newd new (__FILE__, __LINE__)

#else

	#define newd new

#endif

#if defined(_MSC_VER)
	// Legacy codebase + wx DLL consumer: benign /W3 noise (applied via PCH to all TUs)
	#pragma warning(disable : 4244) // narrowing conversions (OpenGL, legacy math)
	#pragma warning(disable : 4251) // wx STL members in exported classes
	#pragma warning(disable : 4267) // size_t to smaller integer
	#pragma warning(disable : 4275) // wx base-class DLL export mismatch
	#pragma warning(disable : 4996) // deprecated wx APIs
#endif

// Boost libraries
#include <boost/utility.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/asio.hpp>

#include <wx/defs.h>
#include "definitions.h"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
	#include <wx/wx.h>
#endif
#include <wx/thread.h>
#include <wx/utils.h>
#include <wx/progdlg.h>
#include <wx/glcanvas.h>
#include <wx/debugrpt.h>
#include <wx/minifram.h>
#include <wx/gbsizer.h>
#include <wx/choicebk.h>
#include <wx/tglbtn.h>
#include <wx/dcbuffer.h>
#include <wx/aui/aui.h>
#include <wx/cmdline.h>
#include <wx/filename.h>
#include <wx/filepicker.h>
#include <wx/arrstr.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/wxhtml.h>
#include <wx/vlbox.h>
#include <wx/stdpaths.h>
#include <wx/url.h>
#include <wx/ipc.h>
#include <wx/grid.h>
#include <wx/clipbrd.h>

// PugiXML
#include "ext/pugixml.hpp"

// Libarchive, for OTGZ
#ifdef OTGZ_SUPPORT
	#include <archive.h>
	#include <archive_entry.h>
#endif

// This has annoyed me one time too many
#define wxANY_ID (wxID_ANY)

#include <assert.h>
#define _MSG(msg) !bool(msg)
#ifdef __DEBUG__
	#define ASSERT assert
#else
	#define ASSERT(...)
#endif

// The complete STL ?, well, almost ;)
#include <math.h>
#include <list>
#include <vector>
#include <map>
#include <string>
#include <istream>
#include <ostream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <set>
#include <queue>
#include <stdexcept>
#include <time.h>
#include <fstream>

typedef std::vector<std::string> StringVector;
typedef wxFileName FileName;

#include "json.h"

#include "con_vector.h"
#include "common.h"

#include "rme_forward_declarations.h"

#if wxCHECK_VERSION(3, 1, 0)
	#define FROM_DIP(widget, size) widget->FromDIP(size)
#else
	#define FROM_DIP(widget, size) size
#endif

// Replacement for the deprecated wxPATH_NORM_ALL (wxWidgets 3.2 wants the flags
// spelled out): expand environment variables and a leading "~", collapse "." and
// "..", and make the path absolute. The long-name/shortcut resolution that
// wxPATH_NORM_ALL also implied is intentionally left out — it touches the
// filesystem and none of the callers need it.
static const int RME_PATH_NORM_FLAGS = wxPATH_NORM_ENV_VARS | wxPATH_NORM_TILDE | wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE;

inline bool isSelectAllShortcut(const wxKeyEvent& event) {
	if (!(event.ControlDown() || event.CmdDown())) {
		return false;
	}

	int keyCode = event.GetKeyCode();
	if (keyCode == WXK_NONE || keyCode == 0) {
		keyCode = event.GetUnicodeKey();
	}

	if (keyCode >= WXK_CONTROL_A && keyCode <= WXK_CONTROL_Z) {
		keyCode = 'A' + (keyCode - WXK_CONTROL_A);
	}

	if (keyCode >= 1 && keyCode <= 26) {
		keyCode = 'A' + (keyCode - 1);
	}

	if (keyCode == WXK_NONE || keyCode == 0) {
		return false;
	}

	return wxToupper(static_cast<wxChar>(keyCode)) == 'A';
}

#endif
