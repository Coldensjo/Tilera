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

#include <algorithm>
#include <set>
#include <map>
#include <deque>
#include <limits>
#include <functional>
#include <fstream>

#include "editor.h"
#include "materials.h"
#include "map.h"
#include "complexitem.h"
#include "settings.h"
#include "gui.h"
#include "map_display.h"
#include "live_map_backup.h"
#include "brush.h"
#include "ground_brush.h"
#include "wall_brush.h"
#include "table_brush.h"
#include "carpet_brush.h"
#include "house_exit_brush.h"
#include "doodad_brush.h"
#include "creature_brush.h"
#include "spawn_brush.h"

#include "live_server.h"
#include "live_client.h"
#include "live_action.h"
#include "live_socket.h"


Editor::Editor(CopyBuffer& copybuffer) :
	actionQueue(newd ActionQueue(*this)),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr),
	live_server(nullptr),
	live_client(nullptr) {
	wxString error;
	wxArrayString warnings;
	bool ok = true;

	ClientVersionID defaultVersion = ClientVersionID(g_settings.getInteger(Config::DEFAULT_CLIENT_VERSION));
	if (defaultVersion == CLIENT_VERSION_NONE) {
		defaultVersion = ClientVersion::getLatestVersion()->getID();
	}

	if (g_gui.GetCurrentVersionID() != defaultVersion) {
		if (g_gui.CloseAllEditors()) {
			ok = g_gui.LoadVersion(defaultVersion, error, warnings);
			g_gui.PopupDialog("Error", error, wxOK);
			g_gui.ListDialog("Warnings", warnings);
		} else {
			throw std::runtime_error("All maps of different versions were not closed.");
		}
	}

	if (!ok) {
		throw std::runtime_error("Couldn't load client version");
	}

	MapVersion version;
	version.otbm = g_gui.GetCurrentVersion().getPrefferedMapVersionID();
	version.client = g_gui.GetCurrentVersionID();
	map.convert(version);

	map.height = 2048;
	map.width = 2048;

	static int unnamed_counter = 0;

	std::string sname = "Untitled-" + i2s(++unnamed_counter);
	map.name = sname + ".otbm";
	map.spawnfile = sname + "-spawn.toml";
	map.housefile = sname + "-house.toml";
	map.description = "No map description available.";
	map.unnamed = true;

	map.doChange();
}

Editor::Editor(CopyBuffer& copybuffer, const FileName& fn, ClientVersionID forcedClientVersion, bool forceReloadVersion) :
	actionQueue(newd ActionQueue(*this)),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr),
	live_server(nullptr),
	live_client(nullptr) {
	MapVersion ver;
	if (!IOMapOTBM::getVersionInfo(fn, ver)) {
		// g_gui.PopupDialog("Error", "Could not open file \"" + fn.GetFullPath() + "\".", wxOK);
		throw std::runtime_error("Could not open file \"" + nstr(fn.GetFullPath()) + "\".\nThis is not a valid OTBM file or it does not exist.");
	}

	/*
	if(ver < CLIENT_VERSION_760) {
		long b = g_gui.PopupDialog("Error", "Unsupported Client Version (pre 7.6), do you want to try to load the map anyways?", wxYES | wxNO);
		if(b == wxID_NO) {
			valid_state = false;
			return;
		}
	}
	*/

	bool success = true;
	ClientVersionID versionToLoad = forcedClientVersion != CLIENT_VERSION_NONE ? forcedClientVersion : ver.client;
	if (g_gui.GetCurrentVersionID() != versionToLoad || forceReloadVersion) {
		wxString error;
		wxArrayString warnings;
		if (g_gui.CloseAllEditors()) {
			success = g_gui.LoadVersion(versionToLoad, error, warnings, forceReloadVersion);
			if (!success) {
				if (g_gui.IsHeadless()) {
					throw std::runtime_error(nstr(error.empty() ? wxString("Couldn't load client version.") : error));
				}
				g_gui.PopupDialog("Error", error, wxOK);
			} else if (!g_gui.IsHeadless()) {
				g_gui.ListDialog("Warnings", warnings);
			}
		} else {
			throw std::runtime_error("All maps of different versions were not closed.");
		}
	}

	if (success) {
		ScopedLoadingBar LoadingBar("Loading OTBM map...");
		success = map.open(nstr(fn.GetFullPath()));
		/* TODO
		if(success && ver.client == CLIENT_VERSION_854_BAD) {
			int ok = g_gui.PopupDialog("Incorrect OTB", "This map has been saved with an incorrect OTB version, do you want to convert it to the new OTB version?\n\nIf you are not sure, click Yes.", wxYES | wxNO);

			if(ok == wxID_YES){
				ver.client = CLIENT_VERSION_854;
				map.convert(ver);
			}
		}
		*/
	}

	if (!success) {
		throw std::runtime_error("Failed to load map.");
	}
}

Editor::Editor(CopyBuffer& copybuffer, LiveClient* client) :
	actionQueue(newd NetworkedActionQueue(*this)),
	selection(*this),
	copybuffer(copybuffer),
	replace_brush(nullptr),
	live_server(nullptr),
	live_client(client) {
	// The map's contents are streamed in from the server on demand; the
	// dimensions and name are filled in once the server hello arrives.
	MapVersion version;
	version.otbm = g_gui.GetCurrentVersion().getPrefferedMapVersionID();
	version.client = g_gui.GetCurrentVersionID();
	map.convert(version);

	map.width = 2048;
	map.height = 2048;
}

Editor::~Editor() {
	// The live server (standalone host) is owned by this editor. The live
	// client, on the other hand, is owned and cleaned up by
	// GUI::CloseLiveEditors, so it must not be deleted here.
	if (live_server) {
		live_server->close();
		delete live_server;
		live_server = nullptr;
	}
	live_client = nullptr;

	UnnamedRenderingLock();
	selection.clear();
	delete actionQueue;
}

LiveSocket& Editor::GetLive() const {
	if (live_server) {
		return static_cast<LiveSocket&>(*live_server);
	}
	return static_cast<LiveSocket&>(*live_client);
}

bool Editor::IsClipboardAllowed() const {
	return !IsLiveClient() || g_settings.getBoolean(Config::LIVE_ALLOW_CLIPBOARD);
}

void Editor::protectItemProperties(Item* item) {
	if (item) {
		property_locked_items.push_back(item);
	}
}

void Editor::releaseItemProperties(Item* item) {
	auto it = std::find(property_locked_items.begin(), property_locked_items.end(), item);
	if (it != property_locked_items.end()) {
		property_locked_items.erase(it);
	}
}

bool Editor::isItemProtectedByProperties(const Item* item) const {
	return item && std::find(property_locked_items.begin(), property_locked_items.end(), item) != property_locked_items.end();
}

LiveServer* Editor::StartLiveServer() {
	ASSERT(IsLocal());
	live_server = newd LiveServer(*this);
	live_server->loadBlockedItems();

	delete actionQueue;
	actionQueue = newd NetworkedActionQueue(*this);

	return live_server;
}

void Editor::BroadcastNodes(DirtyList& dirty_list) {
	if (IsLiveClient()) {
		live_client->sendChanges(dirty_list);
	} else if (IsLiveServer()) {
		live_server->broadcastNodes(dirty_list);
	}
}

void Editor::CloseLiveServer() {
	if (IsLiveServer()) {
		live_server->close();

		delete live_server;
		live_server = nullptr;

		delete actionQueue;
		actionQueue = newd ActionQueue(*this);
	}

	// The live client is detached here; ownership stays with GUI::CloseLiveEditors.
	live_client = nullptr;
}

Action* Editor::createAction(ActionIdentifier type) {
	return actionQueue->createAction(type);
}

BatchAction* Editor::createBatch(ActionIdentifier type) {
	return actionQueue->createBatch(type);
}

void Editor::addBatch(BatchAction* action, int stacking_delay) {
	actionQueue->addBatch(action, stacking_delay);
	g_gui.UpdateMenus();
}

void Editor::addAction(Action* action, int stacking_delay) {
	actionQueue->addAction(action, stacking_delay);
	g_gui.UpdateMenus();
}

bool Editor::saveMap(FileName filename, bool showdialog) {
	std::string savefile = filename.GetFullPath().mb_str(wxConvUTF8).data();
	bool save_as = false;
	bool save_otgz = false;

	if (savefile.empty()) {
		savefile = map.filename;

		FileName c1(wxstr(savefile));
		FileName c2(wxstr(map.filename));
		save_as = c1 != c2;
	}

	// If not named yet, propagate the file name to the auxilliary files
	if (map.unnamed) {
		FileName _name(filename);
		_name.SetExt("xml");

		_name.SetName(filename.GetName() + "-spawn");
		map.spawnfile = nstr(_name.GetFullName());
		_name.SetName(filename.GetName() + "-house");
		map.housefile = nstr(_name.GetFullName());
		_name.SetName(filename.GetName() + "-comments");
		map.commentsfile = nstr(_name.GetFullName());

		map.unnamed = false;
	}

	// File object to convert between local paths etc.
	FileName converter;
	converter.Assign(wxstr(savefile));
	std::string map_path = nstr(converter.GetPath(wxPATH_GET_SEPARATOR | wxPATH_GET_VOLUME));

	// Make temporary backups
	// converter.Assign(wxstr(savefile));
	std::string backup_otbm, backup_house, backup_spawn, backup_comments;

	if (converter.GetExt() == "otgz") {
		save_otgz = true;
		if (converter.FileExists()) {
			backup_otbm = map_path + nstr(converter.GetName()) + ".otgz~";
			std::remove(backup_otbm.c_str());
			std::rename(savefile.c_str(), backup_otbm.c_str());
		}
	} else {
		if (converter.FileExists()) {
			backup_otbm = map_path + nstr(converter.GetName()) + ".otbm~";
			std::remove(backup_otbm.c_str());
			std::rename(savefile.c_str(), backup_otbm.c_str());
		}

		converter.SetFullName(wxstr(map.housefile));
		if (!map.housefile.empty() && converter.FileExists()) {
			const std::string house_path = map_path + map.housefile;
			backup_house = house_path + "~";
			std::remove(backup_house.c_str());
			std::rename(house_path.c_str(), backup_house.c_str());
		}

		converter.SetFullName(wxstr(map.spawnfile));
		if (!map.spawnfile.empty() && converter.FileExists()) {
			const std::string spawn_path = map_path + map.spawnfile;
			backup_spawn = spawn_path + "~";
			std::remove(backup_spawn.c_str());
			std::rename(spawn_path.c_str(), backup_spawn.c_str());
		}

		converter.SetFullName(wxstr(map.commentsfile));
		if (!map.commentsfile.empty() && converter.FileExists()) {
			const std::string comments_path = map_path + map.commentsfile;
			backup_comments = comments_path + "~";
			std::remove(backup_comments.c_str());
			std::rename(comments_path.c_str(), backup_comments.c_str());
		}
	}

	// Save the map
	{
		std::string n = nstr(g_gui.GetLocalDataDirectory()) + ".saving.txt";
		std::ofstream f(n.c_str(), std::ios::trunc | std::ios::out);
		f << backup_otbm << std::endl
		  << backup_house << std::endl
		  << backup_spawn << std::endl
		  << backup_comments << std::endl;
	}

	{

		// Set up the Map paths
		wxFileName fn = wxstr(savefile);
		map.filename = fn.GetFullPath().mb_str(wxConvUTF8);
		map.name = fn.GetFullName().mb_str(wxConvUTF8);

		if (showdialog) {
			g_gui.CreateLoadBar("Saving OTBM map...");
		}

		// Perform the actual save
		IOMapOTBM mapsaver(map.getVersion());
		bool success = mapsaver.saveMap(map, fn);

		if (showdialog) {
			g_gui.DestroyLoadBar();
		}

		// Check for errors...
		if (!success) {
			// Rename the temporary backup files back to their previous names
			if (!backup_otbm.empty()) {
				converter.SetFullName(wxstr(savefile));
				std::string otbm_filename = map_path + nstr(converter.GetName());
				std::rename(backup_otbm.c_str(), std::string(otbm_filename + (save_otgz ? ".otgz" : ".otbm")).c_str());
			}

			if (!backup_house.empty()) {
				std::rename(backup_house.c_str(), (map_path + map.housefile).c_str());
			}

			if (!backup_spawn.empty()) {
				std::rename(backup_spawn.c_str(), (map_path + map.spawnfile).c_str());
			}

			if (!backup_comments.empty()) {
				std::rename(backup_comments.c_str(), (map_path + map.commentsfile).c_str());
			}

			wxString errorMessage = "Could not save, unable to open target for writing.";
			if (!mapsaver.getError().empty()) {
				errorMessage = mapsaver.getError();
			}
			g_gui.PopupDialog("Error", errorMessage, wxOK);
		}

		// Remove temporary save runfile
		{
			std::string n = nstr(g_gui.GetLocalDataDirectory()) + ".saving.txt";
			std::remove(n.c_str());
		}

		// If failure, don't run the rest of the function
		if (!success) {
			return false;
		}
	}

	// Move to permanent backup
	if (!save_as && g_settings.getInteger(Config::ALWAYS_MAKE_BACKUP)) {
		// Move temporary backups to their proper files
		time_t t = time(nullptr);
		tm* current_time = localtime(&t);
		ASSERT(current_time);

		std::ostringstream date;
		date << (1900 + current_time->tm_year);
		if (current_time->tm_mon < 9) {
			date << "-" << "0" << current_time->tm_mon + 1;
		} else {
			date << "-" << current_time->tm_mon + 1;
		}
		date << "-" << current_time->tm_mday;
		date << "-" << current_time->tm_hour;
		date << "-" << current_time->tm_min;
		date << "-" << current_time->tm_sec;

		const bool useLiveBackupFolder = g_gui.IsHeadless();
		std::string backup_path = map_path;
		if (useLiveBackupFolder) {
			backup_path = getLiveMapBackupDirectory(map_path);
			ensureLiveMapBackupDirectory(backup_path);
		}

		if (!backup_otbm.empty()) {
			converter.SetFullName(wxstr(savefile));
			std::string otbm_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_otbm.c_str(), std::string(otbm_filename + "." + date.str() + (save_otgz ? ".otgz" : ".otbm")).c_str());
		}

		if (!backup_house.empty()) {
			converter.SetFullName(wxstr(map.housefile));
			const std::string house_ext = nstr(converter.GetExt());
			std::string house_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_house.c_str(), std::string(house_filename + "." + date.str() + "." + house_ext).c_str());
		}

		if (!backup_spawn.empty()) {
			converter.SetFullName(wxstr(map.spawnfile));
			const std::string spawn_ext = nstr(converter.GetExt());
			std::string spawn_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_spawn.c_str(), std::string(spawn_filename + "." + date.str() + "." + spawn_ext).c_str());
		}

		if (!backup_comments.empty()) {
			converter.SetFullName(wxstr(map.commentsfile));
			const std::string comments_ext = nstr(converter.GetExt());
			std::string comments_filename = backup_path + nstr(converter.GetName());
			std::rename(backup_comments.c_str(), std::string(comments_filename + "." + date.str() + "." + comments_ext).c_str());
		}

		if (useLiveBackupFolder) {
			rotateLiveMapBackups(backup_path);
		}
	} else {
		// Delete the temporary files
		std::remove(backup_otbm.c_str());
		std::remove(backup_house.c_str());
		std::remove(backup_spawn.c_str());
		std::remove(backup_comments.c_str());
	}

	map.clearChanges();

	// Generate actionIds and uniqueIds .lua file
	{
		std::set<uint16_t> actionIds;
		std::set<uint16_t> uniqueIds;

		// Helper function to collect IDs from an item (recursively for containers)
		std::function<void(Item*)> collectItemIds = [&](Item* item) {
			if (!item) {
				return;
			}

			uint16_t actionId = item->getActionID();
			if (actionId > 0) {
				actionIds.insert(actionId);
			}

			uint16_t uniqueId = item->getUniqueID();
			if (uniqueId > 0) {
				uniqueIds.insert(uniqueId);
			}

			// Check if item is a container and recursively process its contents
			Container* container = dynamic_cast<Container*>(item);
			if (container) {
				ItemVector& contents = container->getVector();
				for (Item* containedItem : contents) {
					collectItemIds(containedItem);
				}
			}
		};

		// Iterate through all tiles in the map
		for (MapIterator miter = map.begin(); miter != map.end(); ++miter) {
			Tile* tile = (*miter)->get();
			if (!tile) {
				continue;
			}

			// Check ground item
			if (tile->ground) {
				collectItemIds(tile->ground);
			}

			// Check all items on the tile
			for (Item* item : tile->items) {
				collectItemIds(item);
			}
		}

		// Generate .lua file path (same directory as map, same name with .lua extension)
		FileName luaFile;
		luaFile.Assign(wxstr(savefile));
		luaFile.SetExt("lua");

		// Write the .lua file
		std::ofstream luaStream(nstr(luaFile.GetFullPath()), std::ios::trunc | std::ios::out);
		if (luaStream.is_open()) {
			luaStream << "actionIds = {\n";
			bool first = true;
			for (uint16_t aid : actionIds) {
				if (!first) {
					luaStream << ",\n";
				}
				luaStream << "\t" << aid;
				first = false;
			}
			if (!actionIds.empty()) {
				luaStream << "\n";
			}
			luaStream << "}\n\n";

			luaStream << "uniqueIds = {\n";
			first = true;
			for (uint16_t uid : uniqueIds) {
				if (!first) {
					luaStream << ",\n";
				}
				luaStream << "\t" << uid;
				first = false;
			}
			if (!uniqueIds.empty()) {
				luaStream << "\n";
			}
			luaStream << "}\n";
			luaStream.close();
		}
	}
	return true;
}

std::string Editor::getMapCommentAuthor() const {
	if (IsLiveClient() && live_client) {
		return nstr(live_client->getName());
	}
	if (IsLiveServer()) {
		return "Host";
	}
	return g_settings.getString(Config::LIVE_USERNAME);
}

void Editor::addMapComment(const Position& pos, const std::string& text) {
	if (text.empty()) {
		return;
	}

	if (IsLiveClient()) {
		live_client->sendCommentAdd(pos, text);
		return;
	}

	MapComment comment = map.comments.createComment(pos, getMapCommentAuthor(), text);
	map.doChange();
	if (IsLiveServer()) {
		live_server->broadcastComment(comment);
	}
	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
		g_gui.RefreshCommentsWindow();
	}
}

void Editor::sendLivePing(const Position& pos) {
	if (!IsLive()) {
		return;
	}

	if (IsLiveClient()) {
		live_client->sendPing(pos);
	} else if (IsLiveServer()) {
		live_server->sendPing(pos);
	}
}

void Editor::setLiveCursorColor(const wxColor& color) {
	if (!IsLiveClient() || !live_client) {
		return;
	}

	live_client->setCursorColor(color);
	live_client->sendCursorColor();

	g_settings.setInteger(Config::LIVE_CURSOR_RED, color.Red());
	g_settings.setInteger(Config::LIVE_CURSOR_GREEN, color.Green());
	g_settings.setInteger(Config::LIVE_CURSOR_BLUE, color.Blue());
	g_settings.setInteger(Config::LIVE_CURSOR_ALPHA, color.Alpha());

	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
		g_gui.UpdateMinimap();
	}
}

wxColor Editor::getLiveCursorColor() const {
	if (IsLiveClient() && live_client) {
		return live_client->getOwnCursorColor();
	}

	return wxColor(
		g_settings.getInteger(Config::LIVE_CURSOR_RED),
		g_settings.getInteger(Config::LIVE_CURSOR_GREEN),
		g_settings.getInteger(Config::LIVE_CURSOR_BLUE),
		g_settings.getInteger(Config::LIVE_CURSOR_ALPHA)
	);
}

bool Editor::promptAddMapComment(const Position& pos) {
	wxTextEntryDialog dlg(g_gui.root, "Leave a note for other mappers:", "Add Map Comment");
	if (dlg.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString text = dlg.GetValue().Trim(true).Trim(false);
	if (text.empty()) {
		return false;
	}

	addMapComment(pos, nstr(text));
	return true;
}

bool Editor::editMapComment(uint32_t commentId, const std::string& newText) {
	if (newText.empty()) {
		return false;
	}

	const MapComment* existing = map.comments.findById(commentId);
	if (!existing) {
		return false;
	}

	MapComment updated = *existing;
	updated.text = newText;

	if (IsLiveClient()) {
		map.comments.upsert(updated);
		live_client->sendCommentEdit(commentId, newText);
		if (!g_gui.IsHeadless()) {
			g_gui.RefreshView();
			g_gui.RefreshCommentsWindow();
		}
		return true;
	}

	map.comments.upsert(updated);
	map.doChange();
	if (IsLiveServer()) {
		live_server->broadcastComment(updated);
	}
	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
		g_gui.RefreshCommentsWindow();
	}
	return true;
}

bool Editor::promptEditMapComment(uint32_t commentId) {
	const MapComment* existing = map.comments.findById(commentId);
	if (!existing) {
		return false;
	}

	wxTextEntryDialog dlg(g_gui.root, "Edit comment:", "Edit Map Comment", wxstr(existing->text), wxOK | wxCANCEL | wxTE_MULTILINE);
	if (dlg.ShowModal() != wxID_OK) {
		return false;
	}

	const wxString text = dlg.GetValue().Trim(true).Trim(false);
	if (text.empty()) {
		return false;
	}

	return editMapComment(commentId, nstr(text));
}

bool Editor::removeMapComment(uint32_t commentId) {
	if (IsLiveClient()) {
		if (!map.comments.removeById(commentId)) {
			return false;
		}
		live_client->sendCommentRemove(commentId);
		if (!g_gui.IsHeadless()) {
			g_gui.RefreshView();
			g_gui.RefreshCommentsWindow();
		}
		return true;
	}

	if (!map.comments.removeById(commentId)) {
		return false;
	}

	map.doChange();
	if (IsLiveServer()) {
		live_server->broadcastCommentRemoved(commentId);
	}
	if (!g_gui.IsHeadless()) {
		g_gui.RefreshView();
		g_gui.RefreshCommentsWindow();
	}
	return true;
}

bool Editor::removeMapCommentAt(const Position& pos) {
	const std::vector<const MapComment*> tileComments = map.comments.atPosition(pos.x, pos.y, pos.z);
	if (tileComments.empty()) {
		return false;
	}

	uint32_t commentId = tileComments.front()->id;
	if (tileComments.size() > 1) {
		wxArrayString choices;
		for (const MapComment* comment : tileComments) {
			wxString label = wxstr(comment->author) + " (" + formatMapCommentTime(comment->createdUnix) + ")";
			wxString preview = wxstr(comment->text);
			preview.Replace("\n", " ");
			if (preview.length() > 48) {
				preview = preview.substr(0, 48) + "...";
			}
			label << ": " << preview;
			choices.Add(label);
		}

		wxSingleChoiceDialog dlg(
			g_gui.root,
			"Select the comment to remove:",
			"Remove Map Comment",
			choices
		);
		if (dlg.ShowModal() != wxID_OK) {
			return false;
		}
		commentId = tileComments[dlg.GetSelection()]->id;
	} else if (g_gui.PopupDialog(
		           "Remove Map Comment",
		           wxString::Format(
			           "Remove comment by %s?\n\n%s",
			           wxstr(tileComments.front()->author),
			           wxstr(tileComments.front()->text)
		           ),
		           wxYES | wxNO
		       )
	           != wxID_YES) {
		return false;
	}

	return removeMapComment(commentId);
}

bool Editor::showMapCommentsAt(const Position& pos) {
	const std::vector<const MapComment*> tileComments = map.comments.atPosition(pos.x, pos.y, pos.z);
	if (tileComments.empty()) {
		return false;
	}

	wxString body;
	for (size_t i = 0; i < tileComments.size(); ++i) {
		const MapComment* comment = tileComments[i];
		if (i > 0) {
			body << "\n\n--------------------\n\n";
		}
		body << wxstr(comment->author) << "  (" << formatMapCommentTime(comment->createdUnix) << ")\n\n";
		body << wxstr(comment->text);
	}

	g_gui.PopupDialog("Map Comment", body, wxOK);
	return true;
}

bool Editor::importMiniMap(FileName filename, int import, int import_x_offset, int import_y_offset, int import_z_offset) {
	return false;
}

bool Editor::exportMiniMap(FileName filename, int floor /*= GROUND_LAYER*/, bool displaydialog) {
	return map.exportMinimap(filename, floor, displaydialog);
}

bool Editor::exportSelectionAsMiniMap(FileName directory, wxString fileName) {
	if (!directory.Exists() || !directory.IsDirWritable()) {
		return false;
	}

	int min_x = MAP_MAX_WIDTH + 1, min_y = MAP_MAX_HEIGHT + 1, min_z = MAP_MAX_LAYER + 1;
	int max_x = 0, max_y = 0, max_z = 0;

	const TileSet& tiles = selection.getTiles();
	for (Tile* tile : tiles) {
		if (tile->empty()) {
			continue;
		}

		Position pos = tile->getPosition();

		if (pos.x < min_x) {
			min_x = pos.x;
		}
		if (pos.x > max_x) {
			max_x = pos.x;
		}

		if (pos.y < min_y) {
			min_y = pos.y;
		}
		if (pos.y > max_y) {
			max_y = pos.y;
		}

		if (pos.z < min_z) {
			min_z = pos.z;
		}
		if (pos.z > max_z) {
			max_z = pos.z;
		}
	}

	int numtiles = (max_x - min_x) * (max_y - min_y);
	int minimap_width = max_x - min_x + 1;
	int minimap_height = max_y - min_y + 1;

	if (numtiles == 0) {
		return false;
	}

	if (minimap_width > 2048 || minimap_height > 2048) {
		g_gui.PopupDialog("Error", "Minimap size greater than 2048px.", wxOK);
		return false;
	}

	int tiles_iterated = 0;

	for (int z = min_z; z <= max_z; z++) {
		uint8_t* pixels = newd uint8_t[minimap_width * minimap_height * 3]; // 3 bytes per pixel
		memset(pixels, 0, minimap_width * minimap_height * 3);

		for (Tile* tile : tiles) {
			if (tile->getZ() != z) {
				continue;
			}

			++tiles_iterated;
			if (tiles_iterated % 8192 == 0) {
				g_gui.SetLoadDone(int(tiles_iterated / double(tiles.size()) * 90.0));
			}

			if (tile->empty()) {
				continue;
			}

			uint8_t color = 0;

			for (Item* item : tile->items) {
				if (item->getMiniMapColor() != 0) {
					color = item->getMiniMapColor();
					break;
				}
			}

			if (color == 0 && tile->hasGround()) {
				color = tile->ground->getMiniMapColor();
			}

			uint32_t index = ((tile->getY() - min_y) * minimap_width + (tile->getX() - min_x)) * 3;

			pixels[index] = (uint8_t)(int(color / 36) % 6 * 51); // red
			pixels[index + 1] = (uint8_t)(int(color / 6) % 6 * 51); // green
			pixels[index + 2] = (uint8_t)(color % 6 * 51); // blue
		}

		FileName file(fileName + "_" + i2ws(z) + ".png");
		file.Normalize(wxPATH_NORM_ALL, directory.GetFullPath());
		wxImage* image = newd wxImage(minimap_width, minimap_height, pixels, true);
		image->SaveFile(file.GetFullPath(), wxBITMAP_TYPE_PNG);
		image->Destroy();
		delete[] pixels;
	}

	return true;
}

bool Editor::importMap(FileName filename, int import_x_offset, int import_y_offset, ImportType house_import_type, ImportType spawn_import_type) {
	selection.clear();
	actionQueue->clear();

	Map imported_map;
	bool loaded = imported_map.open(nstr(filename.GetFullPath()));

	if (!loaded) {
		g_gui.PopupDialog("Error", "Error loading map!\n" + imported_map.getError(), wxOK | wxICON_INFORMATION);
		return false;
	}
	g_gui.ListDialog("Warning", imported_map.getWarnings());

	Position offset(import_x_offset, import_y_offset, 0);

	bool resizemap = false;
	bool resize_asked = false;
	int newsize_x = map.getWidth(), newsize_y = map.getHeight();
	int discarded_tiles = 0;

	g_gui.CreateLoadBar("Merging maps...");

	std::map<uint32_t, uint32_t> town_id_map;
	std::map<uint32_t, uint32_t> house_id_map;

	if (house_import_type != IMPORT_DONT) {
		for (TownMap::iterator tit = imported_map.towns.begin(); tit != imported_map.towns.end();) {
			Town* imported_town = tit->second;
			Town* current_town = map.towns.getTown(imported_town->getID());

			Position oldexit = imported_town->getTemplePosition();
			Position newexit = oldexit + offset;
			if (newexit.isValid()) {
				imported_town->setTemplePosition(newexit);
				map.getOrCreateTile(newexit)->getLocation()->increaseTownCount();
			}

			switch (house_import_type) {
				case IMPORT_MERGE: {
					town_id_map[imported_town->getID()] = imported_town->getID();
					if (current_town) {
						++tit;
						continue;
					}
					break;
				}
				case IMPORT_SMART_MERGE: {
					if (current_town) {
						// Compare and insert/merge depending on parameters
						if (current_town->getName() == imported_town->getName() && current_town->getID() == imported_town->getID()) {
							// Just add to map
							town_id_map[imported_town->getID()] = current_town->getID();
							++tit;
							continue;
						} else {
							// Conflict! Find a newd id and replace old
							uint32_t new_id = map.towns.getEmptyID();
							imported_town->setID(new_id);
							town_id_map[imported_town->getID()] = new_id;
						}
					} else {
						town_id_map[imported_town->getID()] = imported_town->getID();
					}
					break;
				}
				case IMPORT_INSERT: {
					// Find a newd id and replace old
					uint32_t new_id = map.towns.getEmptyID();
					imported_town->setID(new_id);
					town_id_map[imported_town->getID()] = new_id;
					break;
				}
				case IMPORT_DONT: {
					++tit;
					continue; // Should never happend..?
					break; // Continue or break ?
				}
			}

			map.towns.addTown(imported_town);

#ifdef __VISUALC__ // C++0x compliance to some degree :)
			tit = imported_map.towns.erase(tit);
#else // Bulky, slow way
			TownMap::iterator tmp_iter = tit;
			++tmp_iter;
			uint32_t next_key = 0;
			if (tmp_iter != imported_map.towns.end()) {
				next_key = tmp_iter->first;
			}
			imported_map.towns.erase(tit);
			if (next_key != 0) {
				tit = imported_map.towns.find(next_key);
			} else {
				tit = imported_map.towns.end();
			}
#endif
		}

		for (HouseMap::iterator hit = imported_map.houses.begin(); hit != imported_map.houses.end();) {
			House* imported_house = hit->second;
			House* current_house = map.houses.getHouse(imported_house->getID());
			imported_house->townid = town_id_map[imported_house->townid];

			Position oldexit = imported_house->getExit();
			imported_house->setExit(nullptr, Position()); // Reset it

			switch (house_import_type) {
				case IMPORT_MERGE: {
					house_id_map[imported_house->getID()] = imported_house->getID();
					if (current_house) {
						++hit;
						Position newexit = oldexit + offset;
						if (newexit.isValid()) {
							current_house->setExit(&map, newexit);
						}
						continue;
					}
					break;
				}
				case IMPORT_SMART_MERGE: {
					if (current_house) {
						// Compare and insert/merge depending on parameters
						if (current_house->name == imported_house->name && current_house->townid == imported_house->townid) {
							// Just add to map
							house_id_map[imported_house->getID()] = current_house->getID();
							++hit;
							Position newexit = oldexit + offset;
							if (newexit.isValid()) {
								imported_house->setExit(&map, newexit);
							}
							continue;
						} else {
							// Conflict! Find a newd id and replace old
							uint32_t new_id = map.houses.getEmptyID();
							house_id_map[imported_house->getID()] = new_id;
							imported_house->setID(new_id);
						}
					} else {
						house_id_map[imported_house->getID()] = imported_house->getID();
					}
					break;
				}
				case IMPORT_INSERT: {
					// Find a newd id and replace old
					uint32_t new_id = map.houses.getEmptyID();
					house_id_map[imported_house->getID()] = new_id;
					imported_house->setID(new_id);
					break;
				}
				case IMPORT_DONT: {
					++hit;
					Position newexit = oldexit + offset;
					if (newexit.isValid()) {
						imported_house->setExit(&map, newexit);
					}
					continue; // Should never happend..?
					break;
				}
			}

			Position newexit = oldexit + offset;
			if (newexit.isValid()) {
				imported_house->setExit(&map, newexit);
			}
			map.houses.addHouse(imported_house);

#ifdef __VISUALC__ // C++0x compliance to some degree :)
			hit = imported_map.houses.erase(hit);
#else // Bulky, slow way
			HouseMap::iterator tmp_iter = hit;
			++tmp_iter;
			uint32_t next_key = 0;
			if (tmp_iter != imported_map.houses.end()) {
				next_key = tmp_iter->first;
			}
			imported_map.houses.erase(hit);
			if (next_key != 0) {
				hit = imported_map.houses.find(next_key);
			} else {
				hit = imported_map.houses.end();
			}
#endif
		}
	}

	std::map<Position, Spawn*> spawn_map;
	if (spawn_import_type != IMPORT_DONT) {
		for (SpawnPositionList::iterator siter = imported_map.spawns.begin(); siter != imported_map.spawns.end();) {
			Position old_spawn_pos = *siter;
			Position new_spawn_pos = *siter + offset;
			switch (spawn_import_type) {
				case IMPORT_SMART_MERGE:
				case IMPORT_INSERT:
				case IMPORT_MERGE: {
					Tile* imported_tile = imported_map.getTile(old_spawn_pos);
					if (imported_tile) {
						ASSERT(imported_tile->spawn);
						spawn_map[new_spawn_pos] = imported_tile->spawn;

						SpawnPositionList::iterator next = siter;
						bool cont = true;
						Position next_spawn;

						++next;
						if (next == imported_map.spawns.end()) {
							cont = false;
						} else {
							next_spawn = *next;
						}
						imported_map.spawns.erase(siter);
						if (cont) {
							siter = imported_map.spawns.find(next_spawn);
						} else {
							siter = imported_map.spawns.end();
						}
					}
					break;
				}
				case IMPORT_DONT: {
					++siter;
					break;
				}
			}
		}
	}


	uint64_t tiles_merged = 0;
	uint64_t tiles_to_import = imported_map.tilecount;
	for (MapIterator mit = imported_map.begin(); mit != imported_map.end(); ++mit) {
		if (tiles_merged % 8092 == 0) {
			g_gui.SetLoadDone(int(100.0 * tiles_merged / tiles_to_import));
		}
		++tiles_merged;

		Tile* import_tile = (*mit)->get();
		Position new_pos = import_tile->getPosition() + offset;
		if (!new_pos.isValid()) {
			++discarded_tiles;
			continue;
		}

		if (!resizemap && (new_pos.x > map.getWidth() || new_pos.y > map.getHeight())) {
			if (resize_asked) {
				++discarded_tiles;
				continue;
			} else {
				resize_asked = true;
				int ret = g_gui.PopupDialog("Collision", "The imported tiles are outside the current map scope. Do you want to resize the map? (Else additional tiles will be removed)", wxYES | wxNO);

				if (ret == wxID_YES) {
					// ...
					resizemap = true;
				} else {
					++discarded_tiles;
					continue;
				}
			}
		}

		if (new_pos.x > newsize_x) {
			newsize_x = new_pos.x;
		}
		if (new_pos.y > newsize_y) {
			newsize_y = new_pos.y;
		}

		imported_map.setTile(import_tile->getPosition(), nullptr);
		TileLocation* location = map.createTileL(new_pos);

		// Check if we should update any houses
		int new_houseid = house_id_map[import_tile->getHouseID()];
		House* house = map.houses.getHouse(new_houseid);
		if (import_tile->isHouseTile() && house_import_type != IMPORT_DONT && house) {
			// We need to notify houses of the tile moving
			house->removeTile(import_tile);
			import_tile->setLocation(location);
			house->addTile(import_tile);
		} else {
			import_tile->setLocation(location);
		}

		if (offset != Position(0, 0, 0)) {
			for (ItemVector::iterator iter = import_tile->items.begin(); iter != import_tile->items.end(); ++iter) {
				Item* item = *iter;
				if (Teleport* teleport = dynamic_cast<Teleport*>(item)) {
					teleport->setDestination(teleport->getDestination() + offset);
				}
			}
		}

		Tile* old_tile = map.getTile(new_pos);
		if (old_tile) {
			map.removeSpawn(old_tile);
		}
		import_tile->creature = nullptr;
		import_tile->spawn = nullptr;

		map.setTile(new_pos, import_tile, true);
	}

	for (std::map<Position, Spawn*>::iterator spawn_iter = spawn_map.begin(); spawn_iter != spawn_map.end(); ++spawn_iter) {
		Position pos = spawn_iter->first;
		TileLocation* location = map.createTileL(pos);
		Tile* tile = location->get();
		if (!tile) {
			tile = map.allocator(location);
			map.setTile(pos, tile);
		} else if (tile->spawn) {
			map.removeSpawnInternal(tile);
			delete tile->spawn;
		}
		tile->spawn = spawn_iter->second;

		map.addSpawn(tile);
	}

	g_gui.DestroyLoadBar();

	map.setWidth(newsize_x);
	map.setHeight(newsize_y);
	g_gui.PopupDialog("Success", "Map imported successfully, " + i2ws(discarded_tiles) + " tiles were discarded as invalid.", wxOK);

	g_gui.RefreshPalettes();
	g_gui.FitViewToMap();

	return true;
}

void Editor::borderizeSelection() {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No items selected. Can't borderize.");
	}

	Action* action = actionQueue->createAction(ACTION_BORDERIZE);
	for (Tile* tile : selection) {
		Tile* newTile = tile->deepCopy(map);
		newTile->borderize(&map);
		newTile->select();
		action->addChange(newd Change(newTile));
	}
	addAction(action);
}

void Editor::borderizeMap(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Borderizing map...");
	}

	uint64_t tiles_done = 0;
	for (TileLocation* tileLocation : map) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = tileLocation->get();
		ASSERT(tile);

		tile->borderize(&map);
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::randomizeSelection() {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No items selected. Can't randomize.");
	}

	Action* action = actionQueue->createAction(ACTION_RANDOMIZE);
	for (Tile* tile : selection) {
		Tile* newTile = tile->deepCopy(map);
		GroundBrush* groundBrush = newTile->getGroundBrush();
		if (groundBrush && groundBrush->isReRandomizable()) {
			groundBrush->draw(&map, newTile, nullptr);

			Item* oldGround = tile->ground;
			Item* newGround = newTile->ground;
			if (oldGround && newGround) {
				newGround->setActionID(oldGround->getActionID());
				newGround->setUniqueID(oldGround->getUniqueID());
			}

			newTile->select();
			action->addChange(newd Change(newTile));
		}
	}
	addAction(action);
}

namespace {

// What a tile "looks like" to the content-aware fill matcher. Tiles sharing a
// ground brush count as the same material regardless of which variation or
// borders they carry; brushless grounds match on the raw item id.
uint32_t contentFillSignature(Tile* tile) {
	if (!tile) {
		return 0; // void
	}
	if (GroundBrush* brush = tile->getGroundBrush()) {
		return 0x80000000 | brush->getID();
	}
	if (tile->ground) {
		return 0x40000000 | tile->ground->getID();
	}
	return tile->items.empty() ? 0 : 0x20000000;
}

} // namespace

void Editor::contentAwareFillSelection(bool ignore_borders, bool ignore_ground) {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No items selected. Can't fill.");
		return;
	}

	// The area being replaced
	std::set<Position> targets;
	for (Tile* tile : selection) {
		targets.insert(tile->getPosition());
	}

	// Everything around the area is the palette we sample from (per floor,
	// since positions carry their own z). Void positions count too - if the
	// surroundings are empty, the fill should continue that emptiness.
	constexpr int SAMPLE_RADIUS = 4;
	constexpr size_t MAX_SAMPLES = 1024;

	std::set<Position> sample_set;
	for (const Position& pos : targets) {
		for (int dy = -SAMPLE_RADIUS; dy <= SAMPLE_RADIUS; ++dy) {
			for (int dx = -SAMPLE_RADIUS; dx <= SAMPLE_RADIUS; ++dx) {
				Position sample(pos.x + dx, pos.y + dy, pos.z);
				if (sample.x < 0 || sample.x >= map.getWidth() || sample.y < 0 || sample.y >= map.getHeight()) {
					continue;
				}
				if (targets.count(sample) == 0) {
					sample_set.insert(sample);
				}
			}
		}
	}

	if (sample_set.empty()) {
		g_gui.SetStatusText("Nothing around the selection to sample from.");
		return;
	}

	std::vector<Position> samples(sample_set.begin(), sample_set.end());
	if (samples.size() > MAX_SAMPLES) {
		// Partial Fisher-Yates: keep a random subset to bound the matching cost
		for (size_t i = 0; i < MAX_SAMPLES; ++i) {
			std::swap(samples[i], samples[i + random(int(samples.size() - i - 1))]);
		}
		samples.resize(MAX_SAMPLES);
	}

	// Content already decided for filled tiles; the map itself is untouched
	// until the action commits, so signature lookups go through here first
	std::map<Position, uint32_t> placed;

	// Returns false while the content at pos is still undecided
	auto knownSignature = [&](const Position& pos, uint32_t& sig) {
		auto it = placed.find(pos);
		if (it != placed.end()) {
			sig = it->second;
			return true;
		}
		if (targets.count(pos)) {
			return false;
		}
		sig = contentFillSignature(map.getTile(pos));
		return true;
	};

	// Peel the selection like an onion: start at tiles touching the outside
	// and grow inward, so every tile is matched against settled neighbors
	std::deque<Position> frontier;
	std::set<Position> visited;
	for (const Position& pos : targets) {
		bool touches_outside = false;
		for (int dy = -1; dy <= 1 && !touches_outside; ++dy) {
			for (int dx = -1; dx <= 1 && !touches_outside; ++dx) {
				touches_outside = targets.count(Position(pos.x + dx, pos.y + dy, pos.z)) == 0;
			}
		}
		if (touches_outside) {
			frontier.push_back(pos);
			visited.insert(pos);
		}
	}

	std::map<Position, Position> fill; // target -> sample it copies
	std::vector<size_t> best;
	while (!frontier.empty()) {
		const Position pos = frontier.front();
		frontier.pop_front();

		// Pick the sample whose surroundings best match this tile's surroundings
		int best_score = std::numeric_limits<int>::min();
		best.clear();
		for (size_t i = 0; i < samples.size(); ++i) {
			const Position& sample = samples[i];
			int score = 0;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0) {
						continue;
					}
					uint32_t want, have;
					if (!knownSignature(Position(pos.x + dx, pos.y + dy, pos.z), want) || !knownSignature(Position(sample.x + dx, sample.y + dy, sample.z), have)) {
						continue;
					}
					const int weight = (dx == 0 || dy == 0) ? 2 : 1;
					score += want == have ? weight : -weight;
				}
			}
			if (score > best_score) {
				best_score = score;
				best.clear();
			}
			if (score == best_score) {
				best.push_back(i);
			}
		}

		const Position& sample = samples[best[random(int(best.size()) - 1)]];
		fill[pos] = sample;
		placed[pos] = contentFillSignature(map.getTile(sample));

		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				Position next(pos.x + dx, pos.y + dy, pos.z);
				if (targets.count(next) && visited.count(next) == 0) {
					visited.insert(next);
					frontier.push_back(next);
				}
			}
		}
	}

	BatchAction* batch = actionQueue->createBatch(ACTION_CONTENT_AWARE_FILL);
	Action* action = actionQueue->createAction(batch);
	for (const auto& [pos, sample] : fill) {
		Tile* sample_tile = map.getTile(sample);
		TileLocation* location = map.createTileL(pos);
		Tile* new_tile = map.allocator(location);

		// The spot keeps its house/zone status; only the content is replaced.
		// Ignored categories are carried over from the old tile untouched.
		if (Tile* old_tile = location->get()) {
			new_tile->house_id = old_tile->house_id;
			new_tile->setMapFlags(old_tile->getMapFlags());
			if (ignore_ground && old_tile->ground) {
				new_tile->addItem(old_tile->ground->deepCopy());
			}
			if (ignore_borders) {
				for (Item* item : old_tile->items) {
					if (item->isBorder()) {
						new_tile->addItem(item->deepCopy());
					}
				}
			}
		}

		if (sample_tile) {
			if (!ignore_ground && sample_tile->ground) {
				new_tile->addItem(sample_tile->ground->deepCopy());
			}
			for (Item* item : sample_tile->items) {
				if (ignore_borders && item->isBorder()) {
					continue;
				}
				Item* copy = item->deepCopy();
				// Unique ids must not be duplicated across the map
				copy->eraseAttribute("uid");
				new_tile->addItem(copy);
			}
			// Creatures, spawns and house exits are deliberately not copied
		}

		new_tile->select();
		action->addChange(newd Change(new_tile));
	}
	batch->addAndCommitAction(action);

	// Settle the seams: reborder the filled area and the ring around it
	// (unless borders are ignored), and reconnect any walls that were
	// stamped down next to existing ones
	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		action = actionQueue->createAction(batch);
		std::set<Position> seams;
		for (const auto& [pos, sample] : fill) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					seams.insert(Position(pos.x + dx, pos.y + dy, pos.z));
				}
			}
		}
		for (const Position& pos : seams) {
			Tile* tile = map.getTile(pos);
			if (!tile) {
				continue;
			}
			if (ignore_borders && !tile->hasWall()) {
				continue;
			}
			Tile* new_tile = tile->deepCopy(map);
			if (!ignore_borders) {
				new_tile->borderize(&map);
			}
			if (new_tile->hasWall()) {
				reconnectTileWalls(&map, new_tile);
			}
			if (fill.count(pos)) {
				new_tile->select();
			}
			action->addChange(newd Change(new_tile));
		}
		batch->addAndCommitAction(action);
	}

	addBatch(batch);
	selection.updateSelectionCount();
	g_gui.SetStatusText(wxString::Format("Content-aware fill replaced %d tile(s).", int(fill.size())));
}

void Editor::randomizeMap(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Randomizing map...");
	}

	uint64_t tiles_done = 0;
	for (TileLocation* tileLocation : map) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(static_cast<int32_t>(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = tileLocation->get();
		ASSERT(tile);

		GroundBrush* groundBrush = tile->getGroundBrush();
		if (groundBrush) {
			Item* oldGround = tile->ground;

			uint16_t actionId, uniqueId;
			if (oldGround) {
				actionId = oldGround->getActionID();
				uniqueId = oldGround->getUniqueID();
			} else {
				actionId = 0;
				uniqueId = 0;
			}
			groundBrush->draw(&map, tile, nullptr);

			Item* newGround = tile->ground;
			if (newGround) {
				newGround->setActionID(actionId);
				newGround->setUniqueID(uniqueId);
			}
			tile->update();
		}
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::clearInvalidHouseTiles(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Clearing invalid house tiles...");
	}

	Houses& houses = map.houses;

	HouseMap::iterator iter = houses.begin();
	while (iter != houses.end()) {
		House* h = iter->second;
		if (map.towns.getTown(h->townid) == nullptr) {
#ifdef __VISUALC__ // C++0x compliance to some degree :)
			iter = houses.erase(iter);
#else // Bulky, slow way
			HouseMap::iterator tmp_iter = iter;
			++tmp_iter;
			uint32_t next_key = 0;
			if (tmp_iter != houses.end()) {
				next_key = tmp_iter->first;
			}
			houses.erase(iter);
			if (next_key != 0) {
				iter = houses.find(next_key);
			} else {
				iter = houses.end();
			}
#endif
		} else {
			++iter;
		}
	}

	uint64_t tiles_done = 0;
	for (MapIterator map_iter = map.begin(); map_iter != map.end(); ++map_iter) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = (*map_iter)->get();
		ASSERT(tile);
		if (tile->isHouseTile()) {
			if (houses.getHouse(tile->getHouseID()) == nullptr) {
				tile->setHouse(nullptr);
			}
		}
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::clearModifiedTileState(bool showdialog) {
	if (showdialog) {
		g_gui.CreateLoadBar("Clearing modified state from all tiles...");
	}

	uint64_t tiles_done = 0;
	for (MapIterator map_iter = map.begin(); map_iter != map.end(); ++map_iter) {
		if (showdialog && tiles_done % 4096 == 0) {
			g_gui.SetLoadDone(int(tiles_done / double(map.tilecount) * 100.0));
		}

		Tile* tile = (*map_iter)->get();
		ASSERT(tile);
		tile->unmodify();
		++tiles_done;
	}

	if (showdialog) {
		g_gui.DestroyLoadBar();
	}
}

void Editor::moveSelection(Position offset) {
	BatchAction* batchAction = actionQueue->createBatch(ACTION_MOVE); // Our saved action batch, for undo!
	Action* action;

	// Remove tiles from the map
	action = actionQueue->createAction(batchAction); // Our action!
	bool doborders = false;
	TileSet tmp_storage;

	// Update the tiles with the newd positions
	for (TileSet::iterator it = selection.begin(); it != selection.end(); ++it) {
		// First we get the old tile and it's position
		Tile* tile = (*it);
		// const Position pos = tile->getPosition();

		// Create the duplicate source tile, which will replace the old one later
		Tile* old_src_tile = tile;
		Tile* new_src_tile;

		new_src_tile = old_src_tile->deepCopy(map);

		Tile* tmp_storage_tile = map.allocator(tile->getLocation());

		// Get all the selected items from the NEW source tile and iterate through them
		// This transfers ownership to the temporary tile
		ItemVector tile_selection = new_src_tile->popSelectedItems();
		for (ItemVector::iterator iit = tile_selection.begin(); iit != tile_selection.end(); iit++) {
			// Add the copied item to the newd destination tile,
			Item* item = (*iit);
			tmp_storage_tile->addItem(item);
		}
		// Move spawns
		if (new_src_tile->spawn && new_src_tile->spawn->isSelected()) {
			tmp_storage_tile->spawn = new_src_tile->spawn;
			new_src_tile->spawn = nullptr;
		}
		// Move creatures
		if (new_src_tile->creature && new_src_tile->creature->isSelected()) {
			tmp_storage_tile->creature = new_src_tile->creature;
			new_src_tile->creature = nullptr;
		}

		// Move house data & tile status if ground is transferred
		if (tmp_storage_tile->ground) {
			tmp_storage_tile->house_id = new_src_tile->house_id;
			new_src_tile->house_id = 0;
			tmp_storage_tile->setMapFlags(new_src_tile->getMapFlags());
			new_src_tile->setMapFlags(TILESTATE_NONE);
			doborders = true;
		}

		tmp_storage.insert(tmp_storage_tile);
		// Add the tile copy to the action
		action->addChange(newd Change(new_src_tile));
	}
	// Commit changes to map
	batchAction->addAndCommitAction(action);

	// Remove old borders (and create some newd?)
	// Only touch borders when ground was actually moved; dragging plain items must not
	// re-borderize the source/destination neighbourhood (causes inverted borders).
	if (doborders && g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG) && selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD))) {
		action = actionQueue->createAction(batchAction);
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (TileSet::iterator it = tmp_storage.begin(); it != tmp_storage.end(); ++it) {
			Position pos = (*it)->getPosition();
			// Go through all neighbours
			Tile* t;
			t = map.getTile(pos.x, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
			t = map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (TileList::iterator it = borderize_tiles.begin(); it != borderize_tiles.end(); ++it) {
			Tile* tile = *it;
			Tile* new_tile = (*it)->deepCopy(map);
			if (doborders) {
				new_tile->borderize(&map);
			}
			new_tile->wallize(&map);
			new_tile->tableize(&map);
			new_tile->carpetize(&map);
			if (tile->ground && tile->ground->isSelected()) {
				new_tile->selectGround();
			}
			action->addChange(newd Change(new_tile));
		}
		// Commit changes to map
		batchAction->addAndCommitAction(action);
	}

	// New action for adding the destination tiles
	action = actionQueue->createAction(batchAction);
	for (TileSet::iterator it = tmp_storage.begin(); it != tmp_storage.end(); ++it) {
		Tile* tile = (*it);
		const Position old_pos = tile->getPosition();
		Position new_pos;

		new_pos = old_pos - offset;

		if (new_pos.z < 0 || new_pos.z > MAP_MAX_LAYER) {
			delete tile;
			continue;
		}
		// Create the duplicate dest tile, which will replace the old one later
		TileLocation* location = map.createTileL(new_pos);
		Tile* old_dest_tile = location->get();
		Tile* new_dest_tile = nullptr;

		if (g_settings.getInteger(Config::MERGE_MOVE) || !tile->ground) {
			// Move items
			if (old_dest_tile) {
				new_dest_tile = old_dest_tile->deepCopy(map);
			} else {
				new_dest_tile = map.allocator(location);
			}
			new_dest_tile->merge(tile);
			delete tile;
		} else {
			// Replace tile instead of just merge
			tile->setLocation(location);
			new_dest_tile = tile;
		}

		action->addChange(newd Change(new_dest_tile));
	}

	// Commit changes to the map
	batchAction->addAndCommitAction(action);

	// Create borders
	// Only touch borders when ground was actually moved; dragging plain items must not
	// re-borderize the destination neighbourhood (causes inverted borders).
	if (doborders && g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_DRAG) && selection.size() < size_t(g_settings.getInteger(Config::BORDERIZE_DRAG_THRESHOLD))) {
		action = actionQueue->createAction(batchAction);
		TileList borderize_tiles;
		// Go through all modified (selected) tiles (might be slow)
		for (TileSet::iterator it = selection.begin(); it != selection.end(); it++) {
			bool add_me = false; // If this tile is touched
			Position pos = (*it)->getPosition();
			// Go through all neighbours
			Tile* t;
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y - 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x - 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			t = map.getTile(pos.x + 1, pos.y + 1, pos.z);
			if (t && !t->isSelected()) {
				borderize_tiles.push_back(t);
				add_me = true;
			}
			if (add_me) {
				borderize_tiles.push_back(*it);
			}
		}
		// Remove duplicates
		borderize_tiles.sort();
		borderize_tiles.unique();
		// Do le borders!
		for (TileList::iterator it = borderize_tiles.begin(); it != borderize_tiles.end(); it++) {
			Tile* tile = *it;
			if (tile->ground) {
				if (tile->ground->getGroundBrush()) {
					Tile* new_tile = tile->deepCopy(map);

					if (doborders) {
						new_tile->borderize(&map);
					}

					new_tile->wallize(&map);
					new_tile->tableize(&map);
					new_tile->carpetize(&map);
					if (tile->ground->isSelected()) {
						new_tile->selectGround();
					}

					action->addChange(newd Change(new_tile));
				}
			}
		}
		// Commit changes to map
		batchAction->addAndCommitAction(action);
	}

	// Store the action for undo
	addBatch(batchAction);
	selection.updateSelectionCount();
}

void Editor::transformSelection(MapTransform transform) {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No selected items to transform.");
		return;
	}

	const Position min_pos = selection.minPosition();
	const Position max_pos = selection.maxPosition();

	BatchAction* batchAction = actionQueue->createBatch(ACTION_MOVE);
	Action* action = actionQueue->createAction(batchAction);

	// Lift the selected content off the source tiles into temporary tiles
	TileSet tmp_storage;
	for (Tile* tile : selection) {
		Tile* new_src_tile = tile->deepCopy(map);
		Tile* tmp_storage_tile = map.allocator(tile->getLocation());

		// This transfers ownership of the selected items to the temporary tile
		ItemVector tile_selection = new_src_tile->popSelectedItems();
		for (Item* item : tile_selection) {
			// Borders, walls, carpets and tables follow the new orientation
			transformItemOrientation(item, transform);
			tmp_storage_tile->addItem(item);
		}

		// Move spawns
		if (new_src_tile->spawn && new_src_tile->spawn->isSelected()) {
			tmp_storage_tile->spawn = new_src_tile->spawn;
			new_src_tile->spawn = nullptr;
		}
		// Move creatures
		if (new_src_tile->creature && new_src_tile->creature->isSelected()) {
			tmp_storage_tile->creature = new_src_tile->creature;
			new_src_tile->creature = nullptr;
		}

		// Move house data & tile status if ground is transferred
		if (tmp_storage_tile->ground) {
			tmp_storage_tile->house_id = new_src_tile->house_id;
			new_src_tile->house_id = 0;
			tmp_storage_tile->setMapFlags(new_src_tile->getMapFlags());
			new_src_tile->setMapFlags(TILESTATE_NONE);
		}

		tmp_storage.insert(tmp_storage_tile);
		action->addChange(newd Change(new_src_tile));
	}
	batchAction->addAndCommitAction(action);

	// Put the lifted content back down on the transformed positions
	action = actionQueue->createAction(batchAction);
	size_t discarded_tiles = 0;
	PositionVector destination_positions;
	destination_positions.reserve(tmp_storage.size());
	for (Tile* tile : tmp_storage) {
		const Position new_pos = transformPosition(tile->getPosition(), transform, min_pos, max_pos);

		if (new_pos.x < 0 || new_pos.x >= map.getWidth() || new_pos.y < 0 || new_pos.y >= map.getHeight()) {
			// The transformed area would leave the map, drop what doesn't fit
			delete tile;
			++discarded_tiles;
			continue;
		}

		TileLocation* location = map.createTileL(new_pos);
		Tile* old_dest_tile = location->get();
		Tile* new_dest_tile = nullptr;

		if (g_settings.getInteger(Config::MERGE_MOVE) || !tile->ground) {
			// Merge items into whatever is already there
			if (old_dest_tile) {
				new_dest_tile = old_dest_tile->deepCopy(map);
			} else {
				new_dest_tile = map.allocator(location);
			}
			new_dest_tile->merge(tile);
			delete tile;
		} else {
			// Replace the tile instead of merging
			tile->setLocation(location);
			new_dest_tile = tile;
		}

		destination_positions.push_back(new_pos);
		action->addChange(newd Change(new_dest_tile));
	}
	batchAction->addAndCommitAction(action);

	// Walls pick their piece from the walls around them, so re-connect the transformed
	// tiles and everything bordering them - that settles corners, ends and T-junctions
	// both inside the area and where it now meets the rest of the map
	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		action = actionQueue->createAction(batchAction);
		TileList wall_tiles;
		for (const Position& pos : destination_positions) {
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					Tile* tile = map.getTile(pos.x + dx, pos.y + dy, pos.z);
					if (tile && tile->hasWall()) {
						wall_tiles.push_back(tile);
					}
				}
			}
		}

		// Remove duplicates
		wall_tiles.sort();
		wall_tiles.unique();

		for (Tile* tile : wall_tiles) {
			Tile* new_tile = tile->deepCopy(map);
			if (reconnectTileWalls(&map, new_tile)) {
				action->addChange(newd Change(new_tile));
			} else {
				delete new_tile;
			}
		}
		batchAction->addAndCommitAction(action);
	}

	// Store the action for undo
	addBatch(batchAction);
	selection.updateSelectionCount();

	if (discarded_tiles > 0) {
		g_gui.SetStatusText(wxString::Format("%d tile(s) fell outside the map and were discarded.", int(discarded_tiles)));
	}
}

void Editor::destroySelection() {
	if (selection.size() == 0) {
		g_gui.SetStatusText("No selected items to delete.");
	} else {
		int tile_count = 0;
		int item_count = 0;
		int protected_item_count = 0;
		PositionList tilestoborder;

		BatchAction* batch = actionQueue->createBatch(ACTION_DELETE_TILES);
		Action* action = actionQueue->createAction(batch);
		bool has_changes = false;

		for (TileSet::iterator it = selection.begin(); it != selection.end(); ++it) {
			Tile* tile = *it;
			Tile* newtile = tile->deepCopy(map);
			bool tile_changed = false;

			if (tile->ground && tile->ground->isSelected()) {
				if (isItemProtectedByProperties(tile->ground)) {
					++protected_item_count;
				} else if (newtile->ground) {
					++item_count;
					delete newtile->ground;
					newtile->ground = nullptr;
					tile_changed = true;
				}
			}

			size_t newtile_item_index = 0;
			for (size_t item_index = 0; item_index < tile->items.size(); ++item_index) {
				Item* item = tile->items[item_index];
				if (!item->isSelected()) {
					++newtile_item_index;
					continue;
				}

				if (isItemProtectedByProperties(item)) {
					++protected_item_count;
					++newtile_item_index;
					continue;
				}

				if (newtile_item_index < newtile->items.size()) {
					++item_count;
					delete newtile->items[newtile_item_index];
					newtile->items.erase(newtile->items.begin() + newtile_item_index);
					tile_changed = true;
				}
			}

			if (newtile->creature && newtile->creature->isSelected()) {
				delete newtile->creature;
				newtile->creature = nullptr;
				tile_changed = true;
			}

			if (newtile->spawn && newtile->spawn->isSelected()) {
				delete newtile->spawn;
				newtile->spawn = nullptr;
				tile_changed = true;
			}

			// Only re-borderize the neighbourhood when ground was actually removed.
			// Deleting plain items must not re-borderize (causes inverted borders).
			bool ground_removed = tile->ground && tile->ground->isSelected() && !isItemProtectedByProperties(tile->ground);

			if (ground_removed && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				for (int y = -1; y <= 1; y++) {
					for (int x = -1; x <= 1; x++) {
						tilestoborder.push_back(
							Position(tile->getPosition().x + x, tile->getPosition().y + y, tile->getPosition().z)
						);
					}
				}
			}

			if (tile_changed) {
				++tile_count;
				has_changes = true;
				newtile->update();
				action->addChange(newd Change(newtile));
			} else {
				delete newtile;
			}
		}

		if (has_changes) {
			batch->addAndCommitAction(action);
		} else {
			delete action;
		}

		if (has_changes && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			action = actionQueue->createAction(batch);
			for (PositionList::iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();

				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->borderize(&map);
					new_tile->wallize(&map);
					new_tile->tableize(&map);
					new_tile->carpetize(&map);
					action->addChange(newd Change(new_tile));
				} else {
					Tile* new_tile = map.allocator(location);
					new_tile->borderize(&map);
					if (new_tile->size()) {
						action->addChange(newd Change(new_tile));
					} else {
						delete new_tile;
					}
				}
			}

			batch->addAndCommitAction(action);
		}

		if (has_changes) {
			addBatch(batch);
		} else {
			delete batch;
		}

		wxString ss;
		if (has_changes) {
			ss << "Deleted " << tile_count << " tile" << (tile_count > 1 ? "s" : "") << " (" << item_count << " item" << (item_count > 1 ? "s" : "") << ")";
		} else {
			ss << "No selected items were deleted.";
		}
		if (protected_item_count > 0) {
			ss << " " << protected_item_count << " item" << (protected_item_count > 1 ? "s were" : " was") << " kept because item properties are open.";
		}
		g_gui.SetStatusText(ss);
	}
}

// Macro to avoid useless code repetition
void doSurroundingBorders(DoodadBrush* doodad_brush, PositionList& tilestoborder, Tile* buffer_tile, Tile* new_tile) {
	if (doodad_brush->doNewBorders() && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y, new_tile->getPosition().z));
		if (buffer_tile->hasGround()) {
			for (int y = -1; y <= 1; y++) {
				for (int x = -1; x <= 1; x++) {
					tilestoborder.push_back(Position(new_tile->getPosition().x + x, new_tile->getPosition().y + y, new_tile->getPosition().z));
				}
			}
		} else if (buffer_tile->hasWall()) {
			tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y - 1, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x - 1, new_tile->getPosition().y, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x + 1, new_tile->getPosition().y, new_tile->getPosition().z));
			tilestoborder.push_back(Position(new_tile->getPosition().x, new_tile->getPosition().y + 1, new_tile->getPosition().z));
		}
	}
}

void removeDuplicateWalls(Tile* buffer, Tile* tile) {
	for (ItemVector::const_iterator iter = buffer->items.begin(); iter != buffer->items.end(); ++iter) {
		if ((*iter)->getWallBrush()) {
			tile->cleanWalls((*iter)->getWallBrush());
		}
	}
}

void Editor::drawInternal(Position offset, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (dodraw) {
		warnLiveBlockedBrushUse(brush);
	}

	if (brush->isDoodad()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		BaseMap* buffer_map = g_gui.doodad_buffer_map;

		Position delta_pos = offset - Position(0x8000, 0x8000, 0x8);
		PositionList tilestoborder;

		for (MapIterator it = buffer_map->begin(); it != buffer_map->end(); ++it) {
			Tile* buffer_tile = (*it)->get();
			Position pos = buffer_tile->getPosition() + delta_pos;
			if (!pos.isValid()) {
				continue;
			}

			TileLocation* location = map.createTileL(pos);
			Tile* tile = location->get();
			DoodadBrush* doodad_brush = brush->asDoodad();

			if (doodad_brush->placeOnBlocking() || alt) {
				if (tile) {
					bool place = true;
					if (!doodad_brush->placeOnDuplicate() && !alt) {
						for (ItemVector::const_iterator iter = tile->items.begin(); iter != tile->items.end(); ++iter) {
							if (doodad_brush->ownsItem(*iter)) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						Tile* new_tile = tile->deepCopy(map);
						removeDuplicateWalls(buffer_tile, new_tile);
						doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
						new_tile->merge(buffer_tile);
						action->addChange(newd Change(new_tile));
					}
				} else {
					Tile* new_tile = map.allocator(location);
					removeDuplicateWalls(buffer_tile, new_tile);
					doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
					new_tile->merge(buffer_tile);
					action->addChange(newd Change(new_tile));
				}
			} else {
				if (tile && !tile->isBlocking()) {
					bool place = true;
					if (!doodad_brush->placeOnDuplicate() && !alt) {
						for (ItemVector::const_iterator iter = tile->items.begin(); iter != tile->items.end(); ++iter) {
							if (doodad_brush->ownsItem(*iter)) {
								place = false;
								break;
							}
						}
					}
					if (place) {
						Tile* new_tile = tile->deepCopy(map);
						removeDuplicateWalls(buffer_tile, new_tile);
						doSurroundingBorders(doodad_brush, tilestoborder, buffer_tile, new_tile);
						new_tile->merge(buffer_tile);
						action->addChange(newd Change(new_tile));
					}
				}
			}
		}
		batch->addAndCommitAction(action);

		if (tilestoborder.size() > 0) {
			Action* action = actionQueue->createAction(batch);

			// Remove duplicates
			tilestoborder.sort();
			tilestoborder.unique();

			for (PositionList::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				Tile* tile = map.getTile(*it);
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->borderize(&map);
					new_tile->wallize(&map);
					action->addChange(newd Change(new_tile));
				}
			}
			batch->addAndCommitAction(action);
		}
		addBatch(batch, 2);
	} else if (brush->isHouseExit()) {
		HouseExitBrush* house_exit_brush = brush->asHouseExit();
		if (!house_exit_brush->canDraw(&map, offset)) {
			return;
		}

		House* house = map.houses.getHouse(house_exit_brush->getHouseID());
		if (!house) {
			return;
		}

		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		action->addChange(Change::Create(house, offset));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isWall()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		// This will only occur with a size 0, when clicking on a tile (not drawing)
		Tile* tile = map.getTile(offset);
		Tile* new_tile = nullptr;
		if (tile) {
			new_tile = tile->deepCopy(map);
		} else {
			new_tile = map.allocator(map.createTileL(offset));
		}

		if (dodraw) {
			bool b = true;
			brush->asWall()->draw(&map, new_tile, &b);
		} else {
			brush->asWall()->undraw(&map, new_tile);
		}
		action->addChange(newd Change(new_tile));
		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	} else if (brush->isSpawn() || brush->isCreature()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		Position draw_position = offset;
		if (brush->isSpawn()) {
			draw_position.x -= 1;
			draw_position.y -= 1;
		}

		Tile* tile = map.getTile(draw_position);
		bool had_spawn = tile && tile->spawn;

		Tile* new_tile = nullptr;
		if (tile) {
			new_tile = tile->deepCopy(map);
		} else {
			new_tile = map.allocator(map.createTileL(draw_position));
		}

		int param = 0;
		if (!brush->isCreature()) {
			param = g_gui.GetBrushSize();
		}

		if (dodraw) {
			brush->draw(&map, new_tile, &param);
		} else {
			brush->undraw(&map, new_tile);
		}

		action->addChange(newd Change(new_tile));

		if (brush->isCreature() && dodraw) {
			bool spawn_added = (new_tile->spawn != nullptr) && !had_spawn;
			if (spawn_added) {
				Position spawn_position = offset;
				spawn_position.x -= 1;
				spawn_position.y -= 1;

				Tile* spawn_tile = map.getTile(spawn_position);
				bool spawn_target_has_spawn = spawn_tile && spawn_tile->spawn;
				if (!spawn_target_has_spawn) {
					Tile* spawn_new_tile = nullptr;
					if (spawn_tile) {
						spawn_new_tile = spawn_tile->deepCopy(map);
					} else {
						spawn_new_tile = map.allocator(map.createTileL(spawn_position));
					}

					if (spawn_new_tile->spawn) {
						delete spawn_new_tile->spawn;
						spawn_new_tile->spawn = nullptr;
					}

					spawn_new_tile->spawn = new_tile->spawn;
					new_tile->spawn = nullptr;
					action->addChange(newd Change(spawn_new_tile));
				}
			}
		}

		batch->addAndCommitAction(action);
		addBatch(batch, 2);
	}
}

void Editor::drawInternal(const PositionVector& tilestodraw, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (dodraw) {
		warnLiveBlockedBrushUse(brush);
	}

#ifdef __DEBUG__
	if (brush->isGround() || brush->isWall()) {
		// Wrong function, end call
		return;
	}
#endif

	Action* action = actionQueue->createAction(ACTION_DRAW);

	if (brush->isOptionalBorder()) {
		// We actually need to do borders, but on the same tiles we draw to
		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				if (dodraw) {
					Tile* new_tile = tile->deepCopy(map);
					brush->draw(&map, new_tile);
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				} else if (!dodraw && tile->hasOptionalBorder()) {
					Tile* new_tile = tile->deepCopy(map);
					brush->undraw(&map, new_tile);
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				}
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				brush->draw(&map, new_tile);
				new_tile->borderize(&map);
				if (new_tile->size() == 0) {
					delete new_tile;
					continue;
				}
				action->addChange(newd Change(new_tile));
			}
		}
	} else {

		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					brush->draw(&map, new_tile, &alt);
				} else {
					brush->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				brush->draw(&map, new_tile, &alt);
				action->addChange(newd Change(new_tile));
			}
		}
	}
	addAction(action, 2);
}

void Editor::drawInternal(const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw) {
	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush) {
		return;
	}

	if (dodraw) {
		warnLiveBlockedBrushUse(brush);
	}

	if (brush->isGround() || brush->isEraser()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
					new_tile->cleanBorders();
				}
				if (dodraw) {
					if (brush->isGround() && alt) {
						std::pair<bool, GroundBrush*> param;
						if (replace_brush) {
							param.first = false;
							param.second = replace_brush;
						} else {
							param.first = true;
							param.second = nullptr;
						}
						g_gui.GetCurrentBrush()->draw(&map, new_tile, &param);
					} else {
						g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
					}
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
					tilestoborder.push_back(*it);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				if (brush->isGround() && alt) {
					std::pair<bool, GroundBrush*> param;
					if (replace_brush) {
						param.first = false;
						param.second = replace_brush;
					} else {
						param.first = true;
						param.second = nullptr;
					}
					g_gui.GetCurrentBrush()->draw(&map, new_tile, &param);
				} else {
					g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				}
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = actionQueue->createAction(batch);
			for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					if (brush->isEraser()) {
						new_tile->wallize(&map);
						new_tile->tableize(&map);
						new_tile->carpetize(&map);
					}
					new_tile->borderize(&map);
					action->addChange(newd Change(new_tile));
				} else {
					Tile* new_tile = map.allocator(location);
					if (brush->isEraser()) {
						// There are no carpets/tables/walls on empty tiles...
						// new_tile->wallize(map);
						// new_tile->tableize(map);
						// new_tile->carpetize(map);
					}
					new_tile->borderize(&map);
					if (new_tile->size() > 0) {
						action->addChange(newd Change(new_tile));
					} else {
						delete new_tile;
					}
				}
			}
			batch->addAndCommitAction(action);
		}

		addBatch(batch, 2);
	} else if (brush->isTable() || brush->isCarpet()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				g_gui.GetCurrentBrush()->draw(&map, new_tile, nullptr);
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		// Do borders!
		action = actionQueue->createAction(batch);
		for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
			Tile* tile = map.getTile(*it);
			if (brush->isTable()) {
				if (tile && tile->hasTable()) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->tableize(&map);
					action->addChange(newd Change(new_tile));
				}
			} else if (brush->isCarpet()) {
				if (tile && tile->hasCarpet()) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->carpetize(&map);
					action->addChange(newd Change(new_tile));
				}
			}
		}
		batch->addAndCommitAction(action);

		addBatch(batch, 2);
	} else if (brush->isWall()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);

		if (alt && dodraw) {
			// This is exempt from USE_AUTOMAGIC
			g_gui.doodad_buffer_map->clear();
			BaseMap* draw_map = g_gui.doodad_buffer_map;

			for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->cleanWalls(brush->isWall());
					g_gui.GetCurrentBrush()->draw(draw_map, new_tile);
					draw_map->setTile(*it, new_tile, true);
				} else if (dodraw) {
					Tile* new_tile = map.allocator(location);
					g_gui.GetCurrentBrush()->draw(draw_map, new_tile);
					draw_map->setTile(*it, new_tile, true);
				}
			}
			for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				// Get the correct tiles from the draw map instead of the editor map
				Tile* tile = draw_map->getTile(*it);
				if (tile) {
					tile->wallize(draw_map);
					action->addChange(newd Change(tile));
				}
			}
			draw_map->clear(false);
			// Commit
			batch->addAndCommitAction(action);
		} else {
			for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
				TileLocation* location = map.createTileL(*it);
				Tile* tile = location->get();
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					// Wall cleaning is exempt from automagic
					new_tile->cleanWalls(brush->isWall());
					if (dodraw) {
						g_gui.GetCurrentBrush()->draw(&map, new_tile);
					} else {
						g_gui.GetCurrentBrush()->undraw(&map, new_tile);
					}
					action->addChange(newd Change(new_tile));
				} else if (dodraw) {
					Tile* new_tile = map.allocator(location);
					g_gui.GetCurrentBrush()->draw(&map, new_tile);
					action->addChange(newd Change(new_tile));
				}
			}

			// Commit changes to map
			batch->addAndCommitAction(action);

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				// Do borders!
				action = actionQueue->createAction(batch);
				for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
					Tile* tile = map.getTile(*it);
					if (tile) {
						Tile* new_tile = tile->deepCopy(map);
						new_tile->wallize(&map);
						// if(*tile == *new_tile) delete new_tile;
						action->addChange(newd Change(new_tile));
					}
				}
				batch->addAndCommitAction(action);
			}
		}

		actionQueue->addBatch(batch, 2);
	} else if (brush->isDoor()) {
		BatchAction* batch = actionQueue->createBatch(ACTION_DRAW);
		Action* action = actionQueue->createAction(batch);
		DoorBrush* door_brush = brush->asDoor();

		// Loop is kind of redundant since there will only ever be one index.
		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				// Wall cleaning is exempt from automagic
				if (brush->isWall()) {
					new_tile->cleanWalls(brush->asWall());
				}
				if (dodraw) {
					door_brush->draw(&map, new_tile, &alt);
				} else {
					door_brush->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				door_brush->draw(&map, new_tile, &alt);
				action->addChange(newd Change(new_tile));
			}
		}

		// Commit changes to map
		batch->addAndCommitAction(action);

		if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
			// Do borders!
			action = actionQueue->createAction(batch);
			for (PositionVector::const_iterator it = tilestoborder.begin(); it != tilestoborder.end(); ++it) {
				Tile* tile = map.getTile(*it);
				if (tile) {
					Tile* new_tile = tile->deepCopy(map);
					new_tile->wallize(&map);
					// if(*tile == *new_tile) delete new_tile;
					action->addChange(newd Change(new_tile));
				}
			}
			batch->addAndCommitAction(action);
		}

		addBatch(batch, 2);
	} else {
		Action* action = actionQueue->createAction(ACTION_DRAW);
		for (PositionVector::const_iterator it = tilestodraw.begin(); it != tilestodraw.end(); ++it) {
			TileLocation* location = map.createTileL(*it);
			Tile* tile = location->get();
			if (tile) {
				Tile* new_tile = tile->deepCopy(map);
				if (dodraw) {
					g_gui.GetCurrentBrush()->draw(&map, new_tile);
				} else {
					g_gui.GetCurrentBrush()->undraw(&map, new_tile);
				}
				action->addChange(newd Change(new_tile));
			} else if (dodraw) {
				Tile* new_tile = map.allocator(location);
				g_gui.GetCurrentBrush()->draw(&map, new_tile);
				action->addChange(newd Change(new_tile));
			}
		}
		addAction(action, 2);
	}
}

void Editor::warnLiveBlockedBrushUse(const Brush* brush) {
	if (IsLiveClient() && live_client) {
		live_client->warnIfBlockedBrushUse(brush);
	}
}

