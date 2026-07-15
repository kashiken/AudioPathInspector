#pragma once

#include "audio_path_inspector/model/DeviceInspection.h"
#include "audio_path_inspector/model/DeviceSummary.h"

#include <string>
#include <vector>

namespace audio_path_inspector::audio {

class DeviceInspectionService final {
public:
    [[nodiscard]] std::vector<model::DeviceSummary> enumerateCaptureDevices(std::wstring& errorMessage) const;

    [[nodiscard]] model::DeviceInspection inspectCaptureDevice(
        const model::DeviceSummary& device,
        std::wstring& errorMessage) const;
};

} // namespace audio_path_inspector::audio
