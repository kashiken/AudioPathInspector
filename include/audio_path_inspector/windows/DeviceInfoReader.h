#pragma once

#include "audio_path_inspector/model/DeviceDetails.h"

#include <string>

namespace audio_path_inspector::windows {

class DeviceInfoReader final {
public:
    [[nodiscard]] model::DeviceDetails readCaptureDeviceDetails(
        const std::wstring& endpointId,
        std::wstring& errorMessage) const;
};

} // namespace audio_path_inspector::windows
