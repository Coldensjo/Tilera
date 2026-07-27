//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "map_view_state.h"
#include "editor.h"
#include "live_client.h"
#include "settings.h"

#include <sstream>
#include <vector>

namespace {
constexpr size_t MAX_STORED_MAP_VIEW_POSITIONS = 50;
constexpr char RECORD_SEPARATOR = '\n';
constexpr char FIELD_SEPARATOR = '\t';

std::string normalizeMapPath(const std::string& filename) {
	if (filename.empty()) {
		return "";
	}

	wxFileName file(wxstr(filename));
	file.Normalize(RME_PATH_NORM_FLAGS);
	return nstr(file.GetFullPath());
}

using MapViewEntry = std::pair<std::string, Position>;

std::vector<MapViewEntry> parseMapViewEntries(const std::string& data) {
	std::vector<MapViewEntry> entries;
	std::istringstream stream(data);
	std::string line;
	while (std::getline(stream, line)) {
		if (line.empty()) {
			continue;
		}

		const size_t separator = line.find(FIELD_SEPARATOR);
		if (separator == std::string::npos) {
			continue;
		}

		const std::string key = line.substr(0, separator);
		std::istringstream positionStream(line.substr(separator + 1));
		Position position;
		positionStream >> position;
		if (!positionStream || key.empty() || !position.isValid()) {
			continue;
		}

		entries.emplace_back(key, position);
	}
	return entries;
}

std::string serializeMapViewEntries(const std::vector<MapViewEntry>& entries) {
	std::ostringstream stream;
	for (size_t index = 0; index < entries.size(); ++index) {
		if (index > 0) {
			stream << RECORD_SEPARATOR;
		}
		stream << entries[index].first << FIELD_SEPARATOR << entries[index].second;
	}
	return stream.str();
}

void writeMapViewEntries(const std::vector<MapViewEntry>& entries) {
	g_settings.setString(Config::MAP_VIEW_POSITIONS, serializeMapViewEntries(entries));
}

std::vector<MapViewEntry> loadMapViewEntries() {
	return parseMapViewEntries(g_settings.getString(Config::MAP_VIEW_POSITIONS));
}

bool loadLegacyMapViewPosition(const std::string& key, Position& position) {
	const std::string legacyPath = normalizeMapPath(g_settings.getString(Config::RECENT_EDITED_MAP_PATH));
	if (legacyPath.empty() || legacyPath != key) {
		return false;
	}

	std::istringstream stream(g_settings.getString(Config::RECENT_EDITED_MAP_POSITION));
	stream >> position;
	return stream && position.isValid();
}
} // namespace

std::string makeLocalMapViewKey(const std::string& filename) {
	return normalizeMapPath(filename);
}

std::string makeLiveMapViewKey(const std::string& host, uint16_t port) {
	return "live://" + host + ":" + std::to_string(port);
}

std::string getMapViewKey(const Editor& editor) {
	if (editor.IsLiveClient() && editor.live_client) {
		return makeLiveMapViewKey(
			editor.live_client->getConnectionAddress(),
			editor.live_client->getConnectionPort()
		);
	}

	if (editor.map.hasFile()) {
		return makeLocalMapViewKey(editor.map.getFilename());
	}

	return "";
}

void saveMapViewPosition(const std::string& key, const Position& position) {
	if (key.empty() || !position.isValid()) {
		return;
	}

	std::vector<MapViewEntry> entries = loadMapViewEntries();
	for (const MapViewEntry& entry : entries) {
		if (entry.first == key && entry.second == position) {
			return;
		}
	}

	for (auto it = entries.begin(); it != entries.end(); ++it) {
		if (it->first == key) {
			entries.erase(it);
			break;
		}
	}

	entries.emplace_back(key, position);
	while (entries.size() > MAX_STORED_MAP_VIEW_POSITIONS) {
		entries.erase(entries.begin());
	}

	writeMapViewEntries(entries);

	// Keep legacy keys in sync for older builds.
	g_settings.setString(Config::RECENT_EDITED_MAP_PATH, key);
	std::ostringstream stream;
	stream << position;
	g_settings.setString(Config::RECENT_EDITED_MAP_POSITION, stream.str());
	g_settings.save();
}

bool loadMapViewPosition(const std::string& key, Position& position) {
	if (key.empty()) {
		return false;
	}

	for (const MapViewEntry& entry : loadMapViewEntries()) {
		if (entry.first == key) {
			position = entry.second;
			return position.isValid();
		}
	}

	return loadLegacyMapViewPosition(key, position);
}
