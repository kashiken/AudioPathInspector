#pragma once

#include <string>

namespace audio_path_inspector::model {

struct ModuleInfo {
    unsigned long processId{0};
    std::wstring name;
    std::wstring path;
};

} // namespace audio_path_inspector::model