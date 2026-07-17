#pragma once

#include "audio_path_inspector/model/EndpointPropertyInfo.h"

#include <string>
#include <vector>

namespace audio_path_inspector::windows {

class EndpointPropertyReader final {
public:
    [[nodiscard]] std::vector<model::EndpointPropertyInfo> readEndpointProperties(
        const std::wstring& endpointId,
        std::wstring& warningMessage) const;
};

} // namespace audio_path_inspector::windows