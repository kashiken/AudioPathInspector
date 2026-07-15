#pragma once

#include <string>

namespace audio_path_inspector::model {

enum class AudioEnhancementsState {
    On,
    Off,
    Unknown,
    NotAvailable
};

struct AudioEnhancementsInfo {
    AudioEnhancementsState state{AudioEnhancementsState::Unknown};
    std::wstring detail;
};

} // namespace audio_path_inspector::model
