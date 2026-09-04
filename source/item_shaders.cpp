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

#include "item_shaders.h"
#include "settings.h"
#include "gui.h"
#include "client_version.h"

#ifdef __APPLE__
	#include <GLUT/glut.h>
#else
	#include <GL/glut.h>
	#include <GL/freeglut_ext.h> // glutGetProcAddress
#endif

#include <chrono>
#include <regex>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filefn.h>

ItemShaders g_itemShaders;

// ============================================================================
// GL extension entry points (the system gl.h only covers GL 1.1 on Windows)

#ifndef GL_FRAGMENT_SHADER
	#define GL_FRAGMENT_SHADER 0x8B30
	#define GL_VERTEX_SHADER 0x8B31
	#define GL_COMPILE_STATUS 0x8B81
	#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_TEXTURE0
	#define GL_TEXTURE0 0x84C0
	#define GL_TEXTURE1 0x84C1
#endif

namespace {

typedef GLuint(APIENTRY* PFN_glCreateShader)(GLenum);
typedef void(APIENTRY* PFN_glShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void(APIENTRY* PFN_glCompileShader)(GLuint);
typedef void(APIENTRY* PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint(APIENTRY* PFN_glCreateProgram)(void);
typedef void(APIENTRY* PFN_glAttachShader)(GLuint, GLuint);
typedef void(APIENTRY* PFN_glLinkProgram)(GLuint);
typedef void(APIENTRY* PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFN_glUseProgram)(GLuint);
typedef GLint(APIENTRY* PFN_glGetUniformLocation)(GLuint, const char*);
typedef void(APIENTRY* PFN_glUniform1f)(GLint, GLfloat);
typedef void(APIENTRY* PFN_glUniform2f)(GLint, GLfloat, GLfloat);
typedef void(APIENTRY* PFN_glUniform1i)(GLint, GLint);
typedef void(APIENTRY* PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void(APIENTRY* PFN_glActiveTexture)(GLenum);
typedef void(APIENTRY* PFN_glDeleteShader)(GLuint);

PFN_glCreateShader p_glCreateShader = nullptr;
PFN_glShaderSource p_glShaderSource = nullptr;
PFN_glCompileShader p_glCompileShader = nullptr;
PFN_glGetShaderiv p_glGetShaderiv = nullptr;
PFN_glCreateProgram p_glCreateProgram = nullptr;
PFN_glAttachShader p_glAttachShader = nullptr;
PFN_glLinkProgram p_glLinkProgram = nullptr;
PFN_glGetProgramiv p_glGetProgramiv = nullptr;
PFN_glUseProgram p_glUseProgram = nullptr;
PFN_glGetUniformLocation p_glGetUniformLocation = nullptr;
PFN_glUniform1f p_glUniform1f = nullptr;
PFN_glUniform2f p_glUniform2f = nullptr;
PFN_glUniform1i p_glUniform1i = nullptr;
PFN_glUniformMatrix4fv p_glUniformMatrix4fv = nullptr;
PFN_glActiveTexture p_glActiveTexture = nullptr;
PFN_glDeleteShader p_glDeleteShader = nullptr;

template <typename T>
bool loadProc(T& target, const char* name) {
	target = reinterpret_cast<T>(glutGetProcAddress(name));
	return target != nullptr;
}

bool loadGLFunctions() {
	bool ok = true;
	ok &= loadProc(p_glCreateShader, "glCreateShader");
	ok &= loadProc(p_glShaderSource, "glShaderSource");
	ok &= loadProc(p_glCompileShader, "glCompileShader");
	ok &= loadProc(p_glGetShaderiv, "glGetShaderiv");
	ok &= loadProc(p_glCreateProgram, "glCreateProgram");
	ok &= loadProc(p_glAttachShader, "glAttachShader");
	ok &= loadProc(p_glLinkProgram, "glLinkProgram");
	ok &= loadProc(p_glGetProgramiv, "glGetProgramiv");
	ok &= loadProc(p_glUseProgram, "glUseProgram");
	ok &= loadProc(p_glGetUniformLocation, "glGetUniformLocation");
	ok &= loadProc(p_glUniform1f, "glUniform1f");
	ok &= loadProc(p_glUniform2f, "glUniform2f");
	ok &= loadProc(p_glUniform1i, "glUniform1i");
	ok &= loadProc(p_glUniformMatrix4fv, "glUniformMatrix4fv");
	ok &= loadProc(p_glActiveTexture, "glActiveTexture");
	ok &= loadProc(p_glDeleteShader, "glDeleteShader");
	return ok;
}

// Seconds since the first shader was drawn, wrapped to keep float precision.
float shaderTimeSeconds() {
	static const auto start = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	return static_cast<float>(elapsed % 3600000) / 1000.f;
}

// ============================================================================
// Compatibility vertex shader
//
// The client's vertex shaders feed generic attribute streams that do not
// exist in the editor's immediate-mode drawing, so the fragment shaders are
// paired with this stand-in. It provides the same varyings: v_TexCoord (the
// sprite), v_TexCoord2 (the outfit color mask - identical for items) and
// v_TexCoord3 (the scrolling u_Tex1 effect lookup). The per-shader scroll
// constants (effectTextureSize / direction / speed) are parsed from the
// client's vertex source and passed in as uniforms.
const char* COMPAT_VERTEX_SHADER = "varying vec2 v_TexCoord;\n"
								   "varying vec2 v_TexCoord2;\n"
								   "varying vec2 v_TexCoord3;\n"
								   "varying vec2 v_Position;\n"
								   "varying vec4 v_Tint;\n"
								   "uniform float u_Time;\n"
								   "uniform vec2 u_Offset;\n"
								   "uniform vec2 u_EffectSize;\n"
								   "uniform vec2 u_Direction;\n"
								   "uniform float u_Speed;\n"
								   "uniform float u_SpriteScale;\n"
								   "void main() {\n"
								   "	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
								   "	v_TexCoord = gl_MultiTexCoord0.xy;\n"
								   "	v_TexCoord2 = gl_MultiTexCoord0.xy + u_Offset;\n"
								   // Scroll-family shaders (u_SpriteScale 32, parsed constants) get a
								   // moving effect-texture lookup; for every other family this reduces
								   // to the plain texcoord (scale 1, size 1, speed 0), matching vertex
								   // shaders that pass a_TexCoord through to v_TexCoord3 unchanged.
								   "	v_TexCoord3 = (gl_MultiTexCoord0.xy * u_SpriteScale + u_Direction * u_Speed * u_Time) / u_EffectSize;\n"
								   "	v_Position = gl_MultiTexCoord0.xy * 32.0 - vec2(16.0, 16.0);\n"
								   "	v_Tint = gl_Color;\n"
								   "}\n";

bool readTextFile(const wxString& path, std::string& out) {
	wxFile file(path, wxFile::read);
	if (!file.IsOpened()) {
		return false;
	}
	const wxFileOffset length = file.Length();
	out.resize(static_cast<size_t>(length));
	return length == 0 || file.Read(&out[0], out.size()) == static_cast<ssize_t>(out.size());
}

// Parses "name = vec2(x, y)" / "name = value" constants out of the client's
// vertex shader source.
bool parseVec2Constant(const std::string& source, const char* name, float& x, float& y) {
	const std::regex pattern(std::string(name) + "\\s*=\\s*vec2\\(\\s*(-?[0-9.]+)\\s*,\\s*(-?[0-9.]+)");
	std::smatch match;
	if (std::regex_search(source, match, pattern)) {
		x = std::stof(match[1]);
		y = std::stof(match[2]);
		return true;
	}
	return false;
}

bool parseFloatConstant(const std::string& source, const char* name, float& value) {
	const std::regex pattern(std::string(name) + "\\s*=\\s*(-?[0-9.]+)\\s*;");
	std::smatch match;
	if (std::regex_search(source, match, pattern)) {
		value = std::stof(match[1]);
		return true;
	}
	return false;
}

GLuint compileStage(GLenum type, const std::string& source) {
	const GLuint shader = p_glCreateShader(type);
	if (shader == 0) {
		return 0;
	}
	const char* text = source.c_str();
	p_glShaderSource(shader, 1, &text, nullptr);
	p_glCompileShader(shader);
	GLint status = 0;
	p_glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == 0) {
		p_glDeleteShader(shader);
		return 0;
	}
	return shader;
}

GLuint loadEffectTexture(const wxString& path) {
	wxImage image;
	if (!wxFileExists(path) || !image.LoadFile(path)) {
		return 0;
	}
	const int width = image.GetWidth();
	const int height = image.GetHeight();
	const unsigned char* rgb = image.GetData();
	const unsigned char* alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
	for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
		rgba[i * 4 + 0] = rgb[i * 3 + 0];
		rgba[i * 4 + 1] = rgb[i * 3 + 1];
		rgba[i * 4 + 2] = rgb[i * 3 + 2];
		rgba[i * 4 + 3] = alpha ? alpha[i] : 255;
	}

	GLuint texture = 0;
	glGenTextures(1, &texture);
	if (texture == 0) {
		return 0;
	}
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	return texture;
}

// The OTClient data directory holding shaders/: the SHADERS_DIRECTORY setting
// when set, otherwise found by walking up from the client assets path.
wxString findShaderDataRoot() {
	const std::string configured = g_settings.getString(Config::SHADERS_DIRECTORY);
	if (!configured.empty()) {
		wxFileName dir;
		dir.AssignDir(wxstr(configured));
		if (dir.DirExists()) {
			return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		}
	}

	if (g_gui.GetCurrentVersionID() == CLIENT_VERSION_NONE) {
		return wxString();
	}
	FileName client_path = g_gui.GetCurrentVersion().getClientPath();
	wxFileName walker;
	walker.AssignDir(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR));
	for (int depth = 0; depth < 5; ++depth) {
		const wxString root = walker.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		if (wxDirExists(root + "shaders")) {
			return root;
		}
		if (walker.GetDirCount() == 0) {
			break;
		}
		walker.RemoveLastDir();
	}
	return wxString();
}

// "/shaders/outfit/x_vertex" -> "<root>shaders/outfit/x_vertex[.frag]"
wxString resolveShaderPath(const wxString& root, const std::string& path) {
	std::string relative = path;
	if (!relative.empty() && (relative[0] == '/' || relative[0] == '\\')) {
		relative.erase(0, 1);
	}
	wxString full = root + wxstr(relative);
	full.Replace("/", wxString(wxFileName::GetPathSeparator()));
	if (wxFileExists(full)) {
		return full;
	}
	if (wxFileExists(full + ".frag")) {
		return full + ".frag";
	}
	return wxString();
}

} // namespace

// ============================================================================
// ItemShaders

void ItemShaders::initialize() {
	initialized = true;

	const wxString root = findShaderDataRoot();
	if (root.IsEmpty()) {
		return;
	}

	// Preferred source of truth: the client's shader registrations.
	const wxString modulesDir = root + "modules" + wxFileName::GetPathSeparator() + "game_shaders";
	if (wxDirExists(modulesDir)) {
		wxArrayString luaFiles;
		wxDir::GetAllFiles(modulesDir, &luaFiles, "*.lua", wxDIR_FILES);
		const std::regex createPattern("create\\w*Shader\\s*\\(\\s*[\"']([^\"']+)[\"']\\s*,\\s*[\"']([^\"']+)[\"']\\s*,\\s*[\"']([^\"']+)[\"']");
		const std::regex texturePattern("addTexture\\s*\\(\\s*[\"']([^\"']+)[\"']\\s*,\\s*[\"']([^\"']+)[\"']");
		for (size_t i = 0; i < luaFiles.GetCount(); ++i) {
			std::string contents;
			if (!readTextFile(luaFiles[i], contents)) {
				continue;
			}
			for (std::sregex_iterator it(contents.begin(), contents.end(), createPattern), end; it != end; ++it) {
				ShaderDef& def = definitions[(*it)[1]];
				def.vertex_file = nstr(resolveShaderPath(root, (*it)[2]));
				def.fragment_file = nstr(resolveShaderPath(root, (*it)[3]));
			}
			for (std::sregex_iterator it(contents.begin(), contents.end(), texturePattern), end; it != end; ++it) {
				auto found = definitions.find((*it)[1]);
				if (found != definitions.end()) {
					found->second.texture_file = nstr(resolveShaderPath(root, (*it)[2]));
				}
			}
		}
	}

	// Fallback / supplement: scan the shaders folder by naming convention
	// (<name>_fragment.frag [+ <name>_vertex.frag]).
	wxArrayString fragFiles;
	wxDir::GetAllFiles(root + "shaders", &fragFiles, "*_fragment.frag", wxDIR_FILES | wxDIR_DIRS);
	for (size_t i = 0; i < fragFiles.GetCount(); ++i) {
		const wxFileName file(fragFiles[i]);
		wxString name = file.GetName(); // "<name>_fragment"
		name = name.Left(name.length() - wxString("_fragment").length());
		const std::string key = nstr(name);
		if (definitions.count(key) != 0) {
			continue;
		}
		ShaderDef& def = definitions[key];
		def.fragment_file = nstr(fragFiles[i]);
		wxString vertex = file.GetPathWithSep() + name + "_vertex.frag";
		if (wxFileExists(vertex)) {
			def.vertex_file = nstr(vertex);
		}
	}
}

bool ItemShaders::compile(const ShaderDef& def, CompiledShader& out) {
	std::string fragmentSource;
	if (def.fragment_file.empty() || !readTextFile(wxstr(def.fragment_file), fragmentSource)) {
		return false;
	}

	// Wrap the client's fragment shader so the editor's per-quad color still
	// modulates the result (selection darkening, transparent-items mode,
	// locked-door highlight). Fixed-function drawing gets this for free from
	// glColor; shaders bypass it, so multiply it back in around their main().
	fragmentSource = std::regex_replace(fragmentSource, std::regex("void\\s+main\\s*\\("), "void shaderMain(");
	fragmentSource += "\n"
					  "varying vec4 v_Tint;\n"
					  "void main() {\n"
					  "	shaderMain();\n"
					  "	gl_FragColor *= v_Tint;\n"
					  "}\n";

	// Two v_TexCoord3 families exist in the client's vertex shaders: the
	// scroll family (effectTextureSize / direction / speed constants, feeding
	// a moving u_Tex1 lookup) and the pass-through family (v_TexCoord3 is the
	// plain texcoord, often used to re-sample the sprite itself). Detect
	// which one this shader pairs with and set the uniforms accordingly -
	// using the scroll formula for a pass-through shader makes discard-based
	// effects sample the wrong texels and visibly cut the sprite off.
	float spriteScale = 1.f;
	float effectWidth = 1.f, effectHeight = 1.f;
	float directionX = 0.f, directionY = 0.f;
	float speed = 0.f;
	std::string vertexSource;
	if (!def.vertex_file.empty() && readTextFile(wxstr(def.vertex_file), vertexSource) && vertexSource.find("effectTextureSize") != std::string::npos) {
		spriteScale = 32.f;
		effectWidth = effectHeight = 64.f;
		directionX = directionY = 1.f;
		parseVec2Constant(vertexSource, "effectTextureSize", effectWidth, effectHeight);
		parseVec2Constant(vertexSource, "direction", directionX, directionY);
		parseFloatConstant(vertexSource, "speed", speed);
	}

	const GLuint vertexStage = compileStage(GL_VERTEX_SHADER, COMPAT_VERTEX_SHADER);
	const GLuint fragmentStage = compileStage(GL_FRAGMENT_SHADER, fragmentSource);
	if (vertexStage == 0 || fragmentStage == 0) {
		if (vertexStage != 0) {
			p_glDeleteShader(vertexStage);
		}
		if (fragmentStage != 0) {
			p_glDeleteShader(fragmentStage);
		}
		return false;
	}

	const GLuint program = p_glCreateProgram();
	p_glAttachShader(program, vertexStage);
	p_glAttachShader(program, fragmentStage);
	p_glLinkProgram(program);
	p_glDeleteShader(vertexStage);
	p_glDeleteShader(fragmentStage);
	GLint status = 0;
	p_glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == 0) {
		return false;
	}

	out.program = program;
	out.loc_time = p_glGetUniformLocation(program, "u_Time");
	if (!def.texture_file.empty()) {
		out.effect_texture = loadEffectTexture(wxstr(def.texture_file));
	}

	// Static uniforms: sampler bindings, no-op outfit colorization, and the
	// scroll constants mirrored from the client's vertex shader.
	p_glUseProgram(program);
	p_glUniform1i(p_glGetUniformLocation(program, "u_Tex0"), 0);
	p_glUniform1i(p_glGetUniformLocation(program, "u_Tex1"), 1);
	static const GLfloat allOnes[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	p_glUniformMatrix4fv(p_glGetUniformLocation(program, "u_Color"), 1, GL_FALSE, allOnes);
	p_glUniform2f(p_glGetUniformLocation(program, "u_Offset"), 0.f, 0.f);
	p_glUniform2f(p_glGetUniformLocation(program, "u_EffectSize"), effectWidth, effectHeight);
	p_glUniform2f(p_glGetUniformLocation(program, "u_Direction"), directionX, directionY);
	p_glUniform1f(p_glGetUniformLocation(program, "u_Speed"), speed);
	p_glUniform1f(p_glGetUniformLocation(program, "u_SpriteScale"), spriteScale);
	p_glUseProgram(0);
	return true;
}

ItemShaders::CompiledShader* ItemShaders::getCompiled(const std::string& name) {
	auto found = compiled.find(name);
	if (found != compiled.end()) {
		return &found->second;
	}

	CompiledShader& shader = compiled[name];
	auto def = definitions.find(name);
	if (def == definitions.end() || !compile(def->second, shader)) {
		shader.failed = true;
	}
	return &shader;
}

bool ItemShaders::apply(const std::string& name) {
	if (name.empty()) {
		return false;
	}
	if (!initialized) {
		initialize();
	}
	if (!functions_loaded) {
		// Deferred to here so it happens with the canvas GL context current.
		functions_loaded = true;
		functions_ok = loadGLFunctions();
	}
	if (!functions_ok) {
		return false;
	}

	CompiledShader* shader = getCompiled(name);
	if (shader->failed || shader->program == 0) {
		return false;
	}

	p_glUseProgram(shader->program);
	if (shader->loc_time >= 0) {
		p_glUniform1f(shader->loc_time, shaderTimeSeconds());
	}
	if (shader->effect_texture != 0) {
		p_glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, shader->effect_texture);
		p_glActiveTexture(GL_TEXTURE0);
	}
	drawn_this_frame = true;
	return true;
}

void ItemShaders::clear() {
	p_glUseProgram(0);
}

std::vector<std::string> ItemShaders::getShaderNames() {
	if (!initialized) {
		initialize();
	}
	std::vector<std::string> names;
	names.reserve(definitions.size());
	for (const auto& entry : definitions) {
		names.push_back(entry.first);
	}
	return names; // std::map iteration keeps them sorted
}

bool ItemShaders::takeDrawnFlag() {
	const bool drawn = drawn_this_frame;
	drawn_this_frame = false;
	return drawn;
}

void ItemShaders::unload() {
	// GL objects are intentionally left alone (no context guarantee here);
	// definitions are re-discovered on the next apply().
	initialized = false;
	definitions.clear();
	compiled.clear();
}
