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

#include "comments_window.h"
#include "gui.h"
#include "editor.h"
#include "map_comment.h"

BEGIN_EVENT_TABLE(CommentsWindow, wxPanel)
EVT_LISTBOX(wxID_ANY, CommentsWindow::OnClickResult)
EVT_BUTTON(wxID_EDIT, CommentsWindow::OnClickEdit)
EVT_BUTTON(wxID_DELETE, CommentsWindow::OnClickDelete)
EVT_BUTTON(wxID_REFRESH, CommentsWindow::OnClickRefresh)
END_EVENT_TABLE()

CommentsWindow::CommentsWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
	comment_list = newd wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(260, 330)), 0, nullptr, wxLB_SINGLE | wxLB_ALWAYS_SB);
	sizer->Add(comment_list, wxSizerFlags(1).Expand());

	wxSizer* buttonsSizer = newd wxBoxSizer(wxHORIZONTAL);
	buttonsSizer->Add(newd wxButton(this, wxID_EDIT, "Edit"), wxSizerFlags(0).Center());
	buttonsSizer->Add(newd wxButton(this, wxID_DELETE, "Delete"), wxSizerFlags(0).Center());
	buttonsSizer->Add(newd wxButton(this, wxID_REFRESH, "Refresh"), wxSizerFlags(0).Center());
	sizer->Add(buttonsSizer, wxSizerFlags(0).Center().DoubleBorder());
	SetSizerAndFit(sizer);

	ReloadList();
}

CommentsWindow::~CommentsWindow() {
	Clear();
}

void CommentsWindow::Clear() {
	for (uint32_t n = 0; n < comment_list->GetCount(); ++n) {
		delete reinterpret_cast<uint32_t*>(comment_list->GetClientData(n));
	}
	comment_list->Clear();
}

void CommentsWindow::ReloadList() {
	Clear();

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	for (const MapComment& comment : editor->getMap().getComments().all()) {
		wxString preview = wxstr(comment.text);
		preview.Replace("\n", " ");
		if (preview.length() > 60) {
			preview = preview.substr(0, 60) + "...";
		}

		wxString label = wxString::Format(
			"[Floor %d] %s (%s): %s",
			comment.pos.z,
			wxstr(comment.author).c_str(),
			formatMapCommentTime(comment.createdUnix).c_str(),
			preview.c_str()
		);

		comment_list->Append(label, newd uint32_t(comment.id));
	}
}

uint32_t CommentsWindow::GetSelectedCommentId() const {
	int selection = comment_list->GetSelection();
	if (selection == wxNOT_FOUND) {
		return 0;
	}
	uint32_t* id = reinterpret_cast<uint32_t*>(comment_list->GetClientData(selection));
	return id ? *id : 0;
}

void CommentsWindow::OnClickResult(wxCommandEvent& event) {
	uint32_t* id = reinterpret_cast<uint32_t*>(event.GetClientData());
	if (!id) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	const MapComment* comment = editor->getMap().getComments().findById(*id);
	if (comment) {
		g_gui.SetScreenCenterPosition(comment->pos);
	}
}

void CommentsWindow::OnClickEdit(wxCommandEvent& WXUNUSED(event)) {
	uint32_t id = GetSelectedCommentId();
	if (id == 0) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	if (editor->promptEditMapComment(id)) {
		ReloadList();
	}
}

void CommentsWindow::OnClickDelete(wxCommandEvent& WXUNUSED(event)) {
	uint32_t id = GetSelectedCommentId();
	if (id == 0) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	const MapComment* comment = editor->getMap().getComments().findById(id);
	if (!comment) {
		return;
	}

	if (g_gui.PopupDialog(
			"Delete Map Comment",
			wxString::Format("Delete comment by %s?\n\n%s", wxstr(comment->author), wxstr(comment->text)),
			wxYES | wxNO
		)
		!= wxID_YES) {
		return;
	}

	editor->removeMapComment(id);
	ReloadList();
}

void CommentsWindow::OnClickRefresh(wxCommandEvent& WXUNUSED(event)) {
	ReloadList();
}
