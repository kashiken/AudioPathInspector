#pragma once

#include <string>

namespace audio_path_inspector::model {

enum class ApoKind {
    StreamEffect,
    ModeEffect,
    EndpointEffect,
    Unknown
};

struct ApoInfo {
    ApoKind kind{ApoKind::Unknown};
    std::wstring clsid;
    std::wstring dllName;
    std::wstring dllPath;
    std::wstring vendor;
    std::wstring fileVersion;
};

} // namespace audio_path_inspector::model
