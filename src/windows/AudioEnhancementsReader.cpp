#include "audio_path_inspector/windows/AudioEnhancementsReader.h"

namespace audio_path_inspector::windows {

model::AudioEnhancementsInfo AudioEnhancementsReader::readCaptureAudioEnhancements(
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    warningMessage.clear();

    model::AudioEnhancementsInfo info;

    if (endpointId.empty()) {
        info.state = model::AudioEnhancementsState::NotAvailable;
        info.detail = L"Endpoint ID is empty.";
        warningMessage = L"Audio Enhancements reader skipped because endpoint ID is empty.";
        return info;
    }

    info.state = model::AudioEnhancementsState::Unknown;
    info.detail = L"Audio Enhancements state is not implemented yet.";
    warningMessage = info.detail;
    return info;
}

} // namespace audio_path_inspector::windows
