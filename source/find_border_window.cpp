//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "find_border_window.h"
#include "brush_edit.h"
#include "brush.h"
#include "gui.h"
#include "theme.h"

namespace {
enum {
	BORDER_DIALOG_LIST = 41001,
	BORDER_DIALOG_FILTER_ID,
	BORDER_DIALOG_FILTER_TEXT,
	BORDER_DIALOG_PREVIEW,
};
} // namespace

BorderDialogListBox::BorderDialogListBox(wxWindow* parent, wxWindowID id) :
	wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE) {
	////
}

void BorderDialogListBox::SetBorderIds(const std::vector<uint32_t>& ids) {
	border_ids = ids;
	SetItemCount(border_ids.size());
}

uint32_t BorderDialogListBox::GetSelectedBorderId() const {
	const ssize_t selection = GetSelection();
	if (selection == wxNOT_FOUND || static_cast<size_t>(selection) >= border_ids.size()) {
		return 0;
	}
	return border_ids[selection];
}

void BorderDialogListBox::SelectBorderId(uint32_t borderId) {
	for (size_t i = 0; i < border_ids.size(); ++i) {
		if (border_ids[i] == borderId) {
			SetSelection(i);
			return;
		}
	}
}

void BorderDialogListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const {
	if (n >= border_ids.size()) {
		return;
	}

	const uint32_t borderId = border_ids[n];
	const AutoBorder* border = g_brushes.getAutoBorder(borderId);
	const wxRect previewRect(rect.GetX() + 2, rect.GetY() + 2, 44, 44);
	DrawAutoBorderPreview(dc, previewRect, border);

	dc.SetTextForeground(ThemeManager::Get().GetPalette().text);

	wxString label = wxString::Format("Border %u", borderId);
	if (border && border->group != 0) {
		label += wxString::Format("  (group %u)", border->group);
	}
	dc.DrawText(label, rect.GetX() + 52, rect.GetY() + 16);
}

wxCoord BorderDialogListBox::OnMeasureItem(size_t WXUNUSED(n)) const {
	return 48;
}

BEGIN_EVENT_TABLE(FindBorderDialog, wxDialog)
EVT_SPINCTRL(BORDER_DIALOG_FILTER_ID, FindBorderDialog::OnFilterSpinChanged)
EVT_TEXT(BORDER_DIALOG_FILTER_TEXT, FindBorderDialog::OnFilterChanged)
EVT_TIMER(wxID_ANY, FindBorderDialog::OnFilterTimer)
EVT_LISTBOX(BORDER_DIALOG_LIST, FindBorderDialog::OnListSelected)
EVT_LISTBOX_DCLICK(BORDER_DIALOG_LIST, FindBorderDialog::OnListDoubleClick)
EVT_BUTTON(wxID_OK, FindBorderDialog::OnClickOK)
EVT_BUTTON(wxID_CANCEL, FindBorderDialog::OnClickCancel)
END_EVENT_TABLE()

FindBorderDialog::FindBorderDialog(wxWindow* parent, uint32_t initialBorderId /* = 0 */) :
	wxDialog(parent, wxID_ANY, "Choose Border", wxDefaultPosition, wxWindow::FromDIP(wxSize(640, 520), parent), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	filter_timer(this),
	result_id(initialBorderId),
	preview_border_id(initialBorderId) {
	SetSizeHints(FromDIP(wxSize(520, 420)), wxDefaultSize);

	wxBoxSizer* rootSizer = newd wxBoxSizer(wxVERTICAL);

	wxBoxSizer* filterSizer = newd wxBoxSizer(wxHORIZONTAL);
	filterSizer->Add(newd wxStaticText(this, wxID_ANY, "Border ID"), wxSizerFlags(0).CenterVertical().Border(wxALL, 5));
	filter_id_spin = newd wxSpinCtrl(this, BORDER_DIALOG_FILTER_ID, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(90, -1)), wxSP_ARROW_KEYS, 0, 10000, static_cast<int>(initialBorderId));
	filterSizer->Add(filter_id_spin, wxSizerFlags(0).CenterVertical().Border(wxTOP | wxBOTTOM | wxRIGHT, 5));
	filterSizer->Add(newd wxStaticText(this, wxID_ANY, "Filter"), wxSizerFlags(0).CenterVertical().Border(wxALL, 5));
	filter_text = newd wxTextCtrl(this, BORDER_DIALOG_FILTER_TEXT, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(160, -1)));
	filterSizer->Add(filter_text, wxSizerFlags(1).CenterVertical().Expand().Border(wxTOP | wxBOTTOM | wxRIGHT, 5));
	rootSizer->Add(filterSizer, wxSizerFlags(0).Expand());

	wxBoxSizer* contentSizer = newd wxBoxSizer(wxHORIZONTAL);

	border_list = newd BorderDialogListBox(this, BORDER_DIALOG_LIST);
	contentSizer->Add(border_list, wxSizerFlags(1).Expand().Border(wxALL, 5));

	wxBoxSizer* previewSizer = newd wxBoxSizer(wxVERTICAL);
	preview_panel = newd wxPanel(this, BORDER_DIALOG_PREVIEW, wxDefaultPosition, FromDIP(wxSize(128, 128)));
	preview_panel->SetMinSize(FromDIP(wxSize(128, 128)));
	preview_panel->Connect(wxEVT_PAINT, wxPaintEventHandler(FindBorderDialog::OnPreviewPaint), nullptr, this);
	previewSizer->Add(preview_panel, wxSizerFlags(0).CenterHorizontal().Border(wxALL, 5));
	preview_label = newd wxStaticText(this, wxID_ANY, "Select a border.");
	preview_label->Wrap(120);
	previewSizer->Add(preview_label, wxSizerFlags(0).CenterHorizontal().Border(wxLEFT | wxRIGHT | wxBOTTOM, 5));
	contentSizer->Add(previewSizer, wxSizerFlags(0).Top().Border(wxALL, 5));

	rootSizer->Add(contentSizer, wxSizerFlags(1).Expand());

	wxStdDialogButtonSizer* buttons = newd wxStdDialogButtonSizer();
	ok_button = newd wxButton(this, wxID_OK);
	buttons->AddButton(ok_button);
	buttons->AddButton(newd wxButton(this, wxID_CANCEL));
	buttons->Realize();
	ok_button->SetDefault();
	rootSizer->Add(buttons, wxSizerFlags(0).Expand().Border(wxALL, 5));

	SetSizer(rootSizer);
	Centre(wxBOTH);

	RefreshList();
	SelectBorderId(initialBorderId);
	UpdatePreview();
}

FindBorderDialog::~FindBorderDialog() {
	if (preview_panel) {
		preview_panel->Disconnect(wxEVT_PAINT, wxPaintEventHandler(FindBorderDialog::OnPreviewPaint), nullptr, this);
	}
}

void FindBorderDialog::RefreshList() {
	const std::vector<uint32_t> allIds = BrushGetAllBorderIds();
	const wxString filter = filter_text->GetValue().Lower();

	std::vector<uint32_t> filtered;
	filtered.reserve(allIds.size());
	for (uint32_t borderId : allIds) {
		if (!filter.empty()) {
			const wxString idText = wxString::Format("%u", borderId);
			if (!idText.Lower().Contains(filter)) {
				continue;
			}
		}
		filtered.push_back(borderId);
	}

	border_list->SetBorderIds(filtered);

	if (!filtered.empty()) {
		if (preview_border_id != 0) {
			border_list->SelectBorderId(preview_border_id);
		}
		if (border_list->GetSelectedBorderId() == 0) {
			border_list->SetSelection(0);
		}
		ok_button->Enable(border_list->GetSelectedBorderId() != 0);
	} else {
		ok_button->Enable(false);
	}
}

void FindBorderDialog::SelectBorderId(uint32_t borderId) {
	if (borderId == 0) {
		return;
	}
	border_list->SelectBorderId(borderId);
	if (border_list->GetSelectedBorderId() == 0 && border_list->GetItemCount() > 0) {
		border_list->SetSelection(0);
	}
	preview_border_id = border_list->GetSelectedBorderId();
}

void FindBorderDialog::UpdatePreview() {
	preview_border_id = border_list->GetSelectedBorderId();
	if (preview_border_id != 0) {
		const AutoBorder* border = g_brushes.getAutoBorder(preview_border_id);
		wxString label = wxString::Format("Border %u", preview_border_id);
		if (border) {
			if (border->group != 0) {
				label += wxString::Format("\nGroup %u", border->group);
			}
			if (border->ground) {
				label += "\nGround border";
			}
		}
		preview_label->SetLabel(label);
		ok_button->Enable(true);
		if (filter_id_spin->GetValue() != static_cast<int>(preview_border_id)) {
			filter_id_spin->SetValue(static_cast<int>(preview_border_id));
		}
	} else {
		preview_label->SetLabel("No matching borders.");
		ok_button->Enable(false);
	}
	if (preview_panel) {
		preview_panel->Refresh();
	}
}

void FindBorderDialog::OnFilterChanged(wxCommandEvent& WXUNUSED(event)) {
	if (filter_timer.IsRunning()) {
		filter_timer.Stop();
	}
	filter_timer.Start(150, true);
}

void FindBorderDialog::OnFilterSpinChanged(wxSpinEvent& WXUNUSED(event)) {
	const uint32_t borderId = static_cast<uint32_t>(filter_id_spin->GetValue());
	SelectBorderId(borderId);
	UpdatePreview();
}

void FindBorderDialog::OnFilterTimer(wxTimerEvent& WXUNUSED(event)) {
	RefreshList();
	UpdatePreview();
}

void FindBorderDialog::OnListSelected(wxCommandEvent& WXUNUSED(event)) {
	UpdatePreview();
}

void FindBorderDialog::OnListDoubleClick(wxCommandEvent& WXUNUSED(event)) {
	wxCommandEvent dummy;
	OnClickOK(dummy);
}

void FindBorderDialog::OnPreviewPaint(wxPaintEvent& WXUNUSED(event)) {
	wxPaintDC dc(preview_panel);
	const wxSize size = preview_panel->GetClientSize();
	const wxRect rect(0, 0, size.GetWidth(), size.GetHeight());
	const AutoBorder* border = preview_border_id != 0 ? g_brushes.getAutoBorder(preview_border_id) : nullptr;
	DrawAutoBorderPreview(dc, rect, border);
}

void FindBorderDialog::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	if (filter_timer.IsRunning()) {
		filter_timer.Stop();
	}

	const uint32_t selectedId = border_list->GetSelectedBorderId();
	if (selectedId != 0) {
		result_id = selectedId;
		EndModal(wxID_OK);
	}
}

void FindBorderDialog::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}
