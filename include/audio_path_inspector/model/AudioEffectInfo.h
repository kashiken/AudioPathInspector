#pragma once

#include <string>

namespace audio_path_inspector::model {

struct AudioEffectInfo {
    std::wstring name;
    bool enabled{false};
    std::wstring status;
};

} // namespace audio_path_inspector::model
