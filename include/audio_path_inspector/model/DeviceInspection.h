#pragma once

#include "audio_path_inspector/model/ApoInfo.h"
#include "audio_path_inspector/model/AudioEnhancementsInfo.h"
#include "audio_path_inspector/model/AudioEffectInfo.h"
#include "audio_path_inspector/model/DeviceDetails.h"
#include "audio_path_inspector/model/DeviceSummary.h"
#include "audio_path_inspector/model/ModuleInfo.h"
#include "audio_path_inspector/model/StreamOpenResult.h"

#include <string>
#include <vector>

namespace audio_path_inspector::model {

struct DeviceInspection {
    DeviceSummary summary;
    DeviceDetails details;
    std::vector<ApoInfo> apos;
    std::wstring apoMessage;
    std::vector<AudioEffectInfo> audioEffects;
    std::wstring audioEffectsMessage;
    AudioEnhancementsInfo audioEnhancements;
    std::wstring audioEnhancementsMessage;
    StreamOpenResult streamOpenResult;
    std::vector<ModuleInfo> audiodgModules;
    std::wstring audiodgModulesMessage;
    std::wstring warningMessage;
};

} // namespace audio_path_inspector::model
