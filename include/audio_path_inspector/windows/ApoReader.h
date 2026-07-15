#pragma once

#include "audio_path_inspector/model/ApoInfo.h"

#include <string>
#include <vector>

namespace audio_path_inspector::windows {

class ApoReader final {
public:
    [[nodiscard]] std::vector<model::ApoInfo> readCaptureApos(
        const std::wstring& endpointId,
        std::wstring& warningMessage) const;
};

} // namespace audio_path_inspector::windows
