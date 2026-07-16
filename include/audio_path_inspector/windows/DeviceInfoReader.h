#pragma once

#include "audio_path_inspector/model/DeviceDetails.h"
#include "audio_path_inspector/model/DeviceFlow.h"

#include <string>

namespace audio_path_inspector::windows {

class DeviceInfoReader final {
public:
    [[nodiscard]] model::DeviceDetails readDeviceDetails(
        model::DeviceFlow flow,
        const std::wstring& endpointId,
        std::wstring& errorMessage) const;

    [[nodiscard]] model::DeviceDetails readCaptureDeviceDetails(
        const std::wstring& endpointId,
        std::wstring& errorMessage) const;
};

} // namespace audio_path_inspector::windows
