#include "app/App.h"

#include "app/MainFrame.h"

#include <wx/msgdlg.h>

#include <memory>

namespace audio_path_inspector::app {

App::~App() = default;

bool App::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    comApartment_ = std::make_unique<windows::ComApartment>(COINIT_APARTMENTTHREADED);
    if (!comApartment_->ok()) {
        wxMessageBox(
            "Failed to initialize COM apartment.",
            "Audio Path Inspector",
            wxOK | wxICON_ERROR);
        return false;
    }

    auto* frame = new MainFrame();
    frame->Show(true);
    return true;
}

int App::OnExit() {
    comApartment_.reset();
    return wxApp::OnExit();
}

} // namespace audio_path_inspector::app
