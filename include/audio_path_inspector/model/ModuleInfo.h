#pragma once

#include <string>

namespace audio_path_inspector::model {

struct ModuleInfo {
    unsigned long processId{0};
    std::wstring name;
    std::wstring path;
    bool audioProcessingCandidate{false};
    std::wstring candidateReason;
};

} // namespace audio_path_inspector::model