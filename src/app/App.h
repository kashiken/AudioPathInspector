#pragma once

#include "audio_path_inspector/windows/ComApartment.h"

#include <wx/app.h>

#include <memory>

namespace audio_path_inspector::app {

class App final : public wxApp {
public:
    ~App() override;

    bool OnInit() override;
    int OnExit() override;

private:
    std::unique_ptr<windows::ComApartment> comApartment_;
};

} // namespace audio_path_inspector::app
