#include "app/MainFrame.h"

#include "audio_path_inspector/audio/DeviceInspectionService.h"

#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <sstream>
#include <vector>

namespace audio_path_inspector::app {

namespace {

constexpr int WindowWidth = 980;
constexpr int WindowHeight = 640;
constexpr int LeftPaneWidth = 280;

wxString toWxString(const std::wstring& value) {
    return wxString(value);
}

wxString formatApoKind(const model::ApoKind kind) {
    switch (kind) {
    case model::ApoKind::StreamEffect:
        return "Stream Effect";
    case model::ApoKind::ModeEffect:
        return "Mode Effect";
    case model::ApoKind::EndpointEffect:
        return "Endpoint Effect";
    case model::ApoKind::Unknown:
        return "Unknown";
    }

    return "Unknown";
}

void appendApoSection(wxString& text, const std::vector<model::ApoInfo>& apos, const std::wstring& message) {
    text << "Registered APOs\n";

    if (apos.empty()) {
        text << "No registered APOs detected in endpoint properties.\n\n";
        return;
    }

    for (const model::ApoInfo& apo : apos) {
        text << "- " << formatApoKind(apo.kind) << "\n";
        text << "  CLSID: " << toWxString(apo.clsid.empty() ? L"Unknown" : apo.clsid) << "\n";
        text << "  DLL: " << toWxString(apo.dllName.empty() ? L"Unknown" : apo.dllName) << "\n";
        text << "  Path: " << toWxString(apo.dllPath.empty() ? L"Unknown" : apo.dllPath) << "\n";
        text << "  Vendor: " << toWxString(apo.vendor.empty() ? L"Unknown" : apo.vendor) << "\n";
        text << "  Version: " << toWxString(apo.fileVersion.empty() ? L"Unknown" : apo.fileVersion) << "\n";
    }

    text << "\n";
}

void appendAudioEffectsSection(wxString& text, const std::vector<model::AudioEffectInfo>& effects, const std::wstring& message) {
    text << "Active Effects\n";

    if (effects.empty()) {
        text << "No Windows Audio Effects Detected.\n\n";
        return;
    }

    for (const model::AudioEffectInfo& effect : effects) {
        text << "- " << toWxString(effect.name.empty() ? L"Unknown" : effect.name);
        text << ": " << (effect.enabled ? "On" : "Off");
        if (!effect.status.empty()) {
            text << " (" << toWxString(effect.status) << ")";
        }
        text << "\n";
    }

    text << "\n";
}

wxString formatAudioEnhancementsState(const model::AudioEnhancementsState state) {
    switch (state) {
    case model::AudioEnhancementsState::On:
        return "On";
    case model::AudioEnhancementsState::Off:
        return "Off";
    case model::AudioEnhancementsState::Unknown:
        return "Unknown";
    case model::AudioEnhancementsState::NotAvailable:
        return "Not available";
    }

    return "Unknown";
}

void appendAudioEnhancementsSection(wxString& text, const model::AudioEnhancementsInfo& info, const std::wstring& message) {
    text << "Audio Enhancements\n";
    text << formatAudioEnhancementsState(info.state) << "\n";
    if (!info.detail.empty()) {
        text << toWxString(info.detail) << "\n";
    }
    text << "\n";
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Audio Path Inspector", wxDefaultPosition, wxSize(WindowWidth, WindowHeight)) {
    buildLayout();
    bindEvents();
    refreshDevices();
}

void MainFrame::buildLayout() {
    auto* rootPanel = new wxPanel(this);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    auto* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
    auto* refreshButton = new wxButton(rootPanel, wxID_REFRESH, "Refresh");
    toolbarSizer->Add(refreshButton, 0, wxALL, 6);
    toolbarSizer->AddStretchSpacer();
    rootSizer->Add(toolbarSizer, 0, wxEXPAND);

    auto* splitter = new wxSplitterWindow(rootPanel, wxID_ANY);

    auto* leftPanel = new wxPanel(splitter);
    auto* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->Add(new wxStaticText(leftPanel, wxID_ANY, "Device List"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
    deviceList_ = new wxListBox(leftPanel, wxID_ANY);
    leftSizer->Add(deviceList_, 1, wxEXPAND | wxALL, 8);
    leftPanel->SetSizer(leftSizer);

    auto* rightPanel = new wxPanel(splitter);
    auto* rightSizer = new wxBoxSizer(wxVERTICAL);
    rightSizer->Add(new wxStaticText(rightPanel, wxID_ANY, "Device Information"), 0, wxLEFT | wxRIGHT | wxTOP, 8);
    detailsText_ = new wxTextCtrl(
        rightPanel,
        wxID_ANY,
        "",
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    rightSizer->Add(detailsText_, 3, wxEXPAND | wxALL, 8);
    rightSizer->Add(new wxStaticText(rightPanel, wxID_ANY, "Log"), 0, wxLEFT | wxRIGHT, 8);
    logText_ = new wxTextCtrl(
        rightPanel,
        wxID_ANY,
        "",
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY);
    rightSizer->Add(logText_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    rightPanel->SetSizer(rightSizer);

    splitter->SplitVertically(leftPanel, rightPanel, LeftPaneWidth);
    splitter->SetMinimumPaneSize(180);

    rootSizer->Add(splitter, 1, wxEXPAND);
    rootPanel->SetSizer(rootSizer);
}

void MainFrame::bindEvents() {
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refreshDevices(); }, wxID_REFRESH);
    deviceList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) {
        const int selection = event.GetSelection();
        if (selection != wxNOT_FOUND) {
            showDeviceDetails(static_cast<std::size_t>(selection));
        }
    });
}

void MainFrame::refreshDevices() {
    deviceList_->Clear();
    detailsText_->Clear();
    devices_.clear();

    std::wstring errorMessage;
    const audio::DeviceInspectionService service;
    devices_ = service.enumerateCaptureDevices(errorMessage);

    if (!errorMessage.empty()) {
        appendLog(toWxString(errorMessage));
    }

    for (const model::DeviceSummary& device : devices_) {
        deviceList_->Append(toWxString(device.friendlyName));
    }

    std::wstringstream message;
    message << L"Capture devices found: " << devices_.size();
    appendLog(toWxString(message.str()));

    if (!devices_.empty()) {
        deviceList_->SetSelection(0);
        showDeviceDetails(0);
    } else {
        detailsText_->SetValue("No capture devices detected.");
    }
}

void MainFrame::showDeviceDetails(const std::size_t index) {
    if (index >= devices_.size()) {
        return;
    }

    const model::DeviceSummary& device = devices_[index];
    std::wstring errorMessage;
    const audio::DeviceInspectionService service;
    const model::DeviceInspection inspection = service.inspectCaptureDevice(device, errorMessage);
    const model::DeviceDetails& details = inspection.details;
    const model::StreamOpenResult& streamOpenResult = inspection.streamOpenResult;

    if (!errorMessage.empty()) {
        appendLog(toWxString(errorMessage));
    }

    wxString text;
    text << "Friendly Name\n";
    text << toWxString(details.friendlyName.empty() ? device.friendlyName : details.friendlyName) << "\n\n";
    text << "Endpoint ID\n";
    text << toWxString(details.endpointId) << "\n\n";
    text << "Device ID\n";
    text << toWxString(device.deviceId) << "\n\n";
    text << "Driver\n";
    text << toWxString(details.driverName) << "\n\n";
    text << "Driver Version\n";
    text << toWxString(details.driverVersion) << "\n\n";
    text << "Manufacturer\n";
    text << toWxString(details.manufacturer.empty() ? L"Unknown" : details.manufacturer) << "\n\n";
    text << "Mix Format\n";
    text << "Format: " << toWxString(details.formatTag.empty() ? L"Unknown" : details.formatTag) << "\n";
    text << "Sample Rate: " << details.sampleRate << " Hz\n";
    text << "Channels: " << details.channelCount << "\n";
    text << "Bits Per Sample: " << details.bitsPerSample << "\n\n";
    appendApoSection(text, inspection.apos, inspection.apoMessage);
    appendAudioEffectsSection(text, inspection.audioEffects, inspection.audioEffectsMessage);
    appendAudioEnhancementsSection(text, inspection.audioEnhancements, inspection.audioEnhancementsMessage);
    text << "Stream Open Tests\n";
    text << "Shared: " << (streamOpenResult.sharedOk ? "OK" : "Failed") << "\n";
    text << toWxString(streamOpenResult.sharedMessage) << "\n";
    text << "Shared RAW: " << (streamOpenResult.rawOk ? "OK" : "Failed") << "\n";
    text << toWxString(streamOpenResult.rawMessage) << "\n";
    text << "Exclusive: " << (streamOpenResult.exclusiveOk ? "OK" : "Failed") << "\n";
    text << toWxString(streamOpenResult.exclusiveMessage) << "\n";

    detailsText_->SetValue(text);
}

void MainFrame::appendLog(const wxString& message) {
    if (logText_ == nullptr) {
        return;
    }

    logText_->AppendText(message);
    logText_->AppendText("\n");
}

} // namespace audio_path_inspector::app
