#pragma once

#include "audio_path_inspector/model/ModuleInfo.h"

#include <string>
#include <vector>

namespace audio_path_inspector::windows {

class AudiodgModuleReader final {
public:
    [[nodiscard]] std::vector<model::ModuleInfo> readLoadedModules(std::wstring& warningMessage) const;
};

} // namespace audio_path_inspector::windows