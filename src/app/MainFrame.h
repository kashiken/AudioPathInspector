#pragma once

#include "audio_path_inspector/model/DeviceFlow.h"
#include "audio_path_inspector/model/DeviceInspection.h"
#include "audio_path_inspector/model/DeviceSummary.h"

#include <wx/frame.h>

#include <atomic>
#include <memory>
#include <vector>

class wxChoice;
class wxListBox;
class wxTextCtrl;

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
    void displayDeviceDetails(
        model::DeviceFlow flow,
        const model::DeviceSummary& device,
        const model::DeviceInspection& inspection,
        const std::wstring& errorMessage);
    void appendLog(const wxString& message);

    wxChoice* flowChoice_{nullptr};
    wxListBox* deviceList_{nullptr};
    wxTextCtrl* detailsText_{nullptr};
    wxTextCtrl* logText_{nullptr};
    std::vector<model::DeviceSummary> devices_;
    model::DeviceFlow currentFlow_{model::DeviceFlow::Capture};
    unsigned int deviceRequestId_{0};
    unsigned int inspectionRequestId_{0};
    std::shared_ptr<std::atomic_bool> alive_{std::make_shared<std::atomic_bool>(true)};
};

} // namespace audio_path_inspector::app