#include "app/MainFrame.h"

#include "audio_path_inspector/audio/DeviceInspectionService.h"
#include "audio_path_inspector/windows/ComApartment.h"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <objbase.h>

#include <algorithm>
#include <cwctype>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace audio_path_inspector::app {

namespace {

constexpr int WindowWidth = 1040;
constexpr int WindowHeight = 700;
constexpr int LeftPaneWidth = 300;
constexpr int CopyDetailsButtonId = wxID_HIGHEST + 1;

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

void appendTreeText(wxTreeCtrl& tree, const wxTreeItemId& item, const int depth, wxString& output) {
    if (!item.IsOk()) {
        return;
    }

    output << wxString(' ', depth * 2) << tree.GetItemText(item) << "\n";

    wxTreeItemIdValue cookie;
    wxTreeItemId child = tree.GetFirstChild(item, cookie);
    while (child.IsOk()) {
        appendTreeText(tree, child, depth + 1, output);
        child = tree.GetNextChild(item, cookie);
    }
}

wxString treeToText(wxTreeCtrl& tree) {
    wxString output;
    const wxTreeItemId root = tree.GetRootItem();
    if (!root.IsOk()) {
        return output;
    }

    wxTreeItemIdValue cookie;
    wxTreeItemId child = tree.GetFirstChild(root, cookie);
    while (child.IsOk()) {
        appendTreeText(tree, child, 0, output);
        child = tree.GetNextChild(root, cookie);
    }
    return output;
}

bool containsText(const wxString& text, const wxString& filter) {
    return text.Lower().Find(filter.Lower()) != wxNOT_FOUND;
}

bool pruneTreeForFilter(wxTreeCtrl& tree, const wxTreeItemId& item, const wxString& filter) {
    bool keep = containsText(tree.GetItemText(item), filter);

    wxTreeItemIdValue cookie;
    wxTreeItemId child = tree.GetFirstChild(item, cookie);
    while (child.IsOk()) {
        const wxTreeItemId current = child;
        child = tree.GetNextChild(item, cookie);
        if (pruneTreeForFilter(tree, current, filter)) {
            keep = true;
        } else {
            tree.Delete(current);
        }
    }

    return keep;
}

void applyTreeFilter(wxTreeCtrl& tree, const wxString& filter) {
    if (filter.empty()) {
        return;
    }

    const wxTreeItemId root = tree.GetRootItem();
    if (!root.IsOk()) {
        return;
    }

    bool anyMatch = false;
    wxTreeItemIdValue cookie;
    wxTreeItemId child = tree.GetFirstChild(root, cookie);
    while (child.IsOk()) {
        const wxTreeItemId current = child;
        child = tree.GetNextChild(root, cookie);
        if (pruneTreeForFilter(tree, current, filter)) {
            anyMatch = true;
        } else {
            tree.Delete(current);
        }
    }

    if (!anyMatch) {
        appendItem(tree, root, "No details match current filter.");
    }
}

bool shouldCollapseByDefault(const wxString& label) {
    return label == "Registry Evidence"
        || label == "All endpoint properties"
        || label == "All collected registry values"
        || label == "All loaded modules";
}

void collapseDefaultClosedSections(wxTreeCtrl& tree, const wxTreeItemId& item) {
    if (!item.IsOk()) {
        return;
    }

    wxTreeItemIdValue cookie;
    wxTreeItemId child = tree.GetFirstChild(item, cookie);
    while (child.IsOk()) {
        collapseDefaultClosedSections(tree, child);
        child = tree.GetNextChild(item, cookie);
    }

    if (shouldCollapseByDefault(tree.GetItemText(item))) {
        tree.Collapse(item);
    }
}

bool labelContains(const wxString& label, const wxString& needle) {
    return label.Lower().Find(needle.Lower()) != wxNOT_FOUND;
}

wxString tooltipForTreeLabel(const wxString& label) {
    if (label == "Device") {
        return "Endpoint identity. Check Flow, Friendly Name, Endpoint ID, and Device ID when matching this result to Windows Settings or registry paths.";
    }
    if (label == "Mix Format") {
        return "The endpoint mix format exposed by Windows. Format, sample rate, channel count, and bit depth can affect which processing paths are available.";
    }
    if (label == "APO Chain (inferred)") {
        return "Best-effort chain inferred from known endpoint APO properties. This is not proof of actual runtime execution order.";
    }
    if (label == "Registered APOs") {
        return "APOs found through standard PKEY_FX_* and PKEY_CompositeFX_* endpoint properties. Empty here does not prove there is no vendor processing.";
    }
    if (label == "Registry FX / Software Component APO Candidates") {
        return "Best-effort APO clues from MMDevices FxProperties and Software Component registry evidence. This is the section to check when Registered APOs is empty.";
    }
    if (label == "Registry FX / Software Component Candidates") {
        return "Registry-derived chain hints inserted into APO Chain inference. Treat these as evidence-backed candidates, not confirmed runtime order.";
    }
    if (label == "Active Effects") {
        return "Effects reported by IAudioEffectsManager. Unsupported or empty results only mean Windows did not report effects through this API.";
    }
    if (label == "Audio Enhancements") {
        return "State inferred from endpoint enhancement properties. Vendor-specific controls may not appear here.";
    }
    if (label == "Stream Open Tests") {
        return "WASAPI open checks for shared, RAW, and exclusive modes. Differences can hint at endpoint or driver path behavior.";
    }
    if (label == "audiodg.exe Loaded DLLs") {
        return "Runtime module evidence from audiodg.exe. Candidate DLLs can support an APO hypothesis, but access may fail on protected systems.";
    }
    if (label == "Endpoint Properties") {
        return "All values exposed by the endpoint property store. Focus on audio/FX/APO candidates before expanding the full list.";
    }
    if (label == "Registry Evidence") {
        return "Read-only registry evidence collected around MMDevices, PnP device paths, and CLSID references. Useful for vendor/private registrations.";
    }
    if (label == "Audio / FX / APO related candidates") {
        return "Endpoint properties whose names or values look audio-processing related. These are higher-signal than the full endpoint property list.";
    }
    if (label == "Audio / FX / APO related registry values") {
        return "Registry values whose path, name, or value looks audio-processing related. Check FxProperties, CLSIDs, Software Components, and vendor names.";
    }
    if (label == "Audio processing module candidates") {
        return "Loaded audiodg.exe modules whose names or paths look audio-processing related. Look for vendor DLL names or APO/effect keywords.";
    }
    if (label == "All endpoint properties") {
        return "Complete endpoint property dump. This can be noisy, so it is collapsed by default.";
    }
    if (label == "All collected registry values") {
        return "Complete registry evidence dump. This can be noisy, so it is collapsed by default.";
    }
    if (label == "All loaded modules") {
        return "Complete audiodg.exe module list grouped by process ID. This can be noisy, so it is collapsed by default.";
    }
    if (label == "Stream Effects") {
        return "APOs registered as stream effects. These usually sit closest to the audio stream.";
    }
    if (label == "Mode Effects") {
        return "APOs registered for audio processing modes. These may vary by default, communications, media, speech, or raw modes.";
    }
    if (label == "Endpoint Effects") {
        return "APOs registered at endpoint scope. These can represent device-level processing.";
    }
    if (label.StartsWith("CLSID")) {
        return "COM class ID for an APO, effect, property set, or related component. A resolvable APO CLSID should usually have HKCR\\CLSID registration.";
    }
    if (label.StartsWith("DLL") || label.StartsWith("Path")) {
        return "File path evidence. Vendor DLL paths are strong clues; missing paths often mean only a registry/property reference was found.";
    }
    if (label.StartsWith("Candidate reason")) {
        return "Why this module was highlighted as audio-processing related.";
    }
    if (label.StartsWith("APO Notifications")) {
        return "Whether the COM object supports APO notification interfaces. Useful for newer APO implementations, but absence is not conclusive.";
    }
    if (labelContains(label, "FxProperties")) {
        return "MMDevices FxProperties registry data. For vendor APOs, this can contain CLSID-like values or Software Component links even when standard endpoint properties are empty.";
    }
    if (labelContains(label, "INZONE_APO") || labelContains(label, "_APO")) {
        return "Software Component APO evidence. This is a strong clue that a vendor APO-related component is installed or associated with the endpoint.";
    }
    if (labelContains(label, "SWC\\VEN_") || labelContains(label, "SWD\\DRIVERENUM")) {
        return "Software Component or driver-enumerated device reference. Follow these values when vendor APOs are not exposed through standard PKEY_FX_* properties.";
    }
    if (labelContains(label, "PKEY_FX") || labelContains(label, "PKEY_CompositeFX")) {
        return "Standard Windows endpoint APO registration property. These are the cleanest inputs for Registered APOs and APO Chain inference.";
    }
    if (labelContains(label, "PKEY_AudioEndpoint_Disable_SysFx")) {
        return "Endpoint flag related to system effects/enhancements. If unavailable, Windows did not expose this setting for the endpoint.";
    }
    if (labelContains(label, "IAudioEffectsManager") || labelContains(label, "0x80004002")) {
        return "Windows Audio Effects API result. 0x80004002 usually means this endpoint does not expose IAudioEffectsManager.";
    }
    if (labelContains(label, "OpenProcess(audiodg.exe") || labelContains(label, "Win32 error=5")) {
        return "audiodg.exe module enumeration failed due to access denial. This limits runtime DLL evidence but does not rule out APO processing.";
    }
    if (labelContains(label, "Shared RAW")) {
        return "RAW shared-mode open test. OK means the endpoint accepts RAW mode; it does not guarantee all vendor or hardware processing is bypassed.";
    }
    if (labelContains(label, "Exclusive")) {
        return "Exclusive-mode open test. Failure can be normal if another client owns the endpoint or the format is not accepted.";
    }

    return "Diagnostic item. Use the parent section tooltip for context, and focus on vendor names, CLSIDs, FxProperties, Software Component links, and HRESULTs.";
}

bool looksLikeGuidString(const std::wstring& value, const std::size_t start) {
    if (start + 38 > value.size() || value[start] != L'{' || value[start + 37] != L'}') {
        return false;
    }

    for (std::size_t index = 1; index < 37; ++index) {
        const wchar_t ch = value[start + index];
        const bool dash = index == 9 || index == 14 || index == 19 || index == 24;
        if (dash) {
            if (ch != L'-') {
                return false;
            }
            continue;
        }
        const bool hex = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F');
        if (!hex) {
            return false;
        }
    }

    return true;
}

std::vector<std::wstring> extractGuidStrings(const std::wstring& value) {
    std::set<std::wstring> guids;
    std::size_t position = 0;
    while (true) {
        const std::size_t start = value.find(L'{', position);
        if (start == std::wstring::npos) {
            break;
        }
        if (looksLikeGuidString(value, start)) {
            guids.insert(value.substr(start, 38));
            position = start + 38;
        } else {
            position = start + 1;
        }
    }
    return {guids.begin(), guids.end()};
}

bool containsInsensitive(const std::wstring& value, const std::wstring& needle) {
    std::wstring haystack = value;
    std::wstring loweredNeedle = needle;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::towlower);
    std::transform(loweredNeedle.begin(), loweredNeedle.end(), loweredNeedle.begin(), ::towlower);
    return haystack.find(loweredNeedle) != std::wstring::npos;
}

bool isRegistryFxCandidate(const model::RegistryEvidenceInfo& item) {
    return containsInsensitive(item.path, L"\\FxProperties")
        || containsInsensitive(item.path, L"_APO")
        || containsInsensitive(item.path, L"#INZONE_APO")
        || containsInsensitive(item.value, L"_APO")
        || containsInsensitive(item.value, L"SWC\\VEN_")
        || containsInsensitive(item.value, L"SWD\\DRIVERENUM");
}

void appendRegistryFxCandidates(wxTreeCtrl& tree, const wxTreeItemId& parent, const std::vector<model::RegistryEvidenceInfo>& evidence) {
    const wxTreeItemId section = appendItem(tree, parent, "Registry FX / Software Component APO Candidates");
    appendItem(tree, section, "Best-effort candidates derived from MMDevices FxProperties and related Software Component registry evidence.");

    bool found = false;
    for (const model::RegistryEvidenceInfo& item : evidence) {
        if (!isRegistryFxCandidate(item)) {
            continue;
        }

        found = true;
        const wxString valueName = item.name.empty() ? "(default)" : toWxString(item.name);
        wxString label = valueName + " = " + fallbackString(item.value, L"(empty)");
        if (containsInsensitive(item.path, L"_APO") || containsInsensitive(item.value, L"_APO")) {
            label << " [software component]";
        } else if (!extractGuidStrings(item.value).empty()) {
            label << " [CLSID/property set candidate]";
        }

        const wxTreeItemId node = appendItem(tree, section, label);
        appendKeyValue(tree, node, "Root", fallbackString(item.root, L"Unknown"));
        appendKeyValue(tree, node, "Path", fallbackString(item.path, L"Unknown"));
        appendKeyValue(tree, node, "Name", valueName);
        appendKeyValue(tree, node, "Type", fallbackString(item.type, L"Unknown"));

        const std::vector<std::wstring> guids = extractGuidStrings(item.value);
        if (!guids.empty()) {
            const wxTreeItemId guidNode = appendItem(tree, node, "GUID values found in value");
            for (const std::wstring& guid : guids) {
                appendItem(tree, guidNode, toWxString(guid));
            }
        }
    }

    if (!found) {
        appendItem(tree, section, "No registry FX or Software Component APO candidates were detected.");
    }
}

void appendRegistryFxCandidateChainGroup(wxTreeCtrl& tree, const wxTreeItemId& parent, const std::vector<model::RegistryEvidenceInfo>& evidence) {
    const wxTreeItemId group = appendItem(tree, parent, "Registry FX / Software Component Candidates");
    appendItem(tree, group, "Best-effort chain hints from FxProperties and Software Component evidence. These are not standard PKEY_FX registrations.");

    bool found = false;
    int index = 1;
    for (const model::RegistryEvidenceInfo& item : evidence) {
        if (!isRegistryFxCandidate(item)) {
            continue;
        }

        found = true;
        const wxString valueName = item.name.empty() ? "(default)" : toWxString(item.name);
        wxString label;
        label << index << ". " << valueName;
        if (!item.value.empty()) {
            label << " = " << toWxString(item.value);
        }
        if (containsInsensitive(item.path, L"_APO") || containsInsensitive(item.value, L"_APO")) {
            label << " [software component]";
        } else if (!extractGuidStrings(item.value).empty()) {
            label << " [CLSID/property set candidate]";
        } else if (containsInsensitive(item.path, L"\\FxProperties")) {
            label << " [FxProperties]";
        }

        const wxTreeItemId node = appendItem(tree, group, label);
        appendKeyValue(tree, node, "Evidence path", fallbackString(item.path, L"Unknown"));
        appendKeyValue(tree, node, "Evidence name", valueName);
        appendKeyValue(tree, node, "Evidence type", fallbackString(item.type, L"Unknown"));

        const std::vector<std::wstring> guids = extractGuidStrings(item.value);
        if (!guids.empty()) {
            const wxTreeItemId guidNode = appendItem(tree, node, "GUID values found in value");
            for (const std::wstring& guid : guids) {
                appendItem(tree, guidNode, toWxString(guid));
            }
        }
        ++index;
    }

    if (!found) {
        appendItem(tree, group, "(none inferred from registry evidence)");
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


void appendEndpointProperties(wxTreeCtrl& tree, const wxTreeItemId& parent, const std::vector<model::EndpointPropertyInfo>& properties, const std::wstring& message) {
    const wxTreeItemId section = appendItem(tree, parent, "Endpoint Properties");
    appendMessage(tree, section, message);

    if (properties.empty()) {
        appendItem(tree, section, "No endpoint properties were listed.");
        return;
    }

    const wxTreeItemId related = appendItem(tree, section, "Audio / FX / APO related candidates");
    const wxTreeItemId all = appendItem(tree, section, "All endpoint properties");

    bool hasRelated = false;
    for (const model::EndpointPropertyInfo& property : properties) {
        const wxString displayName = property.name.empty() ? toWxString(property.key) : toWxString(property.name);
        const wxString label = displayName + " = " + fallbackString(property.value, L"(empty)");
        const wxTreeItemId parentNode = property.audioRelated ? related : all;
        const wxTreeItemId node = appendItem(tree, parentNode, label);
        appendKeyValue(tree, node, "Key", fallbackString(property.key, L"Unknown"));
        appendKeyValue(tree, node, "Type", fallbackString(property.valueType, L"Unknown"));
        if (!property.name.empty()) {
            appendKeyValue(tree, node, "Name", toWxString(property.name));
        }
        if (property.audioRelated) {
            hasRelated = true;
        }
    }

    if (!hasRelated) {
        appendItem(tree, related, "No audio/FX/APO-looking properties were detected by keyword scan.");
    }
}
void appendModuleNode(wxTreeCtrl& tree, const wxTreeItemId& parent, const model::ModuleInfo& module) {
    wxString label = fallbackString(module.name, L"Unknown module");
    if (module.audioProcessingCandidate) {
        label << " [candidate]";
    }

    const wxTreeItemId moduleNode = appendItem(tree, parent, label);
    appendKeyValue(tree, moduleNode, "PID", wxString::Format("%lu", module.processId));
    appendKeyValue(tree, moduleNode, "Path", fallbackString(module.path, L"Unknown"));
    if (!module.candidateReason.empty()) {
        appendKeyValue(tree, moduleNode, "Candidate reason", toWxString(module.candidateReason));
    }
}


void appendRegistryEvidence(wxTreeCtrl& tree, const wxTreeItemId& parent, const std::vector<model::RegistryEvidenceInfo>& evidence, const std::wstring& message) {
    const wxTreeItemId section = appendItem(tree, parent, "Registry Evidence");
    appendMessage(tree, section, message);

    if (evidence.empty()) {
        appendItem(tree, section, "No registry evidence was listed.");
        return;
    }

    const wxTreeItemId related = appendItem(tree, section, "Audio / FX / APO related registry values");
    const wxTreeItemId all = appendItem(tree, section, "All collected registry values");

    bool hasRelated = false;
    for (const model::RegistryEvidenceInfo& item : evidence) {
        const wxString valueName = item.name.empty() ? "(default)" : toWxString(item.name);
        const wxString label = toWxString(item.root + L"\\" + item.path) + " | " + valueName + " = " + fallbackString(item.value, L"(empty)");
        const wxTreeItemId parentNode = item.audioRelated ? related : all;
        const wxTreeItemId node = appendItem(tree, parentNode, label);
        appendKeyValue(tree, node, "Root", fallbackString(item.root, L"Unknown"));
        appendKeyValue(tree, node, "Path", fallbackString(item.path, L"Unknown"));
        appendKeyValue(tree, node, "Name", valueName);
        appendKeyValue(tree, node, "Type", fallbackString(item.type, L"Unknown"));
        if (item.audioRelated) {
            hasRelated = true;
        }
    }

    if (!hasRelated) {
        appendItem(tree, related, "No audio/FX/APO-looking registry values were detected by keyword scan.");
    }
}
void appendAudiodgModules(wxTreeCtrl& tree, const wxTreeItemId& parent, const std::vector<model::ModuleInfo>& modules, const std::wstring& message) {
    const wxTreeItemId section = appendItem(tree, parent, "audiodg.exe Loaded DLLs");
    appendMessage(tree, section, message);

    if (modules.empty()) {
        appendItem(tree, section, "No modules were listed.");
        return;
    }

    const wxTreeItemId candidates = appendItem(tree, section, "Audio processing module candidates");
    bool hasCandidate = false;
    for (const model::ModuleInfo& module : modules) {
        if (module.audioProcessingCandidate) {
            appendModuleNode(tree, candidates, module);
            hasCandidate = true;
        }
    }
    if (!hasCandidate) {
        appendItem(tree, candidates, "No APO/audio-looking loaded modules were detected by keyword scan.");
    }

    const wxTreeItemId allModules = appendItem(tree, section, "All loaded modules");
    unsigned long currentPid = 0;
    wxTreeItemId processNode;
    for (const model::ModuleInfo& module : modules) {
        if (module.processId != currentPid || !processNode.IsOk()) {
            currentPid = module.processId;
            wxString processLabel;
            processLabel << "audiodg.exe pid " << currentPid;
            processNode = appendItem(tree, allModules, processLabel);
        }
        appendModuleNode(tree, processNode, module);
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
    toolbarSizer->Add(new wxStaticText(rootPanel, wxID_ANY, "Filter"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    detailsFilterText_ = new wxTextCtrl(rootPanel, wxID_ANY, "", wxDefaultPosition, wxSize(220, -1));
    toolbarSizer->Add(detailsFilterText_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    copyDetailsButton_ = new wxButton(rootPanel, CopyDetailsButtonId, "Copy Details");
    toolbarSizer->Add(copyDetailsButton_, 0, wxALL, 6);
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
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { copyDetailsToClipboard(); }, CopyDetailsButtonId);
    flowChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        updateCurrentFlowFromUi();
        refreshDevices();
    });
    if (detailsTree_ != nullptr) {
        detailsTree_->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) { updateDetailsTooltip(event); });
        detailsTree_->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
            currentDetailsTooltip_.clear();
            detailsTree_->UnsetToolTip();
        });
    }
    if (detailsFilterText_ != nullptr) {
        detailsFilterText_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
            if (hasDisplayedInspection_) {
                renderCurrentDeviceDetails(false);
            }
        });
    }
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
    hasDisplayedInspection_ = false;
    if (detailsTree_ == nullptr) {
        return;
    }

    detailsTree_->DeleteAllItems();
    const wxTreeItemId root = detailsTree_->AddRoot("Details");
    detailsTree_->AppendItem(root, message);
    detailsTree_->ExpandAll();
}

void MainFrame::updateDetailsTooltip(wxMouseEvent& event) {
    if (detailsTree_ == nullptr) {
        event.Skip();
        return;
    }

    int flags = 0;
    const wxTreeItemId item = detailsTree_->HitTest(event.GetPosition(), flags);
    wxString tooltip;
    if (item.IsOk()) {
        tooltip = tooltipForTreeLabel(detailsTree_->GetItemText(item));
    }

    if (tooltip != currentDetailsTooltip_) {
        currentDetailsTooltip_ = tooltip;
        if (tooltip.empty()) {
            detailsTree_->UnsetToolTip();
        } else {
            detailsTree_->SetToolTip(tooltip);
        }
    }

    event.Skip();
}
void MainFrame::copyDetailsToClipboard() {
    if (detailsTree_ == nullptr) {
        return;
    }

    wxString text = treeToText(*detailsTree_);
    if (logText_ != nullptr && !logText_->GetValue().empty()) {
        if (!text.empty()) {
            text << "\n";
        }
        text << "Log\n" << logText_->GetValue();
    }

    if (text.empty()) {
        appendLog("Nothing to copy.");
        return;
    }

    if (wxTheClipboard == nullptr || !wxTheClipboard->Open()) {
        appendLog("Failed to open clipboard.");
        return;
    }

    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Close();
    appendLog("Details copied to clipboard.");
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
    displayedFlow_ = flow;
    displayedDevice_ = device;
    displayedInspection_ = inspection;
    displayedErrorMessage_ = errorMessage;
    hasDisplayedInspection_ = true;
    renderCurrentDeviceDetails(true);
}

void MainFrame::renderCurrentDeviceDetails(const bool appendErrorMessage) {
    if (detailsTree_ == nullptr || !hasDisplayedInspection_) {
        return;
    }

    const model::DeviceFlow flow = displayedFlow_;
    const model::DeviceSummary& device = displayedDevice_;
    const model::DeviceInspection& inspection = displayedInspection_;
    const model::DeviceDetails& details = inspection.details;
    const model::StreamOpenResult& streamOpenResult = inspection.streamOpenResult;

    if (appendErrorMessage && !displayedErrorMessage_.empty()) {
        appendLog(toWxString(displayedErrorMessage_));
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
    appendRegistryFxCandidateChainGroup(*detailsTree_, chain, inspection.registryEvidence);
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

    appendRegistryFxCandidates(*detailsTree_, root, inspection.registryEvidence);

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
    appendEndpointProperties(*detailsTree_, root, inspection.endpointProperties, inspection.endpointPropertiesMessage);
    appendRegistryEvidence(*detailsTree_, root, inspection.registryEvidence, inspection.registryEvidenceMessage);

    const wxString filter = detailsFilterText_ == nullptr ? wxString() : detailsFilterText_->GetValue();
    applyTreeFilter(*detailsTree_, filter);

    detailsTree_->ExpandAll();
    if (filter.empty()) {
        collapseDefaultClosedSections(*detailsTree_, root);
    }
}
void MainFrame::appendLog(const wxString& message) {
    if (logText_ == nullptr) {
        return;
    }

    logText_->AppendText(message);
    logText_->AppendText("\n");
}

} // namespace audio_path_inspector::app
