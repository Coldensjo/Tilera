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

#include <array>
#include <iterator>
#include <wx/display.h>
#include <wx/textfile.h>

#include "gui.h"
#include "main_menubar.h"

#include "editor.h"
#include "brush.h"
#include "map.h"
#include "sprites.h"
#include "materials.h"
#include "doodad_brush.h"
#include "spawn_brush.h"
#include "terraform.h"

#include "common_windows.h"
#include "result_window.h"
#include "comments_window.h"
#include "minimap_window.h"
#include "find_brush_window.h"
#include "palette_window.h"
#include "map_display.h"
#include "application.h"
#include "welcome_dialog.h"
#include "map_tab.h"
#include "live_socket.h"
#include "live_client.h"
#include "live_tab.h"
#include "map_view_state.h"

namespace {

void ConfigureMinimapAuiPane(wxAuiPaneInfo& info) {
	info.Caption("Minimap").CaptionVisible(false).Gripper(true).CloseButton(false);
}

} // namespace

#ifdef __WXOSX__
	#include <AGL/agl.h>
#endif

const wxEventType EVT_UPDATE_MENUS = wxNewEventType();

// Global GUI instance
GUI g_gui;

namespace {
class WindowFreezeGuard {
public:
	WindowFreezeGuard() : window_(nullptr), active_(false) { }
	explicit WindowFreezeGuard(wxWindow* window) : window_(nullptr), active_(false) {
		reset(window);
	}
	~WindowFreezeGuard() {
		release();
	}

	void reset(wxWindow* window) {
		if (window == window_ && active_) {
			return;
		}
		release();
		window_ = window;
		if (window_) {
			window_->Freeze();
			active_ = true;
		}
	}

	bool isActive() const {
		return active_;
	}

	void release() {
		if (window_ && active_) {
			window_->Thaw();
		}
		window_ = nullptr;
		active_ = false;
	}

private:
	wxWindow* window_;
	bool active_;
};

struct MapConfigSettings {
	MapConfigSettings() :
		hasClientVersion(false),
		hasClientDirectory(false),
		hasItemsDirectory(false),
		hasMonstersDirectory(false),
		hasNpcsDirectory(false),
		clientVersion(CLIENT_VERSION_NONE) {
	}

	bool hasClientVersion;
	bool hasClientDirectory;
	bool hasItemsDirectory;
	bool hasMonstersDirectory;
	bool hasNpcsDirectory;
	ClientVersionID clientVersion;
	wxString clientDirectory;
	wxString itemsDirectory;
	wxString monstersDirectory;
	wxString npcsDirectory;
	wxArrayString warnings;

	bool hasPathOverrides() const {
		return hasClientDirectory || hasItemsDirectory || hasMonstersDirectory || hasNpcsDirectory;
	}
};

// Signature of the path overrides applied by the most recently loaded map
// (empty when that map used none). A version reload — which closes every other
// open map — is only forced when the next map's override signature differs from
// this. Two maps that resolve to the same version with the same overrides (or
// none at all) therefore stay open side by side instead of closing each other.
wxString g_lastMapOverridesSignature;

// The path overrides applied by the most recent map that carried a .config.
// Re-applied when a later map of the same version has no overrides of its own,
// so opening such a map keeps the active client settings instead of resetting
// them to the persisted defaults (loadVersions() rebuilds the version objects on
// every open, wiping any in-memory overrides).
bool g_hasActiveAssetOverrides = false;
ClientVersionID g_activeAssetOverrideVersion = CLIENT_VERSION_NONE;
FileName g_activeAssetOverrideMapFile;
MapConfigSettings g_activeAssetOverrideConfig;

wxString TrimConfigValue(wxString value) {
	value.Trim(true).Trim(false);
	if (value.Length() >= 2) {
		wxUniChar first = value[0];
		wxUniChar last = value[value.Length() - 1];
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			value = value.Mid(1, value.Length() - 2);
			value.Trim(true).Trim(false);
		}
	}
	return value;
}

wxString DigitsOnly(const wxString& value) {
	wxString digits;
	for (wxUniChar ch : value) {
		if (wxIsdigit(ch)) {
			digits.Append(ch);
		}
	}
	return digits;
}

wxString NormalizeConfigIdentifier(wxString value) {
	value.Trim(true).Trim(false);
	value.MakeLower();
	value.Replace("-", "_");
	value.Replace(" ", "_");
	return value;
}

bool IsClientVersionConfigKey(const wxString& sectionName, const wxString& keyName) {
	const wxString normalizedSection = NormalizeConfigIdentifier(sectionName);
	const wxString normalizedKey = NormalizeConfigIdentifier(keyName);

	if (normalizedKey != "client_version" && normalizedKey != "default_client_version") {
		return false;
	}

	return normalizedSection.empty() || normalizedSection == "map" || normalizedSection == "mapeditor" || normalizedSection == "map_editor" || normalizedSection == "preferences";
}

bool IsDirectoryConfigSection(const wxString& sectionName) {
	const wxString normalizedSection = NormalizeConfigIdentifier(sectionName);
	return normalizedSection.empty() || normalizedSection == "map" || normalizedSection == "mapeditor" || normalizedSection == "map_editor" || normalizedSection == "preferences" || normalizedSection == "paths" || normalizedSection == "directories";
}

bool IsClientDirectoryConfigKey(const wxString& sectionName, const wxString& keyName) {
	if (!IsDirectoryConfigSection(sectionName)) {
		return false;
	}

	return keyName == "client_directory" || keyName == "client_dir" || keyName == "client_path" || keyName == "assets_directory" || keyName == "assets_dir" || keyName == "assets_path" || keyName == "graphics_directory" || keyName == "graphics_dir" || keyName == "graphics_path";
}

bool IsItemsDirectoryConfigKey(const wxString& sectionName, const wxString& keyName) {
	if (!IsDirectoryConfigSection(sectionName)) {
		return false;
	}

	return keyName == "items_directory" || keyName == "items_dir" || keyName == "items_path";
}

bool IsMonstersDirectoryConfigKey(const wxString& sectionName, const wxString& keyName) {
	if (!IsDirectoryConfigSection(sectionName)) {
		return false;
	}

	return keyName == "monsters_directory" || keyName == "monsters_dir" || keyName == "monsters_path" || keyName == "monster_directory" || keyName == "monster_dir" || keyName == "monster_path";
}

bool IsNpcsDirectoryConfigKey(const wxString& sectionName, const wxString& keyName) {
	if (!IsDirectoryConfigSection(sectionName)) {
		return false;
	}

	return keyName == "npcs_directory" || keyName == "npcs_dir" || keyName == "npcs_path" || keyName == "npc_directory" || keyName == "npc_dir" || keyName == "npc_path";
}

ClientVersionID ParseConfiguredClientVersion(const wxString& rawValue) {
	wxString value = TrimConfigValue(rawValue);
	if (value.empty()) {
		return CLIENT_VERSION_NONE;
	}

	long versionId = 0;
	if (value.ToLong(&versionId)) {
		ClientVersion* version = ClientVersion::get(static_cast<ClientVersionID>(versionId));
		if (version != nullptr) {
			return version->getID();
		}
	}

	const wxString normalizedValue = DigitsOnly(value);
	for (ClientVersion* version : ClientVersion::getAll()) {
		wxString versionName = wxstr(version->getName());
		if (versionName.CmpNoCase(value) == 0) {
			return version->getID();
		}

		if (!normalizedValue.empty() && DigitsOnly(versionName) == normalizedValue) {
			return version->getID();
		}
	}

	return CLIENT_VERSION_NONE;
}

MapConfigSettings LoadMapConfigSettings(const FileName& mapFileName) {
	MapConfigSettings settings;

	FileName configFile(mapFileName);
	configFile.SetExt("config");
	if (!configFile.FileExists()) {
		return settings;
	}

	wxTextFile file;
	if (!file.Open(configFile.GetFullPath())) {
		settings.warnings.Add("Could not open map config file: " + configFile.GetFullPath());
		return settings;
	}

	wxString sectionName;
	for (size_t lineNumber = 0; lineNumber < file.GetLineCount(); ++lineNumber) {
		wxString line = file.GetLine(lineNumber);
		wxString trimmedLine = line;
		trimmedLine.Trim(true).Trim(false);

		if (trimmedLine.empty() || trimmedLine.StartsWith("#") || trimmedLine.StartsWith(";")) {
			continue;
		}

		if (trimmedLine.StartsWith("[") && trimmedLine.EndsWith("]")) {
			sectionName = trimmedLine.Mid(1, trimmedLine.Length() - 2);
			sectionName = NormalizeConfigIdentifier(sectionName);
			continue;
		}

		const int separator = trimmedLine.Find('=');
		if (separator == wxNOT_FOUND) {
			continue;
		}

		wxString keyName = trimmedLine.Left(separator);
		keyName = NormalizeConfigIdentifier(keyName);

		wxString value = trimmedLine.Mid(separator + 1);
		value = TrimConfigValue(value);

		if (IsClientVersionConfigKey(sectionName, keyName)) {
			ClientVersionID clientVersion = ParseConfiguredClientVersion(value);
			if (clientVersion == CLIENT_VERSION_NONE) {
				wxString warning;
				warning << "Invalid client version in " << configFile.GetFullName() << " at line " << (lineNumber + 1) << ": " << value;
				settings.warnings.Add(warning);
			} else {
				settings.hasClientVersion = true;
				settings.clientVersion = clientVersion;
			}
		} else if (IsClientDirectoryConfigKey(sectionName, keyName)) {
			settings.hasClientDirectory = true;
			settings.clientDirectory = value;
		} else if (IsItemsDirectoryConfigKey(sectionName, keyName)) {
			settings.hasItemsDirectory = true;
			settings.itemsDirectory = value;
		} else if (IsMonstersDirectoryConfigKey(sectionName, keyName)) {
			settings.hasMonstersDirectory = true;
			settings.monstersDirectory = value;
		} else if (IsNpcsDirectoryConfigKey(sectionName, keyName)) {
			settings.hasNpcsDirectory = true;
			settings.npcsDirectory = value;
		}
	}

	file.Close();
	return settings;
}

wxString ResolveConfiguredDirectory(const FileName& mapFileName, const wxString& directoryValue) {
	wxString value = TrimConfigValue(directoryValue);
	if (value.empty()) {
		return wxEmptyString;
	}

	FileName resolvedPath;
	// AssignDir (not Assign) so the whole value is treated as a directory. With
	// Assign, a value without a trailing separator (e.g. ".../things/800") parses
	// the last component as a filename and GetPath() drops it, leaving the wrong
	// directory and breaking client/items/monster/npc asset lookup.
	resolvedPath.AssignDir(value);
	if (!resolvedPath.IsAbsolute()) {
		const wxString mapDirectory = mapFileName.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		resolvedPath.Normalize(RME_PATH_NORM_FLAGS, mapDirectory);
	} else {
		resolvedPath.Normalize(RME_PATH_NORM_FLAGS);
	}

	return resolvedPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

// Builds a signature describing the path overrides a map applies (empty when it
// has none). Override directories are resolved to absolute paths so two maps in
// different folders that point at the same assets compare equal. Mirrors the
// "no overrides" state as an empty string so a plain map never forces a reload.
wxString BuildMapOverridesSignature(const FileName& mapFileName, ClientVersionID resolvedClientVersion, const MapConfigSettings& mapConfig) {
	if (!mapConfig.hasPathOverrides()) {
		return wxString();
	}

	wxString signature;
	signature << static_cast<int>(resolvedClientVersion);
	signature << "|" << (mapConfig.hasClientDirectory ? ResolveConfiguredDirectory(mapFileName, mapConfig.clientDirectory) : wxString());
	signature << "|" << (mapConfig.hasItemsDirectory ? ResolveConfiguredDirectory(mapFileName, mapConfig.itemsDirectory) : wxString());
	signature << "|" << (mapConfig.hasMonstersDirectory ? ResolveConfiguredDirectory(mapFileName, mapConfig.monstersDirectory) : wxString());
	signature << "|" << (mapConfig.hasNpcsDirectory ? ResolveConfiguredDirectory(mapFileName, mapConfig.npcsDirectory) : wxString());
	return signature;
}

void AddInvalidDirectoryWarning(MapConfigSettings& settings, const FileName& mapFileName, const wxString& label, const wxString& rawValue) {
	FileName configFile(mapFileName);
	configFile.SetExt("config");

	wxString warning;
	warning << "Invalid " << label << " in " << configFile.GetFullName() << ": " << rawValue;
	settings.warnings.Add(warning);
}

void ApplyConfiguredDirectory(MapConfigSettings& settings, const FileName& mapFileName, const wxString& rawValue, const wxString& label, const std::function<void(const FileName&)>& setDirectory, const std::function<void()>& clearDirectory, bool allowEmpty) {
	wxString resolvedDirectory = ResolveConfiguredDirectory(mapFileName, rawValue);
	if (resolvedDirectory.empty()) {
		if (allowEmpty) {
			clearDirectory();
		} else {
			AddInvalidDirectoryWarning(settings, mapFileName, label, rawValue);
		}
		return;
	}

	FileName directory(resolvedDirectory);
	if (!directory.DirExists()) {
		AddInvalidDirectoryWarning(settings, mapFileName, label, rawValue);
		return;
	}

	setDirectory(directory);
}

ClientVersionID ApplyMapConfigSettings(const FileName& mapFileName, MapConfigSettings& mapConfig, ClientVersionID fallbackClientVersion) {
	const ClientVersionID targetClientVersion = mapConfig.hasClientVersion ? mapConfig.clientVersion : fallbackClientVersion;
	if (targetClientVersion == CLIENT_VERSION_NONE) {
		if (mapConfig.hasPathOverrides()) {
			mapConfig.warnings.Add("Map config path overrides were ignored because no client version could be resolved for this map.");
		}
		return CLIENT_VERSION_NONE;
	}

	ClientVersion* version = ClientVersion::get(targetClientVersion);
	if (version == nullptr) {
		if (mapConfig.hasPathOverrides()) {
			mapConfig.warnings.Add("Map config path overrides were ignored because the target client version is unavailable.");
		}
		return CLIENT_VERSION_NONE;
	}

	if (mapConfig.hasClientDirectory) {
		ApplyConfiguredDirectory(
			mapConfig,
			mapFileName,
			mapConfig.clientDirectory,
			"client directory",
			[version](const FileName& directory) { version->setClientPath(directory); },
			[]() { },
			false
		);
	}

	if (mapConfig.hasItemsDirectory) {
		ApplyConfiguredDirectory(
			mapConfig,
			mapFileName,
			mapConfig.itemsDirectory,
			"items directory",
			[version](const FileName& directory) { version->setItemsPath(directory); },
			[version]() { version->clearItemsPath(); },
			true
		);
	}

	if (mapConfig.hasMonstersDirectory) {
		ApplyConfiguredDirectory(
			mapConfig,
			mapFileName,
			mapConfig.monstersDirectory,
			"monsters directory",
			[version](const FileName& directory) { version->setMonstersPath(directory); },
			[version]() { version->clearMonstersPath(); },
			true
		);
	}

	if (mapConfig.hasNpcsDirectory) {
		ApplyConfiguredDirectory(
			mapConfig,
			mapFileName,
			mapConfig.npcsDirectory,
			"NPC directory",
			[version](const FileName& directory) { version->setNpcsPath(directory); },
			[version]() { version->clearNpcsPath(); },
			true
		);
	}

	return targetClientVersion;
}
} // namespace

// GUI class implementation
GUI::GUI() :
	aui_manager(nullptr),
	root(nullptr),
	minimap(nullptr),
	gem(nullptr),
	search_result_window(nullptr),
	comments_window(nullptr),
	find_brush_window(nullptr),
	secondary_map(nullptr),
	doodad_buffer_map(nullptr),

	house_brush(nullptr),
	house_exit_brush(nullptr),
	optional_brush(nullptr),
	eraser(nullptr),
	raise_brush(nullptr),
	lower_brush(nullptr),
	flatten_brush(nullptr),
	last_ground_brush(nullptr),
	normal_door_brush(nullptr),
	locked_door_brush(nullptr),
	magic_door_brush(nullptr),
	quest_door_brush(nullptr),
	hatch_door_brush(nullptr),
	window_door_brush(nullptr),
	refresh_brush(nullptr),

	OGLContext(nullptr),
	loaded_version(CLIENT_VERSION_NONE),
	mode(SELECTION_MODE),
	pasting(false),
	restoringMapViewPosition(false),
	hotkeys_enabled(true),

	current_brush(nullptr),
	previous_brush(nullptr),
	brush_shape(BRUSHSHAPE_SQUARE),
	brush_size(0),
	brush_size_even(false),
	brush_variation(0),

	creature_spawntime(0),
	draw_locked_doors(false),
	use_custom_thickness(false),
	custom_thickness_mod(0.0),
	progressBar(nullptr),
	disabled_counter(0),
	tabbook(nullptr) {
	doodad_buffer_map = newd BaseMap();
}

GUI::~GUI() {
	delete doodad_buffer_map;
	delete aui_manager;
	delete OGLContext;
}

wxGLContext* GUI::GetGLContext(wxGLCanvas* win) {
	if (OGLContext == nullptr) {
#ifdef __WXOSX__
		/*
		wxGLContext(AGLPixelFormat fmt, wxGLCanvas *win,
					const wxPalette& WXUNUSED(palette),
					const wxGLContext *other
					);
		*/
		OGLContext = new wxGLContext(win, nullptr);
#else
		OGLContext = newd wxGLContext(win);
#endif
	}

	return OGLContext;
}

wxString GUI::GetDataDirectory() {
	std::string cfg_str = g_settings.getString(Config::DATA_DIRECTORY);
	if (!cfg_str.empty()) {
		FileName dir;
		dir.Assign(wxstr(cfg_str));
		wxString path;
		if (dir.DirExists()) {
			path = dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			return path;
		}
	}

	// Silently reset directory
	FileName exec_directory;
	try {
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast) {
		throw; // Crash application (this should never happend anyways...)
	}

	exec_directory.AppendDir("data");
	return exec_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxString GUI::GetExecDirectory() {
	// Silently reset directory
	FileName exec_directory;
	try {
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast) {
		wxLogError("Could not fetch executable directory.");
	}
	return exec_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxString GUI::GetLocalDataDirectory() {
	if (g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetLocalDirectory() {
	if (g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetExtensionsDirectory() {
	std::string cfg_str = g_settings.getString(Config::EXTENSIONS_DIRECTORY);
	if (!cfg_str.empty()) {
		FileName dir;
		dir.Assign(wxstr(cfg_str));
		wxString path;
		if (dir.DirExists()) {
			path = dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			return path;
		}
	}

	// Silently reset directory
	FileName local_directory = GetLocalDirectory();
	local_directory.AppendDir("extensions");
	local_directory.Mkdir(0755, wxPATH_MKDIR_FULL);
	return local_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

void GUI::discoverDataDirectory(const wxString& existentFile) {
	wxString currentDir = wxGetCwd();
	wxString execDir = GetExecDirectory();

	wxString possiblePaths[] = {
		execDir,
		currentDir + "/",

		// these are used usually when running from build directories
		execDir + "/../",
		execDir + "/../../",
		execDir + "/../../../",
		currentDir + "/../",
	};

	bool found = false;
	for (const wxString& path : possiblePaths) {
		if (wxFileName(path + "data/" + existentFile).FileExists()) {
			m_dataDirectory = path + "data/";
			found = true;
			break;
		}
	}

	if (!found) {
		wxLogError(wxString() + "Could not find data directory.\n");
	}
}

bool GUI::LoadVersion(ClientVersionID version, wxString& error, wxArrayString& warnings, bool force, bool promptForPaths) {
	if (ClientVersion::get(version) == nullptr) {
		error = "Unsupported client version! (8)";
		return false;
	}

	if (version != loaded_version || force) {
		if (getLoadedVersion() != nullptr) {
			// There is another version loaded right now, save window layout
			g_gui.SavePerspective();
		}

		// Disable all rendering so the data is not accessed while reloading
		UnnamedRenderingLock();
		DestroyPalettes();
		DestroyMinimap();

		// Destroy the previous version
		UnloadVersion();

		loaded_version = version;
		if (!getLoadedVersion()->hasValidPaths()) {
			if (!promptForPaths) {
				error = "Couldn't load relevant asset files";
				loaded_version = CLIENT_VERSION_NONE;
				return false;
			}
			if (!getLoadedVersion()->loadValidPaths()) {
				error = "Couldn't load relevant asset files";
				loaded_version = CLIENT_VERSION_NONE;
				return false;
			}
		}

		bool ret = LoadDataFiles(error, warnings);
		if (ret && !headless) {
			g_gui.LoadPerspective();
		} else if (!ret) {
			loaded_version = CLIENT_VERSION_NONE;
		}

		return ret;
	}
	return true;
}

void GUI::EnableHotkeys() {
	hotkeys_enabled = true;
}

void GUI::DisableHotkeys() {
	hotkeys_enabled = false;
}

bool GUI::AreHotkeysEnabled() const {
	return hotkeys_enabled;
}

ClientVersionID GUI::GetCurrentVersionID() const {
	if (loaded_version != CLIENT_VERSION_NONE) {
		return getLoadedVersion()->getID();
	}
	return CLIENT_VERSION_NONE;
}

const ClientVersion& GUI::GetCurrentVersion() const {
	assert(loaded_version);
	return *getLoadedVersion();
}

void GUI::CycleTab(bool forward) {
	tabbook->CycleTab(forward);
}

bool GUI::LoadDataFiles(wxString& error, wxArrayString& warnings) {
	FileName data_path = getLoadedVersion()->getDataPath();
	FileName client_path = getLoadedVersion()->getClientPath();
	FileName items_path = getLoadedVersion()->getItemsPath();
	FileName extension_path = GetExtensionsDirectory();

	FileName exec_directory;
	try {
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (std::bad_cast&) {
		error = "Couldn't establish working directory...";
		return false;
	}

	g_gui.gfx.client_version = getLoadedVersion();

	if (!g_gui.gfx.loadOTFI(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), error, warnings)) {
		error = "Couldn't load otfi file: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.CreateLoadBar("Loading asset files");
	g_gui.SetLoadDone(0, "Loading metadata file...");

	wxFileName metadata_path = g_gui.gfx.getMetadataFileName();
	if (!g_gui.gfx.loadSpriteMetadata(metadata_path, error, warnings)) {
		error = "Couldn't load metadata: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(10, "Loading sprites file...");

	wxFileName sprites_path = g_gui.gfx.getSpritesFileName();
	if (!g_gui.gfx.loadSpriteData(sprites_path.GetFullPath(), error, warnings)) {
		error = "Couldn't load sprites: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(20, "Loading items.otb file...");
	if (!g_items.loadFromOtb(wxString(items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "items.otb"), error, warnings)) {
		error = "Couldn't load items.otb: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(30, "Loading items.xml ...");
	if (!g_items.loadFromGameXml(wxString(items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "items.xml"), error, warnings)) {
		warnings.push_back("Couldn't load items.xml: " + error);
	}

	g_creatures.clear();

	g_gui.SetLoadDone(45, "Loading monsters ...");
	if (!g_creatures.loadFromDirectory(getLoadedVersion()->getMonstersPath(), false, error, warnings)) {
		warnings.push_back("Couldn't load monsters: " + error);
	}

	g_gui.SetLoadDone(47, "Loading NPCs ...");
	if (!g_creatures.loadFromDirectory(getLoadedVersion()->getNpcsPath(), true, error, warnings)) {
		warnings.push_back("Couldn't load NPCs: " + error);
	}

	g_gui.SetLoadDone(50, "Loading materials.xml ...");
	if (!g_materials.loadMaterials(wxString(data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "materials.xml"), error, warnings)) {
		warnings.push_back("Couldn't load materials.xml: " + error);
	}

	g_gui.SetLoadDone(70, "Loading extensions...");
	if (!g_materials.loadExtensions(extension_path, error, warnings)) {
		// warnings.push_back("Couldn't load extensions: " + error);
	}

	g_gui.SetLoadDone(70, "Finishing...");
	g_brushes.init();
	g_materials.createOtherTileset();

	g_gui.DestroyLoadBar();
	return true;
}

void GUI::UnloadVersion() {
	UnnamedRenderingLock();
	gfx.clear();
	current_brush = nullptr;
	previous_brush = nullptr;

	house_brush = nullptr;
	house_exit_brush = nullptr;
	optional_brush = nullptr;
	eraser = nullptr;
	raise_brush = nullptr;
	lower_brush = nullptr;
	flatten_brush = nullptr;
	last_ground_brush = nullptr;
	normal_door_brush = nullptr;
	locked_door_brush = nullptr;
	magic_door_brush = nullptr;
	quest_door_brush = nullptr;
	hatch_door_brush = nullptr;
	window_door_brush = nullptr;
	refresh_brush = nullptr;

	if (loaded_version != CLIENT_VERSION_NONE) {
		// g_gui.UnloadVersion();
		// Drop any Brush*/RAWBrush* the Find Brush window is holding from a
		// previous search before they're freed below - otherwise the next
		// AUI layout pass resizes the panel, triggers BrushIconBox::OnSize,
		// and dereferences dangling pointers.
		if (find_brush_window) {
			find_brush_window->InvalidateResults();
		}
		g_materials.clear();
		g_terraform_pairs.clear();
		g_brushes.clear();
		g_items.clear();
		gfx.clear();

		g_creatures.clear();

		loaded_version = CLIENT_VERSION_NONE;
	}
}

void GUI::SaveMapTabViewPosition(MapTab* mapTab) {
	if (!mapTab) {
		return;
	}

	Editor* editor = mapTab->GetEditor();
	if (!editor) {
		return;
	}

	const std::string key = getMapViewKey(*editor);
	if (key.empty()) {
		return;
	}

	saveMapViewPosition(key, mapTab->GetScreenCenterPosition());
}

void GUI::SaveAllMapViewPositions() {
	if (!tabbook) {
		return;
	}

	for (int index = 0; index < tabbook->GetTabCount(); ++index) {
		if (auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			SaveMapTabViewPosition(mapTab);
		}
	}
}

void GUI::SaveCurrentMap(FileName filename, bool showdialog) {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		Editor* editor = mapTab->GetEditor();
		if (editor && editor->IsLiveClient()) {
			return;
		}
		if (editor) {
			editor->saveMap(filename, showdialog);
			SaveMapTabViewPosition(mapTab);
		}
	}

	UpdateTitle();
	root->UpdateMenubar();
	root->Refresh();
}

bool GUI::IsEditorOpen() const {
	return tabbook != nullptr && GetCurrentMapTab();
}

double GUI::GetCurrentZoom() {
	MapTab* tab = GetCurrentMapTab();
	if (tab) {
		return tab->GetCanvas()->GetZoom();
	}
	return 1.0;
}

void GUI::SetCurrentZoom(double zoom) {
	MapTab* tab = GetCurrentMapTab();
	if (tab) {
		tab->GetCanvas()->SetZoom(zoom);
	}
}

void GUI::FitViewToMap() {
	for (int index = 0; index < tabbook->GetTabCount(); ++index) {
		if (auto* tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			tab->GetView()->FitToMap();
		}
	}
}

void GUI::FitViewToMap(MapTab* mt, bool center) {
	for (int index = 0; index < tabbook->GetTabCount(); ++index) {
		if (auto* tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			if (tab->HasSameReference(mt)) {
				tab->GetView()->FitToMap(center);
			}
		}
	}
}

void GUI::RestoreMapTabViewPosition(MapTab* mapTab, const Position& position) {
	if (!mapTab || !position.isValid()) {
		return;
	}

	const auto apply = [this, mapTab, position]() {
		restoringMapViewPosition = true;
		mapTab->SetScreenCenterPosition(position);
		restoringMapViewPosition = false;
	};

	apply();
	if (root) {
		wxTheApp->CallAfter([this, mapTab, position]() {
			restoringMapViewPosition = true;
			mapTab->SetScreenCenterPosition(position);
			restoringMapViewPosition = false;
		});
	}
}

void GUI::NotifyMapViewPositionChanged(MapTab* mapTab) {
	if (restoringMapViewPosition || !mapTab) {
		return;
	}
	// This notification fires on every scroll step, including middle-mouse
	// panning. Persisting the position writes the entire settings file to disk
	// (Settings::save), which causes visible stutter while panning. Throttle the
	// disk write to at most once per second; the final resting position is still
	// flushed by the explicit SaveMapTabViewPosition calls on tab switch, tab
	// close and application exit.
	if (mapViewPositionSaveWatch.Time() < 1000) {
		return;
	}
	mapViewPositionSaveWatch.Start();
	SaveMapTabViewPosition(mapTab);
}

bool GUI::ConnectToLiveServer() {
	FinishWelcomeDialog();

	if (root && !root->IsShown()) {
		root->Show();
	}

	LiveConnectWindow dialog(root);
	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	// Resolve save prompts before opening the live session so the handshake is
	// not blocked later by a modal dialog behind the live log tab.
	if (!CloseAllEditors()) {
		PopupDialog("Cannot Connect", "Save or close your open maps before joining a live session.", wxOK);
		return false;
	}

	// Store the dialog's choices before constructing the client -- LiveClient reads
	// the live settings (cursor colour) in its constructor.
	g_settings.setString(Config::LIVE_HOST, nstr(dialog.GetHost()));
	g_settings.setInteger(Config::LIVE_PORT, dialog.GetPort());
	g_settings.setString(Config::LIVE_USERNAME, nstr(dialog.GetUsername()));
	g_settings.setString(Config::LIVE_PASSWORD, nstr(dialog.GetPassword()));
	const wxColor cursorColor = dialog.GetCursorColor();
	g_settings.setInteger(Config::LIVE_CURSOR_RED, cursorColor.Red());
	g_settings.setInteger(Config::LIVE_CURSOR_GREEN, cursorColor.Green());
	g_settings.setInteger(Config::LIVE_CURSOR_BLUE, cursorColor.Blue());
	g_settings.setInteger(Config::LIVE_CURSOR_ALPHA, cursorColor.Alpha());

	auto* client = newd LiveClient();
	client->setName(dialog.GetUsername());
	client->setPassword(dialog.GetPassword());
	client->setCursorColor(dialog.GetCursorColor());

	client->createLogWindow(tabbook);

	if (!client->connect(nstr(dialog.GetHost()), dialog.GetPort())) {
		wxString error = client->getLastError();
		CloseLiveEditors(client);
		PopupDialog("Connection Error", error, wxOK);
		return false;
	}

	UpdateMenubar();
	return true;
}

bool GUI::ReconnectToLastLiveServer() {
	FinishWelcomeDialog();

	if (root && !root->IsShown()) {
		root->Show();
	}

	const wxString host = wxstr(g_settings.getString(Config::LIVE_HOST));
	const int port = g_settings.getInteger(Config::LIVE_PORT);
	if (host.empty() || port <= 0 || port > 65535) {
		return false;
	}

	if (!CloseAllEditors()) {
		return false;
	}

	auto* client = newd LiveClient();
	client->setName(wxstr(g_settings.getString(Config::LIVE_USERNAME)));
	client->setPassword(wxstr(g_settings.getString(Config::LIVE_PASSWORD)));
	client->setCursorColor(wxColor(
		g_settings.getInteger(Config::LIVE_CURSOR_RED),
		g_settings.getInteger(Config::LIVE_CURSOR_GREEN),
		g_settings.getInteger(Config::LIVE_CURSOR_BLUE),
		g_settings.getInteger(Config::LIVE_CURSOR_ALPHA)
	));

	client->createLogWindow(tabbook);

	if (!client->connect(nstr(host), static_cast<uint16_t>(port))) {
		wxString error = client->getLastError();
		CloseLiveEditors(client);
		PopupDialog("Connection Error", error, wxOK);
		return false;
	}

	UpdateMenubar();
	return true;
}

bool GUI::NewMap() {
	FinishWelcomeDialog();

	Editor* editor;
	try {
		editor = newd Editor(copybuffer);
	} catch (std::runtime_error& e) {
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}

	auto* mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);
	editor->map.clearChanges();

	SetStatusText("Created new map");
	UpdateTitle();
	RefreshPalettes();
	root->UpdateMenubar();
	root->Refresh();

	return true;
}

void GUI::OpenMap() {
	wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_LOAD_FILE_WILDCARD_OTGZ : MAP_LOAD_FILE_WILDCARD;
	wxFileDialog dialog(root, "Open map file", wxEmptyString, wxEmptyString, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() == wxID_OK) {
		LoadMap(dialog.GetPath());
	}
}

void GUI::SaveMap() {
	if (!IsEditorOpen()) {
		return;
	}
	if (GetCurrentEditor() && GetCurrentEditor()->IsLiveClient()) {
		return;
	}

	if (GetCurrentMap().hasFile()) {
		SaveCurrentMap(true);
	} else {
		wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
		wxFileDialog dialog(root, "Save...", wxEmptyString, wxEmptyString, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

		if (dialog.ShowModal() == wxID_OK) {
			SaveCurrentMap(dialog.GetPath(), true);
		}
	}
}

void GUI::SaveMapAs() {
	if (!IsEditorOpen()) {
		return;
	}
	if (GetCurrentEditor() && GetCurrentEditor()->IsLiveClient()) {
		return;
	}

	wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
	wxFileDialog dialog(root, "Save As...", "", "", wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dialog.ShowModal() == wxID_OK) {
		SaveCurrentMap(dialog.GetPath(), true);
		UpdateTitle();
		root->menu_bar->AddRecentFile(dialog.GetPath());
		root->UpdateMenubar();
	}
}

bool GUI::LoadMap(const FileName& fileName) {
	FinishWelcomeDialog();

	// Replace the current tab only if it is a throwaway scratch map (no file, no
	// changes). A live map matches that test but must never be closed here — doing
	// so would make the live view vanish while the client stays connected.
	Editor* currentEditor = GetCurrentEditor();
	if (currentEditor && !currentEditor->IsLive() && !GetCurrentMap().hasChanged() && !GetCurrentMap().hasFile()) {
		g_gui.CloseCurrentEditor();
	}

	MapConfigSettings mapConfig = LoadMapConfigSettings(fileName);
	if (mapConfig.hasClientVersion) {
		g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, mapConfig.clientVersion);
	}

	MapVersion mapVersion;
	ClientVersionID resolvedClientVersion = CLIENT_VERSION_NONE;
	if (IOMapOTBM::getVersionInfo(fileName, mapVersion)) {
		resolvedClientVersion = mapVersion.client;
	}

	ClientVersion::loadVersions();
	resolvedClientVersion = ApplyMapConfigSettings(fileName, mapConfig, resolvedClientVersion);

	// loadVersions() above rebuilds the version objects from the persisted
	// defaults, so a map without its own overrides would otherwise revert the
	// client paths. Remember the overrides of the map that supplied them and
	// re-apply them for a later override-less map of the same version, so opening
	// such a map keeps the active client settings instead of resetting them. The
	// override signature is taken from whichever config is effectively in force so
	// the inherited map matches the active one and does not trigger a reload.
	wxString overridesSignature;
	if (mapConfig.hasPathOverrides()) {
		g_hasActiveAssetOverrides = true;
		g_activeAssetOverrideVersion = resolvedClientVersion;
		g_activeAssetOverrideMapFile = fileName;
		g_activeAssetOverrideConfig = mapConfig;
		overridesSignature = BuildMapOverridesSignature(fileName, resolvedClientVersion, mapConfig);
	} else if (g_hasActiveAssetOverrides && resolvedClientVersion != CLIENT_VERSION_NONE && g_activeAssetOverrideVersion == resolvedClientVersion) {
		MapConfigSettings inheritedConfig = g_activeAssetOverrideConfig;
		ApplyMapConfigSettings(g_activeAssetOverrideMapFile, inheritedConfig, resolvedClientVersion);
		overridesSignature = BuildMapOverridesSignature(g_activeAssetOverrideMapFile, resolvedClientVersion, g_activeAssetOverrideConfig);
	}

	// Only force a full asset reload (which closes every other open map) when this
	// map's effective path overrides differ from the previous load's. A version
	// mismatch is handled separately by the editor constructor, so maps that share
	// the version and overrides (or use none) can stay open together.
	const bool forceReloadVersion = overridesSignature != g_lastMapOverridesSignature;
	g_lastMapOverridesSignature = overridesSignature;

	Editor* editor;
	try {
		editor = newd Editor(copybuffer, fileName, mapConfig.hasClientVersion ? mapConfig.clientVersion : CLIENT_VERSION_NONE, forceReloadVersion);
	} catch (std::runtime_error& e) {
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}

	const std::string viewKey = makeLocalMapViewKey(editor->map.getFilename());
	Position savedPosition;
	const bool hasSavedPosition = loadMapViewPosition(viewKey, savedPosition);

	auto* mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);

	root->AddRecentFile(fileName);

	mapTab->GetView()->FitToMap(!hasSavedPosition);
	UpdateTitle();
	ListDialog("Map config warnings", mapConfig.warnings);
	ListDialog("Map loader errors", mapTab->GetMap()->getWarnings());
	RefreshPalettes();

	FitViewToMap(mapTab, !hasSavedPosition);
	root->UpdateMenubar();

	if (hasSavedPosition) {
		RestoreMapTabViewPosition(mapTab, savedPosition);
	}
	return true;
}

Editor* GUI::GetCurrentEditor() {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		return mapTab->GetEditor();
	}
	return nullptr;
}

EditorTab* GUI::GetTab(int idx) {
	return tabbook->GetTab(idx);
}

int GUI::GetTabCount() const {
	return tabbook->GetTabCount();
}

EditorTab* GUI::GetCurrentTab() {
	return tabbook->GetCurrentTab();
}

MapTab* GUI::GetCurrentMapTab() const {
	if (tabbook && tabbook->GetTabCount() > 0) {
		EditorTab* editorTab = tabbook->GetCurrentTab();
		auto* mapTab = dynamic_cast<MapTab*>(editorTab);
		return mapTab;
	}
	return nullptr;
}

Map& GUI::GetCurrentMap() {
	Editor* editor = GetCurrentEditor();
	ASSERT(editor);
	return editor->map;
}

int GUI::GetOpenMapCount() {
	std::set<Map*> open_maps;

	for (int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto* tab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if (tab) {
			open_maps.insert(open_maps.begin(), tab->GetMap());
		}
	}

	return static_cast<int>(open_maps.size());
}

bool GUI::ShouldSave() {
	const Map& map = GetCurrentMap();
	if (map.hasChanged()) {
		if (map.getTileCount() == 0) {
			Editor* editor = GetCurrentEditor();
			ASSERT(editor);
			return editor->actionQueue->canUndo();
		}
		return true;
	}
	return false;
}

void GUI::AddPendingCanvasEvent(wxEvent& event) {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		mapTab->GetCanvas()->GetEventHandler()->AddPendingEvent(event);
	}
}

void GUI::CloseCurrentEditor() {
	if (!tabbook || tabbook->GetTabCount() == 0) {
		return;
	}

	const int selection = tabbook->GetSelection();
	if (selection == wxNOT_FOUND) {
		return;
	}

	UnnamedRenderingLock();
	WindowFreezeGuard freeze_guard(root);

	if (auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(selection))) {
		SaveMapTabViewPosition(mapTab);
	}

	tabbook->DeleteTab(selection);
	RefreshPalettes();
	if (root) {
		root->UpdateMenubar();
	}
}


bool GUI::CloseAllEditors() {
	if (!tabbook) {
		return true;
	}

	UnnamedRenderingLock();
	WindowFreezeGuard freeze_guard;

	bool palettesNeedRefresh = false;

	for (int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if (!mapTab) {
			continue;
		}

		if (mapTab->IsUniqueReference() && mapTab->GetMap() && mapTab->GetMap()->hasChanged()) {
			tabbook->SetFocusedTab(i);
			if (root && !root->DoQuerySave(false)) {
				if (palettesNeedRefresh) {
					RefreshPalettes();
					if (root) {
						root->UpdateMenubar();
					}
				}
				return false;
			}
		}

		if (!freeze_guard.isActive()) {
			freeze_guard.reset(root);
		}
		SaveMapTabViewPosition(mapTab);
		tabbook->DeleteTab(i--);
		palettesNeedRefresh = true;
	}

	if (palettesNeedRefresh) {
		RefreshPalettes();
	}
	if (root) {
		root->UpdateMenubar();
	}
	return true;
}

void GUI::NewMapView() {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		auto* newMapTab = newd MapTab(mapTab);
		newMapTab->OnSwitchEditorMode(mode);

		SetStatusText("Created new view");
		UpdateTitle();
		RefreshPalettes();
		root->UpdateMenubar();
		root->Refresh();
	}
}

void GUI::CloseLiveEditors(LiveSocket* sender) {
	if (!tabbook) {
		return;
	}

	// Deleting a live MapTab destroys its Editor/Map. MapCanvas::OnPaint renders
	// through the editor while rendering is enabled, so a paint dispatched mid-
	// destruction would dereference the freed editor and crash (intermittently).
	// Gate it behind a rendering lock exactly like CloseCurrentEditor/CloseAllEditors.
	UnnamedRenderingLock();
	WindowFreezeGuard freeze_guard(root);

	// First, detach any live log tab so the socket's back-reference is cleared
	// before the editor (and the socket it owns) gets destroyed.
	// LiveClient::close() may already have detached the log tab (socket == nullptr),
	// so remove detached log tabs here as well — otherwise the tab lingers after
	// a menu disconnect and the user has to close it by hand.
	for (int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto* logTab = dynamic_cast<LiveLogTab*>(tabbook->GetTab(i));
		if (logTab && (logTab->GetSocket() == sender || logTab->GetSocket() == nullptr)) {
			logTab->Disconnect();
			tabbook->DeleteTab(i--);
		}
	}

	// Then close the live map editor tab(s) bound to this socket.
	for (int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if (mapTab) {
			Editor* editor = mapTab->GetEditor();
			if (editor && editor->IsLive() && &editor->GetLive() == sender) {
				SaveMapTabViewPosition(mapTab);
				tabbook->DeleteTab(i);
				i = -1;
			}
		}
	}

	// The live client is owned here; the live server is owned by its editor.
	if (auto* client = dynamic_cast<LiveClient*>(sender)) {
		delete client;
	}

	if (root) {
		root->UpdateMenubar();
	}
}

void GUI::LoadPerspective() {
	if (headless || !root || !aui_manager) {
		return;
	}

	if (!IsVersionLoaded()) {
		if (g_settings.getInteger(Config::WINDOW_MAXIMIZED)) {
			root->Maximize();
		} else {
			root->SetSize(wxSize(
				g_settings.getInteger(Config::WINDOW_WIDTH),
				g_settings.getInteger(Config::WINDOW_HEIGHT)
			));
		}
	} else {
		// Only create palettes if they don't already exist
		if (palettes.empty()) {
			std::string tmp;
			std::string layout = g_settings.getString(Config::PALETTE_LAYOUT);

			std::vector<std::string> palette_list;
			for (char c : layout) {
				if (c == '|') {
					if (!tmp.empty()) {
						palette_list.push_back(tmp);
						tmp.clear();
					}
				} else {
					tmp.push_back(c);
				}
			}

			if (!tmp.empty()) {
				palette_list.push_back(tmp);
			}

			// If no palettes were loaded from settings, create one default palette
			if (palette_list.empty()) {
				CreatePalette();
			} else {
				// Limit to one palette to prevent duplicates
				// Only use the first valid layout entry
				for (const std::string& name : palette_list) {
					// Skip empty or invalid layout strings
					if (name.empty() || name.length() > 10000) {
						continue;
					}

					PaletteWindow* palette = CreatePalette();

					wxAuiPaneInfo& info = aui_manager->GetPane(palette);
					try {
						aui_manager->LoadPaneInfo(wxstr(name), info);
					} catch (...) {
						// If loading the layout fails, just use default layout
						// The palette is already created, so continue
					}

					if (info.IsFloatable()) {
						bool offscreen = true;
						for (uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
							wxDisplay display(index);
							wxRect rect = display.GetClientArea();
							if (rect.Contains(info.floating_pos)) {
								offscreen = false;
								break;
							}
						}

						if (offscreen) {
							info.Dock();
						}
					}

					// Only create one palette from saved layout
					break;
				}
			}
		}

		if (g_settings.getInteger(Config::MINIMAP_VISIBLE)) {
			if (!minimap) {
				wxAuiPaneInfo info;

				const wxString& data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);
				ConfigureMinimapAuiPane(info);

				minimap = newd MinimapWindow(root);
				aui_manager->AddPane(minimap, info);
			} else {
				wxAuiPaneInfo& info = aui_manager->GetPane(minimap);

				const wxString& data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);
				ConfigureMinimapAuiPane(info);
			}

			wxAuiPaneInfo& info = aui_manager->GetPane(minimap);
			if (info.IsFloatable()) {
				bool offscreen = true;
				for (uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					wxRect rect = display.GetClientArea();
					if (rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if (offscreen) {
					info.Dock();
				}
			}
		}

		aui_manager->Update();
		root->UpdateMenubar();
		if (minimap) {
			minimap->UpdateCaptionChrome();
		}
	}

	root->GetAuiToolBar()->LoadPerspective();
}

void GUI::SavePerspective() {
	if (headless || !root || !aui_manager) {
		return;
	}

	g_settings.setInteger(Config::WINDOW_MAXIMIZED, root->IsMaximized());
	g_settings.setInteger(Config::WINDOW_WIDTH, root->GetSize().GetWidth());
	g_settings.setInteger(Config::WINDOW_HEIGHT, root->GetSize().GetHeight());

	g_settings.setInteger(Config::MINIMAP_VISIBLE, minimap ? 1 : 0);

	wxString pinfo;
	for (auto& palette : palettes) {
		if (aui_manager->GetPane(palette).IsShown()) {
			pinfo << aui_manager->SavePaneInfo(aui_manager->GetPane(palette)) << "|";
		}
	}
	g_settings.setString(Config::PALETTE_LAYOUT, nstr(pinfo));

	if (minimap) {
		wxString s = aui_manager->SavePaneInfo(aui_manager->GetPane(minimap));
		g_settings.setString(Config::MINIMAP_LAYOUT, nstr(s));
	}

	SaveAllMapViewPositions();

	root->GetAuiToolBar()->SavePerspective();
}

void GUI::HideSearchWindow() {
	if (search_result_window) {
		aui_manager->GetPane(search_result_window).Show(false);
		aui_manager->Update();
	}
}

SearchResultWindow* GUI::ShowSearchWindow() {
	if (search_result_window == nullptr) {
		search_result_window = newd SearchResultWindow(root);
		aui_manager->AddPane(search_result_window, wxAuiPaneInfo().Caption("Search Results"));
	} else {
		aui_manager->GetPane(search_result_window).Show();
	}
	aui_manager->Update();
	return search_result_window;
}

void GUI::HideCommentsWindow() {
	if (comments_window) {
		aui_manager->GetPane(comments_window).Show(false);
		aui_manager->Update();
	}
}

CommentsWindow* GUI::ShowCommentsWindow() {
	if (comments_window == nullptr) {
		comments_window = newd CommentsWindow(root);
		aui_manager->AddPane(comments_window, wxAuiPaneInfo().Caption("Map Comments"));
	} else {
		aui_manager->GetPane(comments_window).Show();
		comments_window->ReloadList();
	}
	aui_manager->Update();
	return comments_window;
}

bool GUI::IsCommentsWindowShown() const {
	return comments_window && aui_manager->GetPane(comments_window).IsShown();
}

void GUI::RefreshCommentsWindow() {
	if (comments_window) {
		comments_window->ReloadList();
	}
}

FindBrushWindow* GUI::ShowFindBrushWindow() {
	if (find_brush_window == nullptr) {
		find_brush_window = newd FindBrushWindow(root);
		aui_manager->AddPane(find_brush_window, wxAuiPaneInfo().Caption("Find Brush").TopDockable(false).BottomDockable(false));
		aui_manager->Update();
		find_brush_window->FocusSearch();
	} else if (!aui_manager->GetPane(find_brush_window).IsShown()) {
		aui_manager->GetPane(find_brush_window).Show();
		aui_manager->Update();
		find_brush_window->ResetSearch();
	} else {
		find_brush_window->FocusSearch();
	}
	return find_brush_window;
}

bool GUI::IsFindBrushWindowShown() const {
	return find_brush_window && aui_manager->GetPane(find_brush_window).IsShown();
}

//=============================================================================
// Palette Window Interface implementation

PaletteWindow* GUI::GetPalette() {
	if (palettes.empty()) {
		return nullptr;
	}
	return palettes.front();
}

const GUI::PaletteList& GUI::GetPalettes() {
	return palettes;
}

PaletteWindow* GUI::NewPalette() {
	return CreatePalette();
}

void GUI::RefreshPalettes(Map* m, bool usedefault) {
	for (auto& palette : palettes) {
		palette->OnUpdate(m ? m : (usedefault ? (IsEditorOpen() ? &GetCurrentMap() : nullptr) : nullptr));
	}
	SelectBrush();
}

void GUI::RefreshOtherPalettes(PaletteWindow* p) {
	for (auto& palette : palettes) {
		if (palette != p) {
			palette->OnUpdate(IsEditorOpen() ? &GetCurrentMap() : nullptr);
		}
	}
	SelectBrush();
}

PaletteWindow* GUI::CreatePalette() {
	if (!IsVersionLoaded()) {
		return nullptr;
	}

	auto* palette = newd PaletteWindow(root, g_materials.tilesets);
	aui_manager->AddPane(palette, wxAuiPaneInfo().Caption("Palette").TopDockable(false).BottomDockable(false));
	aui_manager->Update();

	// Make us the active palette
	palettes.push_front(palette);
	// Select brush from this palette
	SelectBrushInternal(palette->GetSelectedBrush());
	// fix for blank house list on f5 or new palette
	palette->OnUpdate(IsEditorOpen() ? &GetCurrentMap() : nullptr);
	return palette;
}

void GUI::ActivatePalette(PaletteWindow* p) {
	const auto it = std::find(palettes.begin(), palettes.end(), p);
	if (it != palettes.end()) {
		palettes.erase(it);
	}
	palettes.push_front(p);
}

void GUI::DestroyPalettes() {
	// Make a copy of the list to avoid issues during destruction
	// Clear thed original list immediately so nothing can access palettes during destruction
	PaletteList tmp = palettes;
	palettes.clear();

	if (!aui_manager) {
		for (auto palette : tmp) {
			if (palette) {
				palette->Destroy();
			}
		}
		return;
	}
	
	// Detach all panes first
	for (auto palette : tmp) {
		if (palette) {
			aui_manager->DetachPane(palette);
		}
	}
	
	// Update AUI manager before destroying windows
	aui_manager->Update();
	
		// Now destroy each palette
		// Check if palette is still valid before destroying (AUI manager might have destroyed it)
		for (auto palette : tmp) {
			if (palette && !palette->IsBeingDeleted() && palette->GetParent()) {
				palette->Destroy();
			}
		}
}

void GUI::RebuildPalettes() {
	// Palette lits might be modified due to active palette changes
	// Use a temporary list for iterating
	PaletteList tmp = palettes;
	for (auto& piter : tmp) {
		piter->ReloadSettings(IsEditorOpen() ? &GetCurrentMap() : nullptr);
	}
	aui_manager->Update();
}

void GUI::RefreshTilesetAddition(TilesetCategoryType type, const std::string& tilesetName) {
	bool found = false;
	PaletteList tmp = palettes;
	for (auto& palette : tmp) {
		if (palette && palette->RefreshTilesetPage(type, tilesetName)) {
			found = true;
		}
	}
	// The page for this tileset/category does not exist yet (new tileset, or a
	// category that previously had no entries) - structure changed, so rebuild.
	if (!found) {
		RebuildPalettes();
	}
}

void GUI::ShowPalette() {
	if (palettes.empty()) {
		return;
	}

	for (auto& palette : palettes) {
		if (aui_manager->GetPane(palette).IsShown()) {
			return;
		}
	}

	aui_manager->GetPane(palettes.front()).Show(true);
	aui_manager->Update();
}

void GUI::SelectPalettePage(PaletteType pt) {
	if (palettes.empty()) {
		CreatePalette();
	}
	PaletteWindow* p = GetPalette();
	if (!p) {
		return;
	}

	ShowPalette();
	p->SelectPage(pt);
	aui_manager->Update();
	SelectBrushInternal(p->GetSelectedBrush());
}

//=============================================================================
// Minimap Window Interface Implementation

void GUI::CreateMinimap() {
	if (!IsVersionLoaded()) {
		return;
	}

	if (minimap) {
		aui_manager->GetPane(minimap).Show(true);
	} else {
		minimap = newd MinimapWindow(root);
		minimap->Show(true);
		wxAuiPaneInfo info;
		ConfigureMinimapAuiPane(info);
		aui_manager->AddPane(minimap, info);
	}
	aui_manager->Update();
	if (minimap) {
		minimap->UpdateCaptionChrome();
	}
}

void GUI::HideMinimap() {
	if (minimap) {
		aui_manager->GetPane(minimap).Show(false);
		aui_manager->Update();
	}
}

void GUI::DestroyMinimap() {
	if (!minimap) {
		return;
	}

	if (!aui_manager) {
		minimap->Destroy();
		minimap = nullptr;
		return;
	}

	if (minimap) {
		aui_manager->DetachPane(minimap);
		aui_manager->Update();
		// Check if minimap is still valid before destroying (AUI manager might have destroyed it)
		if (!minimap->IsBeingDeleted() && minimap->GetParent()) {
			minimap->Destroy();
		}
		minimap = nullptr;
	}
}

void GUI::UpdateMinimap(bool immediate) {
	if (IsMinimapVisible()) {
		minimap->MarkDirty();
		if (immediate) {
			minimap->RefreshCanvas();
		} else {
			minimap->DelayedUpdate();
		}
	}
}

bool GUI::IsMinimapVisible() const {
	if (minimap) {
		const wxAuiPaneInfo& pi = aui_manager->GetPane(minimap);
		if (pi.IsShown()) {
			return true;
		}
	}
	return false;
}

//=============================================================================

void GUI::RefreshView() {
	if (headless || !tabbook) {
		return;
	}

	EditorTab* editorTab = GetCurrentTab();
	if (!editorTab) {
		return;
	}

	if (!dynamic_cast<MapTab*>(editorTab)) {
		editorTab->GetWindow()->Refresh();
		return;
	}

	std::vector<EditorTab*> editorTabs;
	for (int32_t index = 0; index < tabbook->GetTabCount(); ++index) {
		auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index));
		if (mapTab) {
			editorTabs.push_back(mapTab);
		}
	}

	for (EditorTab* editorTab : editorTabs) {
		editorTab->GetWindow()->Refresh();
	}
}

void GUI::CreateLoadBar(wxString message, bool canCancel /* = false */) {
	if (headless || !root) {
		return;
	}

	progressText = message;

	progressFrom = 0;
	progressTo = 100;
	currentProgress = -1;

	progressBar = newd wxGenericProgressDialog("Loading", progressText + " (0%)", 100, root, wxPD_APP_MODAL | wxPD_SMOOTH | (canCancel ? wxPD_CAN_ABORT : 0));
	progressBar->SetSize(progressBar->FromDIP(wxSize(280, -1)));
	progressBar->Show(true);

	for (int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
	}
	progressBar->Update(0);
}

void GUI::SetLoadScale(int32_t from, int32_t to) {
	progressFrom = from;
	progressTo = to;
}

bool GUI::SetLoadDone(int32_t done, const wxString& newMessage) {
	if (done == 100) {
		DestroyLoadBar();
		return true;
	} else if (done == currentProgress) {
		return true;
	}

	if (!newMessage.empty()) {
		progressText = newMessage;
	}

	int32_t newProgress = progressFrom + static_cast<int32_t>((done / 100.f) * (progressTo - progressFrom));
	newProgress = std::max<int32_t>(0, std::min<int32_t>(100, newProgress));

	bool skip = false;
	if (progressBar) {
		progressBar->Update(
			newProgress,
			wxString::Format("%s (%d%%)", progressText, newProgress),
			&skip
		);
		currentProgress = newProgress;
	}


	return skip;
}

void GUI::DestroyLoadBar() {
	if (progressBar) {
		progressBar->Show(false);
		currentProgress = -1;

		progressBar->Destroy();
		progressBar = nullptr;

		if (root->IsActive()) {
			root->Raise();
		} else {
			root->RequestUserAttention();
		}
	}
}

void GUI::ShowWelcomeDialog(const wxBitmap& icon) {
	std::vector<wxString> recent_files = root->GetRecentFiles();
	welcomeDialog = newd WelcomeDialog(__W_RME_APPLICATION_NAME__, "Version " + __W_RME_VERSION__, FROM_DIP(root, wxSize(800, 480)), icon, recent_files);
	welcomeDialog->Bind(wxEVT_CLOSE_WINDOW, &GUI::OnWelcomeDialogClosed, this);
	welcomeDialog->Bind(WELCOME_DIALOG_ACTION, &GUI::OnWelcomeDialogAction, this);
	welcomeDialog->Show();
	UpdateMenubar();
}

void GUI::FinishWelcomeDialog() {
	if (welcomeDialog != nullptr) {
		welcomeDialog->Hide();
		root->Show();
		welcomeDialog->Destroy();
		welcomeDialog = nullptr;
	}
}

bool GUI::IsWelcomeDialogShown() {
	return welcomeDialog != nullptr && welcomeDialog->IsShown();
}

void GUI::OnWelcomeDialogClosed(wxCloseEvent& event) {
	welcomeDialog->Destroy();
	root->Close();
}

void GUI::OnWelcomeDialogAction(wxCommandEvent& event) {
	if (event.GetId() == wxID_NEW) {
		NewMap();
	} else if (event.GetId() == wxID_OPEN) {
		LoadMap(FileName(event.GetString()));
	} else if (event.GetId() == WELCOME_LIVE_CONNECT) {
		ConnectToLiveServer();
	}
}

void GUI::UpdateMenubar() {
	if (headless || !root) {
		return;
	}
	root->UpdateMenubar();
}

void GUI::SetScreenCenterPosition(Position position) {
	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		mapTab->SetScreenCenterPosition(position);
	}
}

void GUI::DoCut() {
	if (!IsSelectionMode()) {
		return;
	}

	Editor* editor = GetCurrentEditor();
	if (!editor || !editor->IsClipboardAllowed()) {
		return;
	}

	editor->copybuffer.cut(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoCopy() {
	if (!IsSelectionMode()) {
		return;
	}

	Editor* editor = GetCurrentEditor();
	if (!editor || !editor->IsClipboardAllowed()) {
		return;
	}

	editor->copybuffer.copy(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoPaste() {
	Editor* editor = GetCurrentEditor();
	if (editor && !editor->IsClipboardAllowed()) {
		return;
	}

	MapTab* mapTab = GetCurrentMapTab();
	if (mapTab) {
		copybuffer.paste(*mapTab->GetEditor(), mapTab->GetCanvas()->GetCursorPosition());
	}
}

void GUI::PreparePaste() {
	Editor* editor = GetCurrentEditor();
	if (!editor || !editor->IsClipboardAllowed()) {
		return;
	}
	SetSelectionMode();
	editor->selection.start();
	editor->selection.clear();
	editor->selection.finish();
	StartPasting();
	RefreshView();
}

void GUI::StartPasting() {
	if (GetCurrentEditor() && GetCurrentEditor()->IsClipboardAllowed()) {
		pasting = true;
		secondary_map = &copybuffer.getBufferMap();
		// The transformation menu items apply to the pending paste while it is held
		UpdateMenus();
	}
}

bool GUI::TransformPaste(MapTransform transform) {
	if (!pasting || !copybuffer.transform(transform)) {
		return false;
	}

	// The copybuffer rebuilds its map when transformed, re-point the preview at it
	secondary_map = &copybuffer.getBufferMap();
	RefreshView();
	return true;
}

void GUI::EndPasting() {
	if (pasting) {
		pasting = false;
		secondary_map = nullptr;
		UpdateMenus();
	}
}

bool GUI::CanUndo() {
	Editor* editor = GetCurrentEditor();
	return (editor && editor->actionQueue->canUndo());
}

bool GUI::CanRedo() {
	Editor* editor = GetCurrentEditor();
	return (editor && editor->actionQueue->canRedo());
}

bool GUI::DoUndo() {
	Editor* editor = GetCurrentEditor();
	if (editor && editor->actionQueue->canUndo()) {
		editor->actionQueue->undo();
		if (editor->selection.size() > 0) {
			SetSelectionMode();
		}
		SetStatusText("Undo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

bool GUI::DoRedo() {
	Editor* editor = GetCurrentEditor();
	if (editor && editor->actionQueue->canRedo()) {
		editor->actionQueue->redo();
		if (editor->selection.size() > 0) {
			SetSelectionMode();
		}
		SetStatusText("Redo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

int GUI::GetCurrentFloor() {
	MapTab* tab = GetCurrentMapTab();
	ASSERT(tab);
	return tab->GetCanvas()->GetFloor();
}

void GUI::ChangeFloor(int new_floor) {
	MapTab* tab = GetCurrentMapTab();
	if (tab) {
		int old_floor = GetCurrentFloor();
		if (new_floor < 0 || new_floor > MAP_MAX_LAYER) {
			return;
		}

		if (old_floor != new_floor) {
			tab->GetCanvas()->ChangeFloor(new_floor);
		}
	}
}

void GUI::SetStatusText(wxString text) {
	if (headless || !g_gui.root) {
		return;
	}
	g_gui.root->SetStatusText(text, 0);
}

void GUI::SetTitle(wxString title) {
	if (g_gui.root == nullptr) {
		return;
	}

#ifdef NIGHTLY_BUILD
	#ifdef SVN_BUILD
		#define TITLE_APPEND (wxString(" (Nightly Build #") << i2ws(SVN_BUILD) << ")")
	#else
		#define TITLE_APPEND (wxString(" (Nightly Build)"))
	#endif
#else
	#ifdef SVN_BUILD
		#define TITLE_APPEND (wxString(" (Build #") << i2ws(SVN_BUILD) << ")")
	#else
		#define TITLE_APPEND (wxString(""))
	#endif
#endif
#ifdef __EXPERIMENTAL__
	if (title != "") {
		g_gui.root->SetTitle(title << " - Tilera BETA" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Tilera BETA") << TITLE_APPEND);
	}
#elif __SNAPSHOT__
	if (title != "") {
		g_gui.root->SetTitle(title << " - Tilera - SNAPSHOT" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Tilera - SNAPSHOT") << TITLE_APPEND);
	}
#else
	if (!title.empty()) {
		g_gui.root->SetTitle(title << " - Tilera" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Tilera") << TITLE_APPEND);
	}
#endif
}

void GUI::UpdateTitle() {
	if (headless || !tabbook) {
		return;
	}

	if (tabbook->GetTabCount() > 0) {
		SetTitle(tabbook->GetCurrentTab()->GetTitle());
		for (int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
			if (tabbook->GetTab(idx)) {
				tabbook->SetTabLabel(idx, tabbook->GetTab(idx)->GetTitle());
			}
		}
	} else {
		SetTitle("");
	}
}

void GUI::UpdateMenus() {
	if (headless || !root) {
		return;
	}
	wxCommandEvent evt(EVT_UPDATE_MENUS);
	g_gui.root->AddPendingEvent(evt);
}

void GUI::ShowToolbar(ToolBarID id, bool show) {
	if (root && root->GetAuiToolBar()) {
		root->GetAuiToolBar()->Show(id, show);
	}
}

void GUI::SwitchMode() {
	if (mode == DRAWING_MODE) {
		SetSelectionMode();
	} else {
		SetDrawingMode();
	}
}

void GUI::SetSelectionMode() {
	if (mode == SELECTION_MODE) {
		return;
	}

	if (current_brush && current_brush->isDoodad()) {
		secondary_map = nullptr;
	}

	// No tabbook when running headless - the mode still has to be tracked,
	// there is just no UI to switch over.
	if (tabbook) {
		tabbook->OnSwitchEditorMode(SELECTION_MODE);
	}
	mode = SELECTION_MODE;
}

void GUI::SetDrawingMode() {
	if (mode == DRAWING_MODE) {
		return;
	}

	// No tabbook when running headless - there are no open tabs whose
	// selection needs clearing, and no UI to switch over.
	if (tabbook) {
		std::set<MapTab*> al;
		for (int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
			EditorTab* editorTab = tabbook->GetTab(idx);
			if (auto* mapTab = dynamic_cast<MapTab*>(editorTab)) {
				if (al.find(mapTab) != al.end()) {
					continue;
				}

				Editor* editor = mapTab->GetEditor();
				editor->selection.start();
				editor->selection.clear();
				editor->selection.finish();
				al.insert(mapTab);
			}
		}
	}

	if (current_brush && current_brush->isDoodad()) {
		secondary_map = doodad_buffer_map;
	} else {
		secondary_map = nullptr;
	}

	if (tabbook) {
		tabbook->OnSwitchEditorMode(DRAWING_MODE);
	}
	mode = DRAWING_MODE;
}

void GUI::SetBrushSizeInternal(int nz) {
	bool new_even = (nz == BRUSH_SIZE_2X2);
	int new_size = new_even ? 1 : std::max(nz, 0);

	bool changed = (new_size != brush_size) || (new_even != brush_size_even);

	brush_size = new_size;
	brush_size_even = new_even;

	if (changed && current_brush && current_brush->isDoodad() && !current_brush->oneSizeFitsAll()) {
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	}
}

void GUI::SetBrushSize(int nz) {
	SetBrushSizeInternal(nz);

	const int selection_value = GetBrushSizeSelectionValue();

	for (auto& palette : palettes) {
		palette->OnUpdateBrushSize(brush_shape, selection_value);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brush_shape, selection_value);
}

void GUI::SetBrushVariation(int nz) {
	if (nz != brush_variation && current_brush && current_brush->isDoodad()) {
		// Monkey!
		brush_variation = nz;
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	}
}

bool GUI::RotateDoodadPreviewItems() {
	if (!current_brush || !current_brush->isDoodad()) {
		return false;
	}

	bool rotated = false;
	for (MapIterator it = doodad_buffer_map->begin(); it != doodad_buffer_map->end(); ++it) {
		Tile* tile = (*it)->get();
		if (tile->ground && tile->ground->isRoteable()) {
			tile->ground->doRotate();
			rotated = true;
		}
		for (Item* item : tile->items) {
			if (item->isRoteable()) {
				item->doRotate();
				rotated = true;
			}
		}
	}

	if (rotated) {
		secondary_map = doodad_buffer_map;
	}
	return rotated;
}

void GUI::SetBrushShape(BrushShape bs) {
	bool shapeChanged = (bs != brush_shape);
	bool evenChanged = false;

	if (bs == BRUSHSHAPE_CIRCLE && brush_size_even) {
		brush_size_even = false;
		evenChanged = true;
	}

	brush_shape = bs;

	if ((shapeChanged || evenChanged) && current_brush && current_brush->isDoodad() && !current_brush->oneSizeFitsAll()) {
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	}

	const int selection_value = GetBrushSizeSelectionValue();

	for (auto& palette : palettes) {
		palette->OnUpdateBrushSize(brush_shape, selection_value);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brush_shape, selection_value);
}

void GUI::SetBrushThickness(bool on, int x, int y) {
	use_custom_thickness = on;

	if (x != -1 || y != -1) {
		custom_thickness_mod = float(max(x, 1)) / float(max(y, 1));
	}

	if (current_brush && current_brush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::SetBrushThickness(int low, int ceil) {
	custom_thickness_mod = float(max(low, 1)) / float(max(ceil, 1));

	if (use_custom_thickness && current_brush && current_brush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::DecreaseBrushSize(bool wrap) {
	static const std::array<int, 8> options = { 0, BRUSH_SIZE_2X2, 1, 2, 4, 6, 8, 11 };
	const int selection = GetBrushSizeSelectionValue();
	auto it = std::find(options.begin(), options.end(), selection);
	if (it != options.end()) {
		if (it == options.begin()) {
			if (wrap) {
				SetBrushSize(options.back());
			}
		} else {
			SetBrushSize(*(it - 1));
		}
		return;
	}

	// Fallback for unusual brush sizes
	const int fallback = std::max(selection - 1, 0);
	SetBrushSize(fallback);
}

void GUI::IncreaseBrushSize(bool wrap) {
	static const std::array<int, 8> options = { 0, BRUSH_SIZE_2X2, 1, 2, 4, 6, 8, 11 };
	const int selection = GetBrushSizeSelectionValue();
	auto it = std::find(options.begin(), options.end(), selection);
	if (it != options.end()) {
		if (std::next(it) == options.end()) {
			if (wrap) {
				SetBrushSize(options.front());
			}
		} else {
			SetBrushSize(*std::next(it));
		}
		return;
	}

	SetBrushSize(selection + 1);
}

void GUI::SetDoorLocked(bool on) {
	draw_locked_doors = on;
	RefreshView();
}

bool GUI::HasDoorLocked() {
	return draw_locked_doors;
}

Brush* GUI::GetCurrentBrush() const {
	return current_brush;
}

BrushShape GUI::GetBrushShape() const {
	if (current_brush == spawn_brush) {
		return BRUSHSHAPE_SQUARE;
	}

	return brush_shape;
}

int GUI::GetBrushSize() const {
	return brush_size;
}

int GUI::GetBrushSizeSelectionValue() const {
	return brush_size_even ? BRUSH_SIZE_2X2 : brush_size;
}

bool GUI::IsBrushEvenSize() const {
	return brush_size_even;
}

int GUI::GetSquareBrushMinOffset() const {
	return -brush_size;
}

int GUI::GetSquareBrushMaxOffset() const {
	return brush_size_even ? brush_size - 1 : brush_size;
}

int GUI::GetSquareBrushOuterMinOffset() const {
	return GetSquareBrushMinOffset() - 1;
}

int GUI::GetSquareBrushOuterMaxOffset() const {
	return GetSquareBrushMaxOffset() + 1;
}

bool GUI::IsSingleTileBrush() const {
	return !brush_size_even && brush_size == 0;
}

int GUI::GetBrushVariation() const {
	return brush_variation;
}

int GUI::GetSpawnTime() const {
	return creature_spawntime;
}

void GUI::SelectBrush() {
	if (palettes.empty()) {
		return;
	}

	SelectBrushInternal(palettes.front()->GetSelectedBrush());

	RefreshView();
}

bool GUI::SelectBrush(const Brush* whatbrush, PaletteType primary) {
	if (palettes.empty()) {
		if (!CreatePalette()) {
			return false;
		}
	}

	if (!palettes.front()->OnSelectBrush(whatbrush, primary)) {
		return false;
	}

	SelectBrushInternal(const_cast<Brush*>(whatbrush));
	root->GetAuiToolBar()->UpdateBrushButtons();

	Editor* editor = GetCurrentEditor();
	if (editor) {
		editor->warnLiveBlockedBrushUse(whatbrush);
	}

	return true;
}

void GUI::SelectBrushInternal(Brush* brush) {
	// Fear no evil don't you say no evil
	if (current_brush != brush && brush) {
		previous_brush = current_brush;
		brush_variation = 0;
	}

	current_brush = brush;
	if (!current_brush) {
		return;
	}

	// The terraforming tools cap their columns with the last ground the user picked
	if (brush->isGround() && brush->asGround()) {
		last_ground_brush = brush->asGround();
	}

	const int max_variation = max(0, brush->getMaxVariation() - 1);
	brush_variation = min(brush_variation, max_variation);
	FillDoodadPreviewBuffer();
	if (brush->isDoodad()) {
		secondary_map = doodad_buffer_map;
	}

	SetDrawingMode();
	RefreshView();
}

void GUI::SelectPreviousBrush() {
	if (previous_brush) {
		SelectBrush(previous_brush);
	}
}

void GUI::FillDoodadPreviewBuffer() {
	if (!current_brush || !current_brush->isDoodad()) {
		return;
	}

	doodad_buffer_map->clear();

	DoodadBrush* brush = current_brush->asDoodad();
	if (brush->isEmpty(GetBrushVariation())) {
		return;
	}

	int object_count = 0;
	int area;
	if (GetBrushShape() == BRUSHSHAPE_SQUARE) {
		area = 2 * GetBrushSize();
		area = area * area + 1;
	} else {
		if (GetBrushSize() == 1) {
			// There is a huge deviation here with the other formula.
			area = 5;
		} else {
			area = int(0.5 + GetBrushSize() * GetBrushSize() * PI);
		}
	}
	const int object_range = (use_custom_thickness ? int(area * custom_thickness_mod) : brush->getThickness() * area / max(1, brush->getThicknessCeiling()));
	const int final_object_count = max(1, object_range + random(object_range));

	Position center_pos(0x8000, 0x8000, 0x8);

	if (brush_size > 0 && !brush->oneSizeFitsAll()) {
		while (object_count < final_object_count) {
			int retries = 0;
			bool exit = false;

			// Try to place objects 5 times
			while (retries < 5 && !exit) {

				int pos_retries = 0;
				int xpos = 0, ypos = 0;
				bool found_pos = false;
				if (GetBrushShape() == BRUSHSHAPE_CIRCLE) {
					while (pos_retries < 5 && !found_pos) {
						xpos = random(-brush_size, brush_size);
						ypos = random(-brush_size, brush_size);
						float distance = sqrt(float(xpos * xpos) + float(ypos * ypos));
						if (distance < g_gui.GetBrushSize() + 0.005) {
							found_pos = true;
						} else {
							++pos_retries;
						}
					}
				} else {
					found_pos = true;
					const int min_offset = GetSquareBrushMinOffset();
					const int max_offset = GetSquareBrushMaxOffset();
					xpos = random(min_offset, max_offset);
					ypos = random(min_offset, max_offset);
				}

				if (!found_pos) {
					++retries;
					continue;
				}

				// Decide whether the zone should have a composite or several single objects.
				bool fail = false;
				if (random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
					// Composite
					const CompositeTileList& composites = brush->getComposite(GetBrushVariation());

					// Figure out if the placement is valid
					for (const auto& composite : composites) {
						Position pos = center_pos + composite.first + Position(xpos, ypos, 0);
						if (Tile* tile = doodad_buffer_map->getTile(pos)) {
							if (!tile->empty()) {
								fail = true;
								break;
							}
						}
					}
					if (fail) {
						++retries;
						break;
					}

					// Transfer items to the stack
					for (const auto& composite : composites) {
						Position pos = center_pos + composite.first + Position(xpos, ypos, 0);
						const ItemVector& items = composite.second;
						Tile* tile = doodad_buffer_map->getTile(pos);

						if (!tile) {
							tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
						}

						for (auto item : items) {
							tile->addItem(item->deepCopy());
						}
						doodad_buffer_map->setTile(tile->getPosition(), tile);
					}
					exit = true;
				} else if (brush->hasSingleObjects(GetBrushVariation())) {
					Position pos = center_pos + Position(xpos, ypos, 0);
					Tile* tile = doodad_buffer_map->getTile(pos);
					if (tile) {
						if (!tile->empty()) {
							fail = true;
							break;
						}
					} else {
						tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
					}
					int variation = GetBrushVariation();
					brush->draw(doodad_buffer_map, tile, &variation);
					// std::cout << "\tpos: " << tile->getPosition() << std::endl;
					doodad_buffer_map->setTile(tile->getPosition(), tile);
					exit = true;
				}
				if (fail) {
					++retries;
					break;
				}
			}
			++object_count;
		}
	} else {
		if (brush->hasCompositeObjects(GetBrushVariation()) && random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
			// Composite
			const CompositeTileList& composites = brush->getComposite(GetBrushVariation());

			// All placement is valid...

			// Transfer items to the buffer
			for (const auto& composite : composites) {
				Position pos = center_pos + composite.first;
				const ItemVector& items = composite.second;
				Tile* tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
				// std::cout << pos << " = " << center_pos << " + " << buffer_tile->getPosition() << std::endl;

				for (auto item : items) {
					tile->addItem(item->deepCopy());
				}
				doodad_buffer_map->setTile(tile->getPosition(), tile);
			}
		} else if (brush->hasSingleObjects(GetBrushVariation())) {
			Tile* tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(center_pos));
			int variation = GetBrushVariation();
			brush->draw(doodad_buffer_map, tile, &variation);
			doodad_buffer_map->setTile(center_pos, tile);
		}
	}
}

long GUI::PopupDialog(wxWindow* parent, wxString title, wxString text, long style, wxString confisavename, uint32_t configsavevalue) {
	if (text.empty()) {
		return wxID_ANY;
	}

	// In headless (standalone server) mode there is no UI to show a modal
	// dialog on, so log to the console and answer affirmatively.
	if (headless || !root) {
		std::cout << "[" << title.ToStdString() << "] " << text.ToStdString() << std::endl;
		if (style & wxYES) {
			return wxID_YES;
		}
		return wxID_OK;
	}

	wxMessageDialog dlg(parent, text, title, style);
	return dlg.ShowModal();
}

long GUI::PopupDialog(wxString title, wxString text, long style, wxString configsavename, uint32_t configsavevalue) {
	return g_gui.PopupDialog(g_gui.root, title, text, style, configsavename, configsavevalue);
}

void GUI::ListDialog(wxWindow* parent, wxString title, const wxArrayString& param_items) {
	if (param_items.empty()) {
		return;
	}

	if (headless || !root) {
		std::cout << "[" << title.ToStdString() << "]" << std::endl;
		for (const wxString& item : param_items) {
			std::cout << "  " << item.ToStdString() << std::endl;
		}
		return;
	}

	WarningDialog dlg(parent, title, param_items);
	dlg.ShowModal();
}

void GUI::ShowTextBox(wxWindow* parent, wxString title, wxString content) {
	wxDialog* dlg = newd wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxTextCtrl* text_field = newd wxTextCtrl(dlg, wxID_ANY, content, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	text_field->SetMinSize(dlg->FromDIP(wxSize(400, 550)));
	topsizer->Add(text_field, wxSizerFlags(5).Expand());

	wxSizer* choicesizer = newd wxBoxSizer(wxHORIZONTAL);
	choicesizer->Add(newd wxButton(dlg, wxID_CANCEL, "OK"), wxSizerFlags(1).Center());
	topsizer->Add(choicesizer, wxSizerFlags(0).Center());
	dlg->SetSizerAndFit(topsizer);

	dlg->ShowModal();
}

void GUI::SetHotkey(int index, Hotkey& hotkey) {
	ASSERT(index >= 0 && index <= 9);
	hotkeys[index] = hotkey;
	SetStatusText("Set hotkey " + i2ws(index) + ".");
}

const Hotkey& GUI::GetHotkey(int index) const {
	ASSERT(index >= 0 && index <= 9);
	return hotkeys[index];
}

void GUI::SaveHotkeys() const {
	std::ostringstream os;
	for (const auto& hotkey : hotkeys) {
		os << hotkey << '\n';
	}
	g_settings.setString(Config::NUMERICAL_HOTKEYS, os.str());
}

void GUI::LoadHotkeys() {
	std::istringstream is;
	is.str(g_settings.getString(Config::NUMERICAL_HOTKEYS));

	std::string line;
	int index = 0;
	while (getline(is, line)) {
		std::istringstream line_is;
		line_is.str(line);
		line_is >> hotkeys[index];

		++index;
	}
}

Hotkey::Hotkey() :
	type(NONE) {
	////
}

Hotkey::Hotkey(Position _pos) : type(POSITION), pos(_pos) {
	////
}

Hotkey::Hotkey(Brush* brush) : type(BRUSH), brushname(brush->getName()) {
	////
}

Hotkey::Hotkey(std::string _name) : type(BRUSH), brushname(_name) {
	////
}

Hotkey::~Hotkey() {
	////
}

std::ostream& operator<<(std::ostream& os, const Hotkey& hotkey) {
	switch (hotkey.type) {
		case Hotkey::POSITION: {
			os << "pos:{" << hotkey.pos << "}";
		} break;
		case Hotkey::BRUSH: {
			if (hotkey.brushname.find('{') != std::string::npos || hotkey.brushname.find('}') != std::string::npos) {
				break;
			}
			os << "brush:{" << hotkey.brushname << "}";
		} break;
		default: {
			os << "none:{}";
		} break;
	}
	return os;
}

std::istream& operator>>(std::istream& is, Hotkey& hotkey) {
	std::string type;
	getline(is, type, ':');
	if (type == "none") {
		is.ignore(2); // ignore "{}"
	} else if (type == "pos") {
		is.ignore(1); // ignore "{"
		Position pos;
		is >> pos;
		hotkey = Hotkey(pos);
		is.ignore(1); // ignore "}"
	} else if (type == "brush") {
		is.ignore(1); // ignore "{"
		std::string brushname;
		getline(is, brushname, '}');
		hotkey = Hotkey(brushname);
	} else {
		// Do nothing...
	}

	return is;
}

void SetWindowToolTip(wxWindow* a, const wxString& tip) {
	a->SetToolTip(tip);
}

void SetWindowToolTip(wxWindow* a, wxWindow* b, const wxString& tip) {
	a->SetToolTip(tip);
	b->SetToolTip(tip);
}
