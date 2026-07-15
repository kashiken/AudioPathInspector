#pragma once

#include <string>

namespace audio_path_inspector::model {

struct DeviceSummary {
    std::wstring friendlyName;
    std::wstring endpointId;
    std::wstring deviceId;
};

} // namespace audio_path_inspector::model
