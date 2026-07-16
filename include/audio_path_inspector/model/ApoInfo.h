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
    std::wstring propertyLabel;
    std::wstring clsid;
    std::wstring dllName;
    std::wstring dllPath;
    std::wstring vendor;
    std::wstring fileVersion;
    bool notificationsSupported{false};
    bool notifications2Supported{false};
    std::wstring notificationsMessage;
};

} // namespace audio_path_inspector::model
