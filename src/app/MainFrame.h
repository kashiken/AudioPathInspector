#pragma once

#include "audio_path_inspector/model/DeviceFlow.h"
#include "audio_path_inspector/model/DeviceInspection.h"
#include "audio_path_inspector/model/DeviceSummary.h"

#include <wx/frame.h>

#include <atomic>
#include <memory>
#include <vector>

class wxButton;
class wxChoice;
class wxListBox;
class wxTextCtrl;
class wxTreeCtrl;

namespace audio_path_inspector::app {

class MainFrame final : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    void buildLayout();
    void bindEvents();
    void refreshDevices();
    void updateCurrentFlowFromUi();
    void showDeviceDetails(std::size_t index);
    void showDetailsMessage(const wxString& message);
    void copyDetailsToClipboard();
    void updateDetailsTooltip(wxMouseEvent& event);
    void displayDeviceDetails(
        model::DeviceFlow flow,
        const model::DeviceSummary& device,
        const model::DeviceInspection& inspection,
        const std::wstring& errorMessage);
    void renderCurrentDeviceDetails(bool appendErrorMessage);
    void appendLog(const wxString& message);

    wxButton* copyDetailsButton_{nullptr};
    wxChoice* flowChoice_{nullptr};
    wxListBox* deviceList_{nullptr};
    wxTreeCtrl* detailsTree_{nullptr};
    wxTextCtrl* detailsFilterText_{nullptr};
    wxTextCtrl* logText_{nullptr};
    std::vector<model::DeviceSummary> devices_;
    bool hasDisplayedInspection_{false};
    model::DeviceFlow displayedFlow_{model::DeviceFlow::Capture};
    model::DeviceSummary displayedDevice_;
    model::DeviceInspection displayedInspection_;
    std::wstring displayedErrorMessage_;
    wxString currentDetailsTooltip_;
    model::DeviceFlow currentFlow_{model::DeviceFlow::Capture};
    unsigned int deviceRequestId_{0};
    unsigned int inspectionRequestId_{0};
    std::shared_ptr<std::atomic_bool> alive_{std::make_shared<std::atomic_bool>(true)};
};

} // namespace audio_path_inspector::app
