#pragma once

#include <string>

namespace audio_path_inspector::model {

struct EndpointPropertyInfo {
    std::wstring key;
    std::wstring name;
    std::wstring value;
    std::wstring valueType;
    bool audioRelated{false};
};

} // namespace audio_path_inspector::model