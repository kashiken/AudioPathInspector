#include "audio_path_inspector/windows/ApoReader.h"

namespace audio_path_inspector::windows {

std::vector<model::ApoInfo> ApoReader::readCaptureApos(
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    warningMessage.clear();

    if (endpointId.empty()) {
        warningMessage = L"APO reader skipped because endpoint ID is empty.";
        return {};
    }

    warningMessage = L"APO enumeration is not implemented yet.";
    return {};
}

} // namespace audio_path_inspector::windows
