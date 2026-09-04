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

#ifndef RME_ITEM_SHADERS_H_
#define RME_ITEM_SHADERS_H_

#include "main.h"

// Renders OTClient-style item shaders (items.xml <attribute key="shader">)
// on the map canvas. Shader sources are discovered from the client data
// directory: registrations are parsed from modules/game_shaders/*.lua
// (g_shaders.create*Shader / addTexture calls), with a fallback scan of the
// shaders/ folder by *_fragment.frag naming convention. The client's fragment
// shaders are compiled as-is; a compatibility vertex shader replaces the
// client's (its scroll constants are parsed out of the original), since the
// editor draws with the fixed-function pipeline.
//
// The data directory is auto-detected by walking up from the client assets
// path until a "shaders" folder is found, and can be overridden with the
// SHADERS_DIRECTORY setting.
class ItemShaders {
public:
	// Binds the named shader program, compiling it on first use. Must be
	// called with the map canvas GL context current. Returns false when the
	// shader is unknown or failed to compile - draw unshaded then.
	bool apply(const std::string& name);
	// Restores fixed-function drawing after apply() returned true.
	void clear();

	// True when a shader was applied since the last call - lets the canvas
	// keep refreshing so u_Time-based effects animate.
	bool takeDrawnFlag();

	// All discovered shader names, sorted. Safe to call without a GL context
	// (discovery only reads files); used for value suggestions in the item
	// editor.
	std::vector<std::string> getShaderNames();

	// Forget all discovered definitions; they are re-discovered on the next
	// apply(). (GL objects are not touched - safe without a current context.)
	void unload();

private:
	struct ShaderDef {
		std::string vertex_file; // may be empty (default vertex shader)
		std::string fragment_file;
		std::string texture_file; // may be empty (no u_Tex1)
	};

	struct CompiledShader {
		unsigned int program = 0;
		unsigned int effect_texture = 0;
		bool failed = false;
		int loc_time = -1;
	};

	void initialize();
	CompiledShader* getCompiled(const std::string& name);
	bool compile(const ShaderDef& def, CompiledShader& out);

	bool initialized = false;
	bool functions_loaded = false;
	bool functions_ok = false;
	bool drawn_this_frame = false;
	std::map<std::string, ShaderDef> definitions;
	std::map<std::string, CompiledShader> compiled;
};

extern ItemShaders g_itemShaders;

#endif
