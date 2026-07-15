#pragma once

#include <string>

namespace audio_path_inspector::model {

struct StreamOpenResult {
    bool sharedOk{false};
    bool rawOk{false};
    bool exclusiveOk{false};
    std::wstring sharedMessage;
    std::wstring rawMessage;
    std::wstring exclusiveMessage;
};

} // namespace audio_path_inspector::model
