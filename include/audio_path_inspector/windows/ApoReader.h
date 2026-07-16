#pragma once

#include "audio_path_inspector/model/ApoInfo.h"
#include "audio_path_inspector/model/DeviceFlow.h"

#include <string>
#include <vector>

namespace audio_path_inspector::windows {

class ApoReader final {
public:
    [[nodiscard]] std::vector<model::ApoInfo> readApos(
        model::DeviceFlow flow,
        const std::wstring& endpointId,
        std::wstring& warningMessage) const;

    [[nodiscard]] std::vector<model::ApoInfo> readCaptureApos(
        const std::wstring& endpointId,
        std::wstring& warningMessage) const;
};

} // namespace audio_path_inspector::windows
