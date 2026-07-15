#pragma once

#include "audio_path_inspector/model/AudioEffectInfo.h"

#include <string>
#include <vector>

namespace audio_path_inspector::windows {

class AudioEffectsReader final {
public:
    [[nodiscard]] std::vector<model::AudioEffectInfo> readCaptureAudioEffects(
        const std::wstring& endpointId,
        std::wstring& warningMessage) const;
};

} // namespace audio_path_inspector::windows
