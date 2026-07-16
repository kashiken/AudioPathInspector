#include "app/MainFrame.h"

#include "audio_path_inspector/audio/DeviceInspectionService.h"
#include "audio_path_inspector/windows/ComApartment.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <objbase.h>

#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace audio_path_inspector::app {

namespace {

constexpr int WindowWidth = 1040;
constexpr int WindowHeight = 700;
constexpr int LeftPaneWidth = 300;

wxString toWxString(const std::wstring& value) {
    return wxString(value);
}

wxString fallbackString(const std::wstring& value, const wchar_t* fallback) {
    return toWxString(value.empty() ? fallback : value);
}

wxString flowName(const model::DeviceFlow flow) {
    return flow == model::DeviceFlow::Capture ? "Capture" : "Render";
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

wxString openStatus(const bool ok, const std::wstring& message) {
    wxString value = ok ? "OK" : "Failed";
    if (!message.empty()) {
        value << " - " << toWxString(message);
    }
    return value;
}

wxTreeItemId appendItem(wxTreeCtrl& tree, const wxTreeItemId& parent, const wxString& label) {
    return tree.AppendItem(parent, label);
}

void appendKeyValue(wxTreeCtrl& tree, const wxTreeItemId& parent, const wxString& key, const wxString& value) {
    tree.AppendItem(parent, key + ": " + value);
}

void appendMessage(wxTreeCtrl& tree, const wxTreeItemId& parent, const std::wstring& message) {
    if (!message.empty()) {
        tree.AppendItem(parent, toWxString(message));
    }
}

void appendApoChainGroup(wxTreeCtrl& tree, const wxTreeItemId& parent, const wxString& label, const std::vector<model::ApoInfo>& apos, const model::ApoKind kind) {
    const wxTreeItemId group = appendItem(tree, parent, label);

    bool found = false;
    int index = 1;
    for (const model::ApoInfo& apo : apos) {
        if (apo.kind != kind) {
            continue;
        }

        found = true;
        wxString title;
        title << index << ". ";
        if (!apo.propertyLabel.empty()) {
            title << toWxString(apo.propertyLabel) << " - ";
        }
        title << fallbackString(apo.dllName, L"Unknown APO");

        const wxTreeItemId node = appendItem(tree, group, title);
        appendKeyValue(tree, node, "CLSID", fallbackString(apo.clsid, L"Unknown"));
        appendKeyValue(tree, node, "Vendor", fallbackString(apo.vendor, L"Unknown"));
        ++index;
    }

    if (!found) {
        appendItem(tree, group, "(none registered)");
    }
}

void appendApoNode(wxTreeCtrl& tree, const wxTreeItemId& parent, const model::ApoInfo& apo) {
    const wxString label = apo.propertyLabel.empty() ? formatApoKind(apo.kind) : toWxString(apo.propertyLabel);
    const wxTreeItemId node = appendItem(tree, parent, label);
    appendKeyValue(tree, node, "CLSID", fallbackString(apo.clsid, L"Unknown"));
    appendKeyValue(tree, node, "DLL", fallbackString(apo.dllName, L"Unknown"));
    appendKeyValue(tree, node, "Path", fallbackString(apo.dllPath, L"Unknown"));
    appendKeyValue(tree, node, "Vendor", fallbackString(apo.vendor, L"Unknown"));
    appendKeyValue(tree, node, "Version", fallbackString(apo.fileVersion, L"Unknown"));

    wxString notificationSupport = apo.notificationsSupported ? "IAudioProcessingObjectNotifications" : "Not supported";
    if (apo.notifications2Supported) {
        notificationSupport << " + Notifications2";
    }
    appendKeyValue(tree, node, "APO Notifications", notificationSupport);
    appendMessage(tree, node, apo.notificationsMessage);
}

void appendAudiodgModules(wxTreeCtrl& tree, const wxTreeItemId& parent, const std::vector<model::ModuleInfo>& modules, const std::wstring& message) {
    const wxTreeItemId section = appendItem(tree, parent, "audiodg.exe Loaded DLLs");
    appendMessage(tree, section, message);

    if (modules.empty()) {
        appendItem(tree, section, "No modules were listed.");
        return;
    }

    unsigned long currentPid = 0;
    wxTreeItemId processNode;
    for (const model::ModuleInfo& module : modules) {
        if (module.processId != currentPid || !processNode.IsOk()) {
            currentPid = module.processId;
            wxString processLabel;
            processLabel << "audiodg.exe pid " << currentPid;
            processNode = appendItem(tree, section, processLabel);
        }

        const wxTreeItemId moduleNode = appendItem(tree, processNode, fallbackString(module.name, L"Unknown module"));
        appendKeyValue(tree, moduleNode, "Path", fallbackString(module.path, L"Unknown"));
    }
}

} // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Audio Path Inspector", wxDefaultPosition, wxSize(WindowWidth, WindowHeight)) {
    buildLayout();
    bindEvents();
    refreshDevices();
}

MainFrame::~MainFrame() {
    alive_->store(false);
}

void MainFrame::buildLayout() {
    auto* rootPanel = new wxPanel(this);
    auto* rootSizer = new wxBoxSizer(wxVERTICAL);

    auto* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
    toolbarSizer->Add(new wxStaticText(rootPanel, wxID_ANY, "Flow"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    flowChoice_ = new wxChoice(rootPanel, wxID_ANY);
    flowChoice_->Append("Capture");
    flowChoice_->Append("Render");
    flowChoice_->SetSelection(0);
    toolbarSizer->Add(flowChoice_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);

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
    detailsTree_ = new wxTreeCtrl(
        rightPanel,
        wxID_ANY,
        wxDefaultPosition,
        wxDefaultSize,
        wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT);
    rightSizer->Add(detailsTree_, 3, wxEXPAND | wxALL, 8);
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
    flowChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        updateCurrentFlowFromUi();
        refreshDevices();
    });
    deviceList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) {
        const int selection = event.GetSelection();
        if (selection != wxNOT_FOUND) {
            showDeviceDetails(static_cast<std::size_t>(selection));
        }
    });
}

void MainFrame::updateCurrentFlowFromUi() {
    currentFlow_ = flowChoice_ != nullptr && flowChoice_->GetSelection() == 1
        ? model::DeviceFlow::Render
        : model::DeviceFlow::Capture;
}

void MainFrame::showDetailsMessage(const wxString& message) {
    if (detailsTree_ == nullptr) {
        return;
    }

    detailsTree_->DeleteAllItems();
    const wxTreeItemId root = detailsTree_->AddRoot("Details");
    detailsTree_->AppendItem(root, message);
    detailsTree_->ExpandAll();
}

void MainFrame::refreshDevices() {
    updateCurrentFlowFromUi();
    const model::DeviceFlow flow = currentFlow_;
    const unsigned int requestId = ++deviceRequestId_;
    ++inspectionRequestId_;

    deviceList_->Clear();
    showDetailsMessage("Loading " + flowName(flow).Lower() + " devices...");
    devices_.clear();

    std::thread([this, alive = alive_, flow, requestId]() {
        const windows::ComApartment comApartment(COINIT_MULTITHREADED);
        std::wstring errorMessage;
        std::vector<model::DeviceSummary> devices;
        if (comApartment.ok()) {
            const audio::DeviceInspectionService service;
            devices = service.enumerateDevices(flow, errorMessage);
        } else {
            errorMessage = L"CoInitializeEx(worker) failed.";
        }

        wxTheApp->CallAfter([this, alive, flow, requestId, devices = std::move(devices), errorMessage = std::move(errorMessage)]() mutable {
            if (!alive->load() || requestId != deviceRequestId_) {
                return;
            }

            devices_ = std::move(devices);
            deviceList_->Clear();

            if (!errorMessage.empty()) {
                appendLog(toWxString(errorMessage));
            }

            for (const model::DeviceSummary& device : devices_) {
                deviceList_->Append(toWxString(device.friendlyName));
            }

            wxString message;
            message << flowName(flow) << " devices found: " << devices_.size();
            appendLog(message);

            if (!devices_.empty()) {
                deviceList_->SetSelection(0);
                showDeviceDetails(0);
            } else {
                showDetailsMessage("No " + flowName(flow).Lower() + " devices detected.");
            }
        });
    }).detach();
}

void MainFrame::showDeviceDetails(const std::size_t index) {
    if (index >= devices_.size()) {
        return;
    }

    const model::DeviceSummary device = devices_[index];
    const model::DeviceFlow flow = currentFlow_;
    const unsigned int requestId = ++inspectionRequestId_;
    showDetailsMessage("Inspecting " + toWxString(device.friendlyName) + "...");

    std::thread([this, alive = alive_, flow, device, requestId]() {
        const windows::ComApartment comApartment(COINIT_MULTITHREADED);
        std::wstring errorMessage;
        model::DeviceInspection inspection;
        if (comApartment.ok()) {
            const audio::DeviceInspectionService service;
            inspection = service.inspectDevice(flow, device, errorMessage);
        } else {
            errorMessage = L"CoInitializeEx(worker) failed.";
        }

        wxTheApp->CallAfter([this, alive, flow, device, requestId, inspection = std::move(inspection), errorMessage = std::move(errorMessage)]() {
            if (!alive->load() || requestId != inspectionRequestId_) {
                return;
            }

            displayDeviceDetails(flow, device, inspection, errorMessage);
        });
    }).detach();
}

void MainFrame::displayDeviceDetails(
    const model::DeviceFlow flow,
    const model::DeviceSummary& device,
    const model::DeviceInspection& inspection,
    const std::wstring& errorMessage) {
    if (detailsTree_ == nullptr) {
        return;
    }

    const model::DeviceDetails& details = inspection.details;
    const model::StreamOpenResult& streamOpenResult = inspection.streamOpenResult;

    if (!errorMessage.empty()) {
        appendLog(toWxString(errorMessage));
    }

    detailsTree_->DeleteAllItems();
    const wxTreeItemId root = detailsTree_->AddRoot("Device Inspection");

    const wxTreeItemId deviceInfo = appendItem(*detailsTree_, root, "Device");
    appendKeyValue(*detailsTree_, deviceInfo, "Flow", flowName(flow));
    appendKeyValue(*detailsTree_, deviceInfo, "Friendly Name", fallbackString(details.friendlyName.empty() ? device.friendlyName : details.friendlyName, L"Unknown"));
    appendKeyValue(*detailsTree_, deviceInfo, "Endpoint ID", fallbackString(details.endpointId, L"Unknown"));
    appendKeyValue(*detailsTree_, deviceInfo, "Device ID", fallbackString(device.deviceId, L"Unknown"));
    appendKeyValue(*detailsTree_, deviceInfo, "Driver", fallbackString(details.driverName, L"Unknown"));
    appendKeyValue(*detailsTree_, deviceInfo, "Driver Version", fallbackString(details.driverVersion, L"Unknown"));
    appendKeyValue(*detailsTree_, deviceInfo, "Manufacturer", fallbackString(details.manufacturer, L"Unknown"));

    const wxTreeItemId mixFormat = appendItem(*detailsTree_, root, "Mix Format");
    appendKeyValue(*detailsTree_, mixFormat, "Format", fallbackString(details.formatTag, L"Unknown"));
    appendKeyValue(*detailsTree_, mixFormat, "Sample Rate", wxString::Format("%u Hz", details.sampleRate));
    appendKeyValue(*detailsTree_, mixFormat, "Channels", wxString::Format("%u", details.channelCount));
    appendKeyValue(*detailsTree_, mixFormat, "Bits Per Sample", wxString::Format("%u", details.bitsPerSample));

    const wxTreeItemId chain = appendItem(*detailsTree_, root, "APO Chain (inferred)");
    appendItem(*detailsTree_, chain, "Endpoint property registration order. This is not a runtime execution trace.");
    appendItem(*detailsTree_, chain, "Device Endpoint");
    appendApoChainGroup(*detailsTree_, chain, "Stream Effects", inspection.apos, model::ApoKind::StreamEffect);
    appendApoChainGroup(*detailsTree_, chain, "Mode Effects", inspection.apos, model::ApoKind::ModeEffect);
    appendApoChainGroup(*detailsTree_, chain, "Endpoint Effects", inspection.apos, model::ApoKind::EndpointEffect);
    appendItem(*detailsTree_, chain, "Windows Audio Engine / Effects");

    const wxTreeItemId registeredApos = appendItem(*detailsTree_, root, "Registered APOs");
    appendMessage(*detailsTree_, registeredApos, inspection.apoMessage);
    if (inspection.apos.empty()) {
        appendItem(*detailsTree_, registeredApos, "No registered APOs detected in endpoint properties.");
    } else {
        for (const model::ApoInfo& apo : inspection.apos) {
            appendApoNode(*detailsTree_, registeredApos, apo);
        }
    }

    const wxTreeItemId activeEffects = appendItem(*detailsTree_, root, "Active Effects");
    appendMessage(*detailsTree_, activeEffects, inspection.audioEffectsMessage);
    if (inspection.audioEffects.empty()) {
        appendItem(*detailsTree_, activeEffects, "No Windows Audio Effects Detected.");
    } else {
        for (const model::AudioEffectInfo& effect : inspection.audioEffects) {
            wxString label = fallbackString(effect.name, L"Unknown");
            label << ": " << (effect.enabled ? "On" : "Off");
            const wxTreeItemId effectNode = appendItem(*detailsTree_, activeEffects, label);
            appendKeyValue(*detailsTree_, effectNode, "Status", fallbackString(effect.status, L"Unknown"));
        }
    }

    const wxTreeItemId enhancements = appendItem(*detailsTree_, root, "Audio Enhancements");
    appendKeyValue(*detailsTree_, enhancements, "State", formatAudioEnhancementsState(inspection.audioEnhancements.state));
    appendMessage(*detailsTree_, enhancements, inspection.audioEnhancements.detail);
    if (inspection.audioEnhancementsMessage != inspection.audioEnhancements.detail) {
        appendMessage(*detailsTree_, enhancements, inspection.audioEnhancementsMessage);
    }

    const wxTreeItemId streamTests = appendItem(*detailsTree_, root, "Stream Open Tests");
    appendKeyValue(*detailsTree_, streamTests, "Shared", openStatus(streamOpenResult.sharedOk, streamOpenResult.sharedMessage));
    appendKeyValue(*detailsTree_, streamTests, "Shared RAW", openStatus(streamOpenResult.rawOk, streamOpenResult.rawMessage));
    appendKeyValue(*detailsTree_, streamTests, "Exclusive", openStatus(streamOpenResult.exclusiveOk, streamOpenResult.exclusiveMessage));

    appendAudiodgModules(*detailsTree_, root, inspection.audiodgModules, inspection.audiodgModulesMessage);

    detailsTree_->Expand(root);
    detailsTree_->Expand(deviceInfo);
    detailsTree_->Expand(chain);
    detailsTree_->Expand(streamTests);
}

void MainFrame::appendLog(const wxString& message) {
    if (logText_ == nullptr) {
        return;
    }

    logText_->AppendText(message);
    logText_->AppendText("\n");
}

} // namespace audio_path_inspector::app