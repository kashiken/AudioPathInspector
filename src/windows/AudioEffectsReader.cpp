#include "audio_path_inspector/windows/AudioEffectsReader.h"

namespace audio_path_inspector::windows {

std::vector<model::AudioEffectInfo> AudioEffectsReader::readCaptureAudioEffects(
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    warningMessage.clear();

    if (endpointId.empty()) {
        warningMessage = L"Audio Effects reader skipped because endpoint ID is empty.";
        return {};
    }

    warningMessage = L"Audio Effects enumeration is not implemented yet.";
    return {};
}

} // namespace audio_path_inspector::windows
