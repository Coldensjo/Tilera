//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "live_update.h"

#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <utility>

namespace {

wxString executableDirectory() {
	wxFileName execFile;
	try {
		execFile = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast&) {
		execFile.Assign(wxGetCwd(), wxEmptyString);
	}
	return execFile.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

bool fileSizeIfExists(const wxFileName& path, uint32_t& size) {
	if (!path.FileExists()) {
		return false;
	}

	wxStructStat statBuffer;
	if (wxStat(path.GetFullPath(), &statBuffer) != 0) {
		return false;
	}

	size = static_cast<uint32_t>(statBuffer.st_size);
	return true;
}

wxString normalizeUpdateRelativePath(wxString path) {
	path.Trim(true).Trim(false);
	path.Replace("\\", "/");
	while (path.StartsWith("./")) {
		path = path.Mid(2);
	}
	while (path.EndsWith("/") && path.length() > 1) {
		path.RemoveLast();
	}
	return path;
}

bool isSafeUpdateRelativePath(const wxString& path) {
	if (path.empty()) {
		return false;
	}
	if (path.StartsWith("/") || path.Find(':') != wxNOT_FOUND) {
		return false;
	}

	const wxString normalized = normalizeUpdateRelativePath(path);
	const wxArrayString parts = wxSplit(normalized, '/');
	for (const wxString& part : parts) {
		if (part.empty() || part == "." || part == "..") {
			return false;
		}
	}
	return !parts.empty();
}

bool resolveUpdatePath(const wxFileName& baseDir, const wxString& relativePath, wxFileName& out) {
	if (!isSafeUpdateRelativePath(relativePath)) {
		return false;
	}

	const wxString normalized = normalizeUpdateRelativePath(relativePath);
	const wxArrayString parts = wxSplit(normalized, '/');
	if (parts.empty()) {
		return false;
	}

	out = baseDir;
	for (size_t i = 0; i + 1 < parts.size(); ++i) {
		out.AppendDir(parts[i]);
	}
	out.SetFullName(parts.Last());
	return true;
}

std::string makeUpdateRelativeFilename(const wxFileName& baseDir, const wxFileName& sourcePath) {
	wxFileName relative = sourcePath;
	const wxString base = baseDir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	if (relative.MakeRelativeTo(base)) {
		return nstr(normalizeUpdateRelativePath(relative.GetFullPath()));
	}
	return nstr(sourcePath.GetFullName());
}

} // namespace

bool collectUpdateFiles(const std::vector<wxString>& paths, std::vector<LiveUpdateFile>& files, wxString& error) {
	files.clear();

	wxFileName baseDir;
	baseDir.AssignDir(executableDirectory());

	for (const wxString& rawPath : paths) {
		wxString trimmed = rawPath;
		trimmed.Trim(true).Trim(false);
		if (trimmed.empty()) {
			continue;
		}

		wxFileName sourcePath(trimmed);
		sourcePath.Normalize(RME_PATH_NORM_FLAGS, wxEmptyString);
		if (!sourcePath.FileExists()) {
			error = "Update file not found: " + trimmed;
			files.clear();
			return false;
		}

		uint32_t size = 0;
		if (!fileSizeIfExists(sourcePath, size) || size == 0) {
			error = "Update file is empty or unreadable: " + trimmed;
			files.clear();
			return false;
		}

		const std::string relativeName = makeUpdateRelativeFilename(baseDir, sourcePath);
		if (!isSafeUpdateRelativePath(wxstr(relativeName))) {
			error = "Update file has an invalid relative path: " + trimmed;
			files.clear();
			return false;
		}

		LiveUpdateFile file;
		file.filename = relativeName;
		file.sourcePath = sourcePath;
		file.size = size;
		files.push_back(file);
	}

	if (files.empty()) {
		error = "No update files are configured.";
		return false;
	}

	return true;
}

wxFileName getLiveUpdateStagingDir() {
	wxFileName dir;
	dir.AssignDir(executableDirectory());
	dir.AppendDir("update_staging");
	return dir;
}

void resetLiveUpdateReceiveState(LiveUpdateReceiveState& state) {
	if (state.output.is_open()) {
		state.output.close();
	}
	state = LiveUpdateReceiveState();
}

bool beginLiveUpdateReceive(LiveUpdateReceiveState& state, const std::string& filename, uint32_t size, wxString& error) {
	if (state.output.is_open()) {
		state.output.close();
	}

	if (!state.stagingDir.IsOk() || state.stagingDir.GetPath().empty()) {
		state.stagingDir = getLiveUpdateStagingDir();
	}
	state.stagingDir.Mkdir(0755, wxPATH_MKDIR_FULL);

	const wxString relativePath = normalizeUpdateRelativePath(wxstr(filename));
	if (!isSafeUpdateRelativePath(relativePath)) {
		error = "Server sent an update file with an invalid name.";
		return false;
	}

	wxFileName target;
	if (!resolveUpdatePath(state.stagingDir, relativePath, target)) {
		error = "Server sent an update file with an invalid name.";
		return false;
	}
	target.Mkdir(wxPATH_MKDIR_FULL);

	state.output.open(nstr(target.GetFullPath()), std::ios::binary | std::ios::trunc);
	if (!state.output.is_open()) {
		error = "Could not write to the update staging folder: " + target.GetFullPath();
		return false;
	}

	state.currentFilename = nstr(relativePath);
	state.expectedSize = size;
	state.receivedSize = 0;
	state.active = true;
	return true;
}

bool appendLiveUpdateChunk(LiveUpdateReceiveState& state, const std::string& chunk, wxString& error) {
	if (!state.output.is_open()) {
		error = "Received an update chunk before the file started.";
		return false;
	}

	state.output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
	if (!state.output.good()) {
		error = "Failed while writing the downloaded update.";
		return false;
	}

	state.receivedSize += static_cast<uint32_t>(chunk.size());
	return true;
}

bool finishLiveUpdateReceive(LiveUpdateReceiveState& state, wxString& error) {
	if (state.output.is_open()) {
		state.output.close();
	}

	if (state.receivedSize != state.expectedSize) {
		error = wxString::Format("Update file '%s' was truncated (%u of %u bytes).",
			wxstr(state.currentFilename), state.receivedSize, state.expectedSize);
		return false;
	}

	state.receivedFiles.push_back(state.currentFilename);
	state.currentFilename.clear();
	state.expectedSize = 0;
	state.receivedSize = 0;
	return true;
}

bool applyLiveUpdateAndRestart(const wxFileName& stagingDir, const std::vector<std::string>& files, bool reconnect, wxString& error) {
	if (files.empty()) {
		error = "The server did not send any update files.";
		return false;
	}

	const wxString execDir = executableDirectory();
	wxFileName execFile;
	try {
		execFile = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	} catch (const std::bad_cast&) {
		error = "Could not locate the running editor executable.";
		return false;
	}
	const wxString runningExe = execFile.GetFullName();

	wxFileName execDirFile;
	execDirFile.AssignDir(execDir);

	// Validate every staged file up front so we never start swapping with a
	// truncated download.
	for (const std::string& name : files) {
		wxFileName staged;
		if (!resolveUpdatePath(stagingDir, wxstr(name), staged)) {
			error = "The downloaded update contains an invalid file path.";
			return false;
		}
		uint32_t size = 0;
		if (!fileSizeIfExists(staged, size) || size == 0) {
			error = "The downloaded update is incomplete; please try again.";
			return false;
		}
	}

	// Swap each staged file over the live one. The running executable cannot be
	// deleted on Windows, but it can be renamed; we move it aside to *.old and
	// drop the new build in its place. Track what we moved so we can roll back if
	// a later file fails, leaving a working editor behind.
	std::vector<std::pair<wxFileName, wxFileName>> backups; // <target, backup>
	const auto rollback = [&backups]() {
		for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
			const wxFileName& target = it->first;
			const wxFileName& backup = it->second;
			if (target.FileExists()) {
				wxRemoveFile(target.GetFullPath());
			}
			if (backup.FileExists()) {
				wxRenameFile(backup.GetFullPath(), target.GetFullPath(), true);
			}
		}
	};

	for (const std::string& name : files) {
		wxFileName staged;
		if (!resolveUpdatePath(stagingDir, wxstr(name), staged)) {
			error = "The downloaded update contains an invalid file path.";
			rollback();
			return false;
		}

		wxFileName target;
		if (!resolveUpdatePath(execDirFile, wxstr(name), target)) {
			error = "The downloaded update contains an invalid file path.";
			rollback();
			return false;
		}
		target.Mkdir(wxPATH_MKDIR_FULL);

		if (target.FileExists()) {
			const wxString backupPath = target.GetFullPath() + ".old";
			if (wxFileExists(backupPath)) {
				wxRemoveFile(backupPath);
			}
			if (!wxRenameFile(target.GetFullPath(), backupPath, true)) {
				error = "Could not replace '" + wxstr(name) + "'. Close other editor windows and try again.";
				rollback();
				return false;
			}
			wxFileName backup;
			backup.Assign(backupPath);
			backups.emplace_back(target, backup);
		}

		bool installed = wxRenameFile(staged.GetFullPath(), target.GetFullPath(), true);
		if (!installed) {
			// Rename can fail across volumes/handles; fall back to a copy.
			if (wxCopyFile(staged.GetFullPath(), target.GetFullPath(), true)) {
				wxRemoveFile(staged.GetFullPath());
				installed = true;
			}
		}
		if (!installed) {
			error = "Could not install the new '" + wxstr(name) + "'.";
			rollback();
			return false;
		}
	}

	// Relaunch the (now updated) editor executable.
	wxFileName newExe;
	newExe.AssignDir(execDir);
	newExe.SetFullName(runningExe);

	wxString command = "\"" + newExe.GetFullPath() + "\"";
	if (reconnect) {
		command += " --live-reconnect";
	}

	if (wxExecute(command, wxEXEC_ASYNC) == 0) {
		error = "The update was installed but the editor could not be relaunched automatically. Please start it again.";
		return false;
	}

	return true;
}

void cleanupLiveUpdateLeftovers() {
	const wxString execDir = executableDirectory();
	if (execDir.empty()) {
		return;
	}

	wxArrayString oldFiles;
	wxDir::GetAllFiles(execDir, &oldFiles, "*.old", wxDIR_FILES);
	for (const wxString& file : oldFiles) {
		wxRemoveFile(file);
	}

	// Drop any half-finished staging folder from an interrupted update.
	wxFileName staging = getLiveUpdateStagingDir();
	if (staging.DirExists()) {
		staging.Rmdir(wxPATH_RMDIR_RECURSIVE);
	}
}
