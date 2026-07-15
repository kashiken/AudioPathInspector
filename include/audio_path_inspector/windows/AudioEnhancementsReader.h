#pragma once

#include "audio_path_inspector/model/AudioEnhancementsInfo.h"

#include <string>

namespace audio_path_inspector::windows {

class AudioEnhancementsReader final {
public:
    [[nodiscard]] model::AudioEnhancementsInfo readCaptureAudioEnhancements(
        const std::wstring& endpointId,
        std::wstring& warningMessage) const;
};

} // namespace audio_path_inspector::windows
