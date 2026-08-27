//////////////////////////////////////////////////////////////////////
// Standalone console host for live collaborative map editing.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "live_server.h"
#include "live_peer.h"
#include "live_update.h"
#include "live_session_bounds.h"
#include "editor.h"
#include "gui.h"
#include "settings.h"
#include "client_version.h"
#include "net_connection.h"
#include "iomap_otbm.h"
#include "map.h"

#ifdef __APPLE__
	#include <GLUT/glut.h>
#else
	#include <GL/glut.h>
#endif

#include <wx/app.h>
#include <wx/init.h>

#ifdef __WINDOWS__
	#include <io.h>
#else
	#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <wx/stdpaths.h>
#include <wx/textfile.h>

namespace {

struct MapServerConfig {
	FileName mapPath;
	uint16_t port = 31313;
	wxString password;
	wxString sessionName;
	LiveSessionBounds sessionBounds;
	FileName assetsPath;
	FileName itemsPath;
	FileName monstersPath;
	FileName npcsPath;
	unsigned autosaveMinutes = 5;
	std::vector<wxString> updateFiles;
	bool updateFilesConfigured = false;
};

std::atomic<bool> g_running { true };
Editor* g_editor = nullptr;
LiveServer* g_server = nullptr;
unsigned g_autosaveIntervalSeconds = 5 * 60;
std::chrono::steady_clock::time_point g_lastAutosave = std::chrono::steady_clock::now();
std::mutex g_commandMutex;
std::vector<std::string> g_pendingCommands;

void printUsage() {
	std::cout << "MapServer - Live collaboration host for Remere's Map Editor\n"
	          << "Usage: MapServer_x64.exe [options]\n"
	          << "\nSettings are read from mapserver.cfg next to the executable.\n"
	          << "Copy mapserver.cfg.example to mapserver.cfg and edit it, then run MapServer_x64.exe.\n"
	          << "\nCommand-line options override mapserver.cfg:\n"
	          << "  --map <file.otbm>     Map file to host\n"
	          << "  --port <number>       TCP port (default 31313)\n"
	          << "  --password <pass>     Session password (default: empty)\n"
	          << "  --name <name>         Session display name\n"
	          << "  --autosave <minutes>  Auto-save interval (default 5, 0 disables)\n"
	          << "  --x <coord>           Session center X (requires y, z, radius)\n"
	          << "  --y <coord>           Session center Y\n"
	          << "  --z <floor>           Session center floor\n"
	          << "  --radius <sqm>        Editable/viewable radius around center\n"
	          << "  --assets <directory>  Folder with Tibia.dat, Tibia.spr, and Tibia.otfi\n"
	          << "  --items <directory>   Folder with items.otb and items.xml\n"
	          << "  --monsters <directory> Folder with monster XML files\n"
	          << "  --npcs <directory>    Folder with NPC XML files\n"
	          << "\nConsole commands: save, list, kick <name>, block <id|from-to>, exit\n";
}

wxString getMapServerConfigPath() {
	FileName execPath;
	try {
		execPath = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast&) {
		execPath = FileName(".");
	}

	FileName configPath;
	configPath.Assign(execPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), "mapserver.cfg");
	return configPath.GetFullPath();
}

wxString trimMapServerConfigValue(const wxString& value) {
	wxString trimmed = value;
	trimmed.Trim(true).Trim(false);
	if (trimmed.length() >= 2 &&
	    ((trimmed.StartsWith("\"") && trimmed.EndsWith("\"")) || (trimmed.StartsWith("'") && trimmed.EndsWith("'")))) {
		trimmed = trimmed.Mid(1, trimmed.length() - 2);
	}
	return trimmed;
}

bool parseMapServerConfigLong(const wxString& value, long& out) {
	const wxString trimmed = trimMapServerConfigValue(value);
	if (trimmed.empty()) {
		return false;
	}
	return trimmed.ToLong(&out);
}

bool isStdinInteractive() {
#ifdef __WINDOWS__
	return _isatty(_fileno(stdin)) != 0;
#else
	return isatty(fileno(stdin)) != 0;
#endif
}

void waitForEnterOnStartupFailure(int argc) {
	if (argc > 1) {
		return;
	}

	std::cout << std::endl << "Press Enter to exit..." << std::endl;
	std::cin.clear();
	std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	std::cin.get();
}

bool loadMapServerConfig(MapServerConfig& config) {
	const wxString configPath = getMapServerConfigPath();
	if (!wxFileName(configPath).FileExists()) {
		return true;
	}

	wxTextFile file;
	if (!file.Open(configPath)) {
		std::cerr << "Could not read mapserver.cfg: " << configPath.ToStdString() << std::endl;
		return false;
	}

	long centerX = -1;
	long centerY = -1;
	long centerZ = -1;
	long radius = -1;
	bool hasCenterX = false;
	bool hasCenterY = false;
	bool hasCenterZ = false;
	bool hasRadius = false;

	for (size_t lineNumber = 0; lineNumber < file.GetLineCount(); ++lineNumber) {
		wxString line = file.GetLine(lineNumber);
		line.Trim(true).Trim(false);
		if (line.empty() || line.StartsWith("#") || line.StartsWith(";")) {
			continue;
		}
		if (line.StartsWith("[") && line.EndsWith("]")) {
			continue;
		}

		const int separator = line.Find('=');
		if (separator == wxNOT_FOUND) {
			continue;
		}

		wxString key = line.Left(separator);
		key.Trim(true).Trim(false);
		key.MakeUpper();

		const wxString value = trimMapServerConfigValue(line.Mid(separator + 1));

		if (key == "MAP") {
			if (!value.empty()) {
				config.mapPath.Assign(value);
			}
		} else if (key == "PORT") {
			long port = 0;
			if (parseMapServerConfigLong(value, port) && port >= 1 && port <= 65535) {
				config.port = static_cast<uint16_t>(port);
			}
		} else if (key == "PASSWORD") {
			config.password = value;
		} else if (key == "NAME") {
			config.sessionName = value;
		} else if (key == "AUTOSAVE") {
			long autosaveMinutes = -1;
			if (parseMapServerConfigLong(value, autosaveMinutes) && autosaveMinutes >= 0) {
				config.autosaveMinutes = static_cast<unsigned>(autosaveMinutes);
			}
		} else if (key == "ASSETS") {
			if (!value.empty()) {
				config.assetsPath.AssignDir(value);
			}
		} else if (key == "ITEMS" || key == "ITEM") {
			if (!value.empty()) {
				config.itemsPath.AssignDir(value);
			}
		} else if (key == "MONSTERS" || key == "MONSTER") {
			if (!value.empty()) {
				config.monstersPath.AssignDir(value);
			}
		} else if (key == "NPCS" || key == "NPC") {
			if (!value.empty()) {
				config.npcsPath.AssignDir(value);
			}
		} else if (key == "UPDATE_FILES" || key == "UPDATE_FILE" || key == "UPDATE") {
			if (!value.empty()) {
				config.updateFilesConfigured = true;
				wxArrayString entries = wxSplit(value, ';');
				for (const wxString& entry : entries) {
					wxString trimmed = entry;
					trimmed.Trim(true).Trim(false);
					if (!trimmed.empty()) {
						config.updateFiles.push_back(trimmed);
					}
				}
			}
		} else if (key == "CENTER_X") {
			hasCenterX = parseMapServerConfigLong(value, centerX);
		} else if (key == "CENTER_Y") {
			hasCenterY = parseMapServerConfigLong(value, centerY);
		} else if (key == "CENTER_Z") {
			hasCenterZ = parseMapServerConfigLong(value, centerZ);
		} else if (key == "RADIUS") {
			hasRadius = parseMapServerConfigLong(value, radius);
		}
	}

	const bool anyBounds = hasCenterX || hasCenterY || hasCenterZ || hasRadius;
	if (anyBounds) {
		if (!hasCenterX || !hasCenterY || !hasCenterZ || !hasRadius || centerX < 0 || centerY < 0 || centerZ < 0 || radius <= 0) {
			std::cerr << "mapserver.cfg: session bounds require CENTER_X, CENTER_Y, CENTER_Z, and RADIUS." << std::endl;
			return false;
		}
		if (centerZ > 15) {
			std::cerr << "mapserver.cfg: CENTER_Z must be between 0 and 15." << std::endl;
			return false;
		}
		config.sessionBounds.enabled = true;
		config.sessionBounds.centerX = static_cast<uint16_t>(centerX);
		config.sessionBounds.centerY = static_cast<uint16_t>(centerY);
		config.sessionBounds.centerZ = static_cast<uint8_t>(centerZ);
		config.sessionBounds.radius = static_cast<uint32_t>(radius);
	}

	std::cout << "[live] Loaded settings from " << configPath.ToStdString() << std::endl;
	return true;
}

bool validateMapServerConfig(MapServerConfig& config) {
	if (!config.mapPath.FileExists()) {
		std::cerr << "Map file not found";
		if (!config.mapPath.GetFullPath().empty()) {
			std::cerr << ": " << config.mapPath.GetFullPath().ToStdString();
		}
		std::cerr << std::endl;
		std::cerr << "Set MAP in mapserver.cfg or pass --map <file.otbm>." << std::endl;
		std::cerr << "Expected config file: " << getMapServerConfigPath().ToStdString() << std::endl;
		printUsage();
		return false;
	}
	return true;
}

size_t countMapCreatures(Map& map) {
	size_t count = 0;
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		Tile* tile = (*it)->get();
		if (tile && tile->creature) {
			++count;
		}
	}
	return count;
}

void logMapLoadSummary(Map& map) {
	std::cout << "[live] House file: " << map.getHouseFilename() << std::endl;
	std::cout << "[live] Houses on map: " << map.houses.count() << std::endl;
	std::cout << "[live] Spawn file: " << map.getSpawnFilename() << std::endl;
	std::cout << "[live] Creatures on map: " << countMapCreatures(map) << std::endl;

	if (!map.hasWarnings()) {
		return;
	}

	std::cout << "[live] Map loader warnings:" << std::endl;
	for (const wxString& warning : map.getWarnings()) {
		std::cout << "  " << warning.ToStdString() << std::endl;
	}
}

bool saveHostedMap(bool automatic) {
	if (!g_editor) {
		return false;
	}

	FileName filename = wxstr(g_editor->getMap().getFilename());
	if (filename.GetFullPath().empty()) {
		std::cout << "[live] Map has no filename; cannot save." << std::endl;
		return false;
	}

	if (!g_editor->saveMap(filename, false)) {
		std::cout << "[live] Save failed." << std::endl;
		return false;
	}

	g_lastAutosave = std::chrono::steady_clock::now();

	std::cout << "[live] " << (automatic ? "Auto-saved" : "Saved")
	          << " map to " << filename.GetFullPath().ToStdString() << std::endl;
	return true;
}

void tickAutosave() {
	if (!g_editor || g_autosaveIntervalSeconds == 0) {
		return;
	}

	if (!g_editor->getMap().hasChanged()) {
		return;
	}

	const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - g_lastAutosave
	).count();

	if (elapsed >= static_cast<long long>(g_autosaveIntervalSeconds)) {
		saveHostedMap(true);
	}
}

bool parseArgs(int argc, wxChar** argv, MapServerConfig& config) {
	int boundsX = config.sessionBounds.enabled ? static_cast<int>(config.sessionBounds.centerX) : -1;
	int boundsY = config.sessionBounds.enabled ? static_cast<int>(config.sessionBounds.centerY) : -1;
	int boundsZ = config.sessionBounds.enabled ? static_cast<int>(config.sessionBounds.centerZ) : -1;
	int boundsRadius = config.sessionBounds.enabled ? static_cast<int>(config.sessionBounds.radius) : -1;
	bool boundsConfigured = config.sessionBounds.enabled;

	for (int i = 1; i < argc; ++i) {
		wxString arg = argv[i];
		if (arg == wxT("--help") || arg == wxT("-h")) {
			printUsage();
			return false;
		} else if (arg == wxT("--map") && i + 1 < argc) {
			config.mapPath.Assign(argv[++i]);
		} else if (arg == wxT("--port") && i + 1 < argc) {
			long parsedPort = wxAtoi(argv[++i]);
			if (parsedPort < 1 || parsedPort > 65535) {
				std::cerr << "Invalid port number." << std::endl;
				return false;
			}
			config.port = static_cast<uint16_t>(parsedPort);
		} else if (arg == wxT("--password") && i + 1 < argc) {
			config.password = argv[++i];
		} else if (arg == wxT("--name") && i + 1 < argc) {
			config.sessionName = argv[++i];
		} else if (arg == wxT("--autosave") && i + 1 < argc) {
			long autosaveMinutes = wxAtoi(argv[++i]);
			if (autosaveMinutes < 0) {
				std::cerr << "Invalid autosave interval." << std::endl;
				return false;
			}
			config.autosaveMinutes = static_cast<unsigned>(autosaveMinutes);
		} else if (arg == wxT("--x") && i + 1 < argc) {
			boundsX = wxAtoi(argv[++i]);
			boundsConfigured = true;
		} else if (arg == wxT("--y") && i + 1 < argc) {
			boundsY = wxAtoi(argv[++i]);
			boundsConfigured = true;
		} else if (arg == wxT("--z") && i + 1 < argc) {
			boundsZ = wxAtoi(argv[++i]);
			boundsConfigured = true;
		} else if (arg == wxT("--radius") && i + 1 < argc) {
			boundsRadius = wxAtoi(argv[++i]);
			boundsConfigured = true;
		} else if (arg == wxT("--assets") && i + 1 < argc) {
			config.assetsPath.AssignDir(argv[++i]);
		} else if (arg == wxT("--items") && i + 1 < argc) {
			config.itemsPath.AssignDir(argv[++i]);
		} else if (arg == wxT("--monsters") && i + 1 < argc) {
			config.monstersPath.AssignDir(argv[++i]);
		} else if (arg == wxT("--npcs") && i + 1 < argc) {
			config.npcsPath.AssignDir(argv[++i]);
		} else {
			std::cerr << "Unknown argument: " << arg.ToStdString() << std::endl;
			return false;
		}
	}

	if (boundsConfigured) {
		if (boundsX < 0 || boundsY < 0 || boundsZ < 0 || boundsRadius <= 0) {
			std::cerr << "Session bounds require all of --x, --y, --z, and --radius (or CENTER_X/Y/Z and RADIUS in mapserver.cfg)." << std::endl;
			return false;
		}
		if (boundsZ > 15) {
			std::cerr << "Invalid session floor (0-15)." << std::endl;
			return false;
		}
		config.sessionBounds.enabled = true;
		config.sessionBounds.centerX = static_cast<uint16_t>(boundsX);
		config.sessionBounds.centerY = static_cast<uint16_t>(boundsY);
		config.sessionBounds.centerZ = static_cast<uint8_t>(boundsZ);
		config.sessionBounds.radius = static_cast<uint32_t>(boundsRadius);
	} else {
		config.sessionBounds.enabled = false;
	}

	return validateMapServerConfig(config);
}

wxString normalizeConfigDirectory(const wxString& rawPath) {
	if (rawPath.empty()) {
		return wxEmptyString;
	}

	FileName directory;
	directory.AssignDir(rawPath);
	if (!directory.DirExists()) {
		return wxEmptyString;
	}

	wxString normalized = directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	if (!normalized.EndsWith(FileName::GetPathSeparator())) {
		normalized << FileName::GetPathSeparator();
	}
	return normalized;
}

wxString getMapDataRoot(const MapServerConfig& config) {
	FileName mapDirectory;
	mapDirectory.AssignDir(config.mapPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR));
	if (!mapDirectory.DirExists() || mapDirectory.GetDirCount() == 0) {
		return wxEmptyString;
	}

	FileName dataDirectory = mapDirectory;
	dataDirectory.RemoveLastDir();
	return dataDirectory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

bool directoryHasItemsFiles(const FileName& directory) {
	const FileName itemsOtb(directory.GetFullPath(), "items.otb");
	const FileName itemsXml(directory.GetFullPath(), "items.xml");
	return itemsOtb.FileExists() && itemsXml.FileExists();
}

void resolveDefaultServerDataPaths(MapServerConfig& config) {
	const wxString dataRoot = getMapDataRoot(config);
	if (dataRoot.empty()) {
		return;
	}

	if (config.monstersPath.GetFullPath().empty()) {
		FileName monstersDirectory;
		monstersDirectory.AssignDir(dataRoot);
		monstersDirectory.AppendDir("monster");
		if (monstersDirectory.DirExists()) {
			config.monstersPath = monstersDirectory;
		}
	}

	if (config.npcsPath.GetFullPath().empty()) {
		FileName npcsDirectory;
		npcsDirectory.AssignDir(dataRoot);
		npcsDirectory.AppendDir("npc");
		if (npcsDirectory.DirExists()) {
			config.npcsPath = npcsDirectory;
		}
	}

	if (config.itemsPath.GetFullPath().empty()) {
		FileName itemsDirectory;
		itemsDirectory.AssignDir(dataRoot);
		itemsDirectory.AppendDir("items");
		if (itemsDirectory.DirExists() && directoryHasItemsFiles(itemsDirectory)) {
			config.itemsPath = itemsDirectory;
		}
	}
}

void applyItemsPath(ClientVersion* clientVersion, const MapServerConfig& config) {
	if (clientVersion == nullptr) {
		return;
	}

	if (config.itemsPath.GetFullPath().empty()) {
		return;
	}

	if (!config.itemsPath.DirExists()) {
		std::cerr << "[live] Warning: ITEMS path does not exist: "
		          << config.itemsPath.GetFullPath().ToStdString() << std::endl;
		return;
	}

	if (!directoryHasItemsFiles(config.itemsPath)) {
		std::cerr << "[live] Warning: ITEMS path is missing items.otb or items.xml: "
		          << config.itemsPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR).ToStdString()
		          << std::endl;
		return;
	}

	clientVersion->setItemsPath(config.itemsPath);
	std::cout << "[live] Using items from "
	          << config.itemsPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR).ToStdString()
	          << std::endl;
}

void applyCreaturePaths(ClientVersion* clientVersion, const MapServerConfig& config) {
	if (clientVersion == nullptr) {
		return;
	}

	if (!config.monstersPath.GetFullPath().empty()) {
		if (config.monstersPath.DirExists()) {
			clientVersion->setMonstersPath(config.monstersPath);
			std::cout << "[live] Using monsters from "
			          << config.monstersPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR).ToStdString()
			          << std::endl;
		} else {
			std::cerr << "[live] Warning: MONSTERS path does not exist: "
			          << config.monstersPath.GetFullPath().ToStdString() << std::endl;
		}
	}

	if (!config.npcsPath.GetFullPath().empty()) {
		if (config.npcsPath.DirExists()) {
			clientVersion->setNpcsPath(config.npcsPath);
			std::cout << "[live] Using NPCs from "
			          << config.npcsPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR).ToStdString()
			          << std::endl;
		} else {
			std::cerr << "[live] Warning: NPCS path does not exist: "
			          << config.npcsPath.GetFullPath().ToStdString() << std::endl;
		}
	}
}

bool configureClientForMap(const MapServerConfig& config) {
	MapVersion version;
	if (!IOMapOTBM::getVersionInfo(config.mapPath, version)) {
		std::cerr << "Could not read client version from map file." << std::endl;
		return false;
	}

	ClientVersion* clientVersion = ClientVersion::get(version.client);
	if (clientVersion == nullptr) {
		std::cerr << "Unsupported map client version." << std::endl;
		return false;
	}

	if (!config.itemsPath.GetFullPath().empty()) {
		clientVersion->clearItemsPath();
	}

	wxArrayString searchDirs;
	const wxString configuredAssetsDir = normalizeConfigDirectory(config.assetsPath.GetFullPath());
	if (!configuredAssetsDir.empty()) {
		searchDirs.Add(configuredAssetsDir);
	} else if (!config.assetsPath.GetFullPath().empty()) {
		std::cerr << "[live] ASSETS path does not exist: " << config.assetsPath.GetFullPath().ToStdString() << std::endl;
	}
	searchDirs.Add(g_gui.GetExecDirectory());

	wxString workingDirectory;
	workingDirectory = wxGetCwd();
	if (!workingDirectory.empty()) {
		workingDirectory = wxGetCwd();
		if (!workingDirectory.EndsWith(FileName::GetPathSeparator())) {
			workingDirectory << FileName::GetPathSeparator();
		}
		searchDirs.Add(workingDirectory);
	}

	for (size_t i = 0; i < searchDirs.size(); ++i) {
		const wxString candidate = searchDirs[i];
		bool duplicate = false;
		for (size_t j = 0; j < i; ++j) {
			if (searchDirs[j] == candidate) {
				duplicate = true;
				break;
			}
		}
		if (duplicate) {
			continue;
		}

		if (clientVersion->tryConfigureFromDirectory(FileName(candidate))) {
			std::cout << "[live] Using client assets from " << candidate.ToStdString() << std::endl;
			applyItemsPath(clientVersion, config);
			applyCreaturePaths(clientVersion, config);
			ClientVersion::saveVersions();
			return true;
		}
	}

	std::cerr << "Could not locate Tibia.dat/Tibia.spr for map client version "
	          << clientVersion->getName() << "." << std::endl;
	std::cerr << "Set ASSETS in mapserver.cfg or pass --assets <directory>." << std::endl;
	if (!config.itemsPath.GetFullPath().empty() && !directoryHasItemsFiles(config.itemsPath)) {
		std::cerr << "Set ITEMS in mapserver.cfg or pass --items <directory> for items.otb/items.xml." << std::endl;
	}
	std::cerr << "Searched:" << std::endl;
	for (const wxString& candidate : searchDirs) {
		std::cerr << "  - " << candidate.ToStdString() << std::endl;
	}
	return false;
}

// Resolves the editor build the server hands to outdated clients. Uses the
// configured UPDATE_FILES list, or defaults to Editor_x64.exe next to the
// MapServer executable when present.
void configureUpdatePackage(const MapServerConfig& config) {
	if (!g_server) {
		return;
	}

	std::vector<wxString> paths = config.updateFiles;
	if (paths.empty() && !config.updateFilesConfigured) {
		FileName execPath;
		try {
			execPath = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
		} catch (const std::bad_cast&) {
			execPath = FileName(".");
		}
		FileName defaultEditor(execPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), "Editor_x64.exe");
		if (defaultEditor.FileExists()) {
			paths.push_back(defaultEditor.GetFullPath());
		}
	}

	if (paths.empty()) {
		std::cout << "[live] Editor auto-update disabled (no UPDATE_FILES configured)." << std::endl;
		return;
	}

	std::vector<LiveUpdateFile> files;
	wxString error;
	if (!collectUpdateFiles(paths, files, error)) {
		std::cerr << "[live] Auto-update disabled: " << error.ToStdString() << std::endl;
		return;
	}

	g_server->setUpdateFiles(files);
	std::cout << "[live] Editor auto-update enabled; serving " << files.size()
	          << " file(s) to outdated clients:" << std::endl;
	for (const LiveUpdateFile& file : files) {
		std::cout << "  - " << file.filename << " (" << file.size << " bytes)" << std::endl;
	}
}

void processCommand(const std::string& line) {
	std::istringstream iss(line);
	std::string cmd;
	iss >> cmd;
	if (cmd.empty()) {
		return;
	}

	if (cmd == "exit" || cmd == "quit") {
		g_running = false;
		if (g_server) {
			g_server->close();
		}
	} else if (cmd == "save") {
		saveHostedMap(false);
	} else if (cmd == "list") {
		if (!g_server) {
			return;
		}
		const auto& clients = g_server->getClients();
		if (clients.empty()) {
			std::cout << "[live] No clients connected." << std::endl;
			return;
		}
		std::cout << "[live] Connected clients:" << std::endl;
		for (const auto& entry : clients) {
			LivePeer* peer = entry.second;
			std::cout << "  - " << peer->getName().ToStdString()
			          << " (" << peer->getHostName() << ")" << std::endl;
		}
	} else if (cmd == "kick") {
		std::string name;
		iss >> name;
		if (name.empty()) {
			std::cout << "[live] Usage: kick <name>" << std::endl;
			return;
		}
		if (g_server) {
			g_server->kickClient(wxstr(name));
		}
	} else if (cmd == "block") {
		std::string spec;
		iss >> spec;
		if (spec.empty()) {
			std::cout << "[live] Usage: block <id> or block <from>-<to>" << std::endl;
			return;
		}
		if (!g_server) {
			return;
		}
		wxString feedback;
		if (g_server->blockItems(spec, feedback)) {
			std::cout << "[live] " << feedback.ToStdString() << std::endl;
		} else {
			std::cout << "[live] " << feedback.ToStdString() << std::endl;
		}
	} else {
		std::cout << "[live] Unknown command. Try: save, list, kick <name>, block <id|from-to>, exit" << std::endl;
	}
}

void drainPendingCommands() {
	std::vector<std::string> commands;
	{
		std::lock_guard<std::mutex> lock(g_commandMutex);
		commands.swap(g_pendingCommands);
	}
	for (const std::string& command : commands) {
		processCommand(command);
	}
}

} // namespace

class MapServerApp : public wxAppConsole {
public:
	bool OnInit() override {
#if defined(__WXGTK__)
		// Headless servers have no display; glutInit would abort. The server
		// never renders, so glut is only initialized when a display exists.
		if (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY")) {
			int glutArgc = 1;
			char* glutArgv[1] = { const_cast<char*>("MapServer") };
			glutInit(&glutArgc, glutArgv);
		}
#elif defined(__WINDOWS__)
		int glutArgc = 1;
		char* glutArgv[1] = { const_cast<char*>("MapServer") };
		glutInit(&glutArgc, glutArgv);
#endif

		mt_seed(time(nullptr));
		srand(time(nullptr));

		g_gui.SetHeadless(true);
		g_gui.discoverDataDirectory("clients.xml");
		g_settings.load();
		ClientVersion::loadVersions();
		g_settings.setInteger(Config::ALWAYS_MAKE_BACKUP, 1);

		const auto startupFailed = [this]() {
			waitForEnterOnStartupFailure(argc);
			return false;
		};

		MapServerConfig config;
		const bool configFileExists = wxFileName(getMapServerConfigPath()).FileExists();
		if (!loadMapServerConfig(config)) {
			return startupFailed();
		}
		if (!configFileExists) {
			std::cout << "[live] No mapserver.cfg found. Copy mapserver.cfg.example to mapserver.cfg and edit it." << std::endl;
			std::cout << "[live] Expected path: " << getMapServerConfigPath().ToStdString() << std::endl;
		}
		if (!parseArgs(argc, argv, config)) {
			return startupFailed();
		}

		g_autosaveIntervalSeconds = config.autosaveMinutes * 60;

		resolveDefaultServerDataPaths(config);

		if (!configureClientForMap(config)) {
			return startupFailed();
		}

		try {
			g_editor = newd Editor(g_gui.copybuffer, config.mapPath);
		} catch (const std::runtime_error& e) {
			std::cerr << "Failed to load map: " << e.what() << std::endl;
			return startupFailed();
		} catch (const std::string& e) {
			std::cerr << "Failed to load map: " << e << std::endl;
			return startupFailed();
		}

		if (!config.sessionName.empty()) {
			g_editor->getMap().setName(nstr(config.sessionName));
		}

		Map& map = g_editor->getMap();
		if (!IOMapOTBM::loadAuxiliaryHouses(map, config.mapPath)) {
			std::cerr << "[live] Warning: no house data loaded." << std::endl;
		}
		if (countMapCreatures(map) == 0) {
			if (!IOMapOTBM::loadAuxiliarySpawns(map, config.mapPath)) {
				std::cerr << "[live] Warning: no creatures loaded from spawn file." << std::endl;
			}
		}
		if (!IOMapOTBM::loadAuxiliaryComments(map, config.mapPath)) {
			std::cerr << "[live] Warning: no map comments loaded." << std::endl;
		}
		logMapLoadSummary(map);

		g_server = g_editor->StartLiveServer();
		g_server->setPort(config.port);
		g_server->setPassword(config.password);
		if (config.sessionBounds.enabled) {
			g_server->setSessionBounds(config.sessionBounds);
		}
		configureUpdatePackage(config);

		if (!g_server->bind()) {
			std::cerr << g_server->getLastError().ToStdString() << std::endl;
			return startupFailed();
		}

		std::cout << "[live] MapServer version " << __RME_VERSION__
		          << " (live protocol " << __LIVE_NET_VERSION__ << ")" << std::endl;
		std::cout << "[live] Hosting '" << g_editor->getMap().getName()
		          << "' on port " << config.port << std::endl;
		if (config.sessionBounds.enabled) {
			std::cout << "[live] Session area: center ("
			          << config.sessionBounds.centerX << ","
			          << config.sessionBounds.centerY << ","
			          << static_cast<unsigned>(config.sessionBounds.centerZ) << ") radius "
			          << config.sessionBounds.radius << std::endl;
		}
		if (g_autosaveIntervalSeconds > 0) {
			std::cout << "[live] Auto-save every " << (g_autosaveIntervalSeconds / 60)
			          << " minute(s) with timestamped backups in the map's backup/ folder."
			          << std::endl;
			std::cout << "[live] When 10+ backups accumulate, the oldest 10 are zipped together."
			          << std::endl;
		} else {
			std::cout << "[live] Auto-save disabled." << std::endl;
		}
		std::cout << "[live] Type 'save', 'list', 'kick <name>', 'block <id|from-to>', or 'exit'." << std::endl;
		g_lastAutosave = std::chrono::steady_clock::now();
		return true;
	}

	int OnRun() override {
		const bool interactiveInput = isStdinInteractive();
		std::thread inputThread([interactiveInput]() {
			std::string line;
			while (g_running && std::getline(std::cin, line)) {
				{
					std::lock_guard<std::mutex> lock(g_commandMutex);
					g_pendingCommands.push_back(line);
				}
			}
			// EOF on a non-interactive stdin (service/daemon, redirected
			// input) must not stop the server; only an interactive EOF
			// counts as a quit request.
			if (interactiveInput) {
				g_running = false;
			}
		});

		while (g_running) {
			while (Pending()) {
				Dispatch();
			}
			drainPendingCommands();
			tickAutosave();
			wxMilliSleep(10);
		}

		if (inputThread.joinable()) {
			inputThread.join();
		}

		if (g_editor && g_editor->getMap().hasChanged()) {
			std::cout << "[live] Saving pending changes before shutdown..." << std::endl;
			saveHostedMap(true);
		}

		NetworkConnection::getInstance().stop();

		if (g_editor) {
			g_editor->CloseLiveServer();
			delete g_editor;
			g_editor = nullptr;
			g_server = nullptr;
		}

		return 0;
	}
};

wxIMPLEMENT_APP_CONSOLE(MapServerApp);
