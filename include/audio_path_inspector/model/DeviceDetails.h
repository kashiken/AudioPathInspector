#pragma once

#include <cstdint>
#include <string>

namespace audio_path_inspector::model {

struct DeviceDetails {
    std::wstring friendlyName;
    std::wstring endpointId;
    std::wstring deviceDescription;
    std::wstring manufacturer;
    std::wstring driverName;
    std::wstring driverVersion;
    std::uint32_t sampleRate{0};
    std::uint16_t channelCount{0};
    std::uint16_t bitsPerSample{0};
    std::wstring formatTag;
};

} // namespace audio_path_inspector::model
