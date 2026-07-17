#pragma once

#include "audio_path_inspector/model/EndpointPropertyInfo.h"
#include "audio_path_inspector/model/RegistryEvidenceInfo.h"

#include <string>
#include <vector>

namespace audio_path_inspector::windows {

class RegistryEvidenceReader final {
public:
    [[nodiscard]] std::vector<model::RegistryEvidenceInfo> readRegistryEvidence(
        const std::wstring& endpointId,
        const std::vector<model::EndpointPropertyInfo>& endpointProperties,
        std::wstring& warningMessage) const;
};

} // namespace audio_path_inspector::windows