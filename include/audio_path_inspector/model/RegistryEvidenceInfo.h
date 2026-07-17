#pragma once

#include <string>

namespace audio_path_inspector::model {

struct RegistryEvidenceInfo {
    std::wstring root;
    std::wstring path;
    std::wstring name;
    std::wstring type;
    std::wstring value;
    bool audioRelated{false};
};

} // namespace audio_path_inspector::model