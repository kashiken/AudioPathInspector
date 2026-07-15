#pragma once

#include "audio_path_inspector/model/DeviceSummary.h"

#include <wx/frame.h>

#include <vector>

class wxListBox;
class wxTextCtrl;

namespace audio_path_inspector::app {

class MainFrame final : public wxFrame {
public:
    MainFrame();

private:
    void buildLayout();
    void bindEvents();
    void refreshDevices();
    void showDeviceDetails(std::size_t index);
    void appendLog(const wxString& message);

    wxListBox* deviceList_{nullptr};
    wxTextCtrl* detailsText_{nullptr};
    wxTextCtrl* logText_{nullptr};
    std::vector<model::DeviceSummary> devices_;
};

} // namespace audio_path_inspector::app
