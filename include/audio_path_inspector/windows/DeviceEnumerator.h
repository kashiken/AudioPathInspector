#pragma once

#include "audio_path_inspector/model/DeviceFlow.h"
#include "audio_path_inspector/model/DeviceSummary.h"

#include <string>
#include <vector>

namespace audio_path_inspector::windows {

class DeviceEnumerator final {
public:
    [[nodiscard]] std::vector<model::DeviceSummary> enumerateDevices(
        model::DeviceFlow flow,
        std::wstring& errorMessage) const;

    [[nodiscard]] std::vector<model::DeviceSummary> enumerateCaptureDevices(std::wstring& errorMessage) const;
};

} // namespace audio_path_inspector::windows
