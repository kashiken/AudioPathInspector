#include "audio_path_inspector/windows/ApoReader.h"

#include <initguid.h>
#include <Windows.h>
#include <audioenginebaseapo.h>
#include <audioengineextensionapo.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace audio_path_inspector::windows {

namespace {

using Microsoft::WRL::ComPtr;

struct ApoProperty {
    PROPERTYKEY key;
    model::ApoKind kind;
    const wchar_t* label;
};

std::wstring formatHresult(const wchar_t* operation, const HRESULT hr) {
    std::wstringstream stream;
    stream << operation << L" failed. HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
    return stream.str();
}

std::wstring guidToString(const GUID& guid) {
    wchar_t buffer[39] = {};
    if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) {
        return L"";
    }
    return buffer;
}

bool tryParseGuid(const std::wstring& value, GUID& guid) {
    return SUCCEEDED(CLSIDFromString(value.c_str(), &guid));
}

std::vector<GUID> readGuidList(IPropertyStore& store, const PROPERTYKEY& key) {
    PROPVARIANT value;
    PropVariantInit(&value);

    std::vector<GUID> result;
    const HRESULT hr = store.GetValue(key, &value);
    if (FAILED(hr)) {
        PropVariantClear(&value);
        return result;
    }

    if (value.vt == VT_CLSID && value.puuid != nullptr) {
        result.push_back(*value.puuid);
    } else if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        GUID guid{};
        if (tryParseGuid(value.pwszVal, guid)) {
            result.push_back(guid);
        }
    } else if (value.vt == (VT_VECTOR | VT_LPWSTR)) {
        for (ULONG index = 0; index < value.calpwstr.cElems; ++index) {
            if (value.calpwstr.pElems[index] == nullptr) {
                continue;
            }
            GUID guid{};
            if (tryParseGuid(value.calpwstr.pElems[index], guid)) {
                result.push_back(guid);
            }
        }
    }

    PropVariantClear(&value);
    return result;
}

std::wstring readRegistryString(HKEY root, const std::wstring& subKey, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD bytes = 0;
    LONG status = RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || bytes == 0) {
        return L"";
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    status = RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, value.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return L"";
    }

    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }

    if (type == REG_EXPAND_SZ) {
        const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (needed > 0) {
            std::wstring expanded(needed, L'\0');
            ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
            while (!expanded.empty() && expanded.back() == L'\0') {
                expanded.pop_back();
            }
            return expanded;
        }
    }

    return value;
}

std::wstring baseName(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring queryVersionString(void* versionData, const WORD language, const WORD codePage, const wchar_t* name) {
    wchar_t subBlock[80] = {};
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\%s", language, codePage, name);

    void* value = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(versionData, subBlock, &value, &length) || value == nullptr || length == 0) {
        return L"";
    }

    return static_cast<const wchar_t*>(value);
}

struct FileMetadata {
    std::wstring vendor;
    std::wstring fileVersion;
};

FileMetadata readFileMetadata(const std::wstring& path) {
    FileMetadata metadata;
    if (path.empty()) {
        return metadata;
    }

    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return metadata;
    }

    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
        return metadata;
    }

    struct Translation {
        WORD language;
        WORD codePage;
    };

    Translation* translations = nullptr;
    UINT translationBytes = 0;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationBytes)
        && translations != nullptr
        && translationBytes >= sizeof(Translation)) {
        metadata.vendor = queryVersionString(data.data(), translations[0].language, translations[0].codePage, L"CompanyName");
        metadata.fileVersion = queryVersionString(data.data(), translations[0].language, translations[0].codePage, L"FileVersion");
    }

    if (metadata.fileVersion.empty()) {
        VS_FIXEDFILEINFO* fixedInfo = nullptr;
        UINT fixedInfoBytes = 0;
        if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &fixedInfoBytes)
            && fixedInfo != nullptr
            && fixedInfoBytes >= sizeof(VS_FIXEDFILEINFO)) {
            std::wstringstream stream;
            stream << HIWORD(fixedInfo->dwFileVersionMS) << L'.'
                   << LOWORD(fixedInfo->dwFileVersionMS) << L'.'
                   << HIWORD(fixedInfo->dwFileVersionLS) << L'.'
                   << LOWORD(fixedInfo->dwFileVersionLS);
            metadata.fileVersion = stream.str();
        }
    }

    return metadata;
}
void fillNotificationSupport(const GUID& guid, model::ApoInfo& info) {
    ComPtr<IUnknown> instance;
    HRESULT hr = CoCreateInstance(guid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&instance));
    if (FAILED(hr)) {
        info.notificationsMessage = formatHresult(L"CoCreateInstance(APO)", hr);
        return;
    }

    ComPtr<IAudioProcessingObjectNotifications> notifications;
    hr = instance.As(&notifications);
    if (FAILED(hr)) {
        info.notificationsMessage = L"IAudioProcessingObjectNotifications is not supported.";
        return;
    }

    info.notificationsSupported = true;

    APO_NOTIFICATION_DESCRIPTOR* descriptors = nullptr;
    DWORD count = 0;
    hr = notifications->GetApoNotificationRegistrationInfo(&descriptors, &count);
    if (SUCCEEDED(hr)) {
        std::wstringstream stream;
        stream << L"IAudioProcessingObjectNotifications supported. Registration descriptors: " << count;
        info.notificationsMessage = stream.str();
        CoTaskMemFree(descriptors);
    } else {
        info.notificationsMessage = formatHresult(L"IAudioProcessingObjectNotifications::GetApoNotificationRegistrationInfo", hr);
    }

    ComPtr<IAudioProcessingObjectNotifications2> notifications2;
    hr = instance.As(&notifications2);
    if (SUCCEEDED(hr)) {
        info.notifications2Supported = true;
    }
}

ComPtr<IMMDevice> getDevice(const std::wstring& endpointId, std::wstring& warningMessage) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        warningMessage = formatHresult(L"CoCreateInstance(MMDeviceEnumerator)", hr);
        return nullptr;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(endpointId.c_str(), &device);
    if (FAILED(hr)) {
        warningMessage = formatHresult(L"IMMDeviceEnumerator::GetDevice", hr);
        return nullptr;
    }

    return device;
}

void addAposFromProperty(IPropertyStore& store, const ApoProperty& property, std::vector<model::ApoInfo>& apos) {
    for (const GUID& guid : readGuidList(store, property.key)) {
        const std::wstring clsid = guidToString(guid);
        const std::wstring clsidKey = L"CLSID\\" + clsid;
        const std::wstring dllPath = readRegistryString(HKEY_CLASSES_ROOT, clsidKey + L"\\InprocServer32", nullptr);
        const FileMetadata metadata = readFileMetadata(dllPath);

        model::ApoInfo info;
        info.kind = property.kind;
        info.propertyLabel = property.label;
        info.clsid = clsid;
        info.dllPath = dllPath;
        info.dllName = dllPath.empty() ? property.label : baseName(dllPath);
        info.vendor = metadata.vendor;
        info.fileVersion = metadata.fileVersion;
        fillNotificationSupport(guid, info);
        apos.push_back(std::move(info));
    }
}

} // namespace

std::vector<model::ApoInfo> ApoReader::readApos(
    const model::DeviceFlow flow,
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    static_cast<void>(flow);
    warningMessage.clear();

    if (endpointId.empty()) {
        warningMessage = L"APO reader skipped because endpoint ID is empty.";
        return {};
    }

    ComPtr<IMMDevice> device = getDevice(endpointId, warningMessage);
    if (!device) {
        return {};
    }

    ComPtr<IPropertyStore> store;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr)) {
        warningMessage = formatHresult(L"IMMDevice::OpenPropertyStore", hr);
        return {};
    }

    const std::array<ApoProperty, 12> properties{{
        {PKEY_FX_StreamEffectClsid, model::ApoKind::StreamEffect, L"Stream Effect"},
        {PKEY_CompositeFX_StreamEffectClsid, model::ApoKind::StreamEffect, L"Composite Stream Effect"},
        {PKEY_FX_KeywordDetector_StreamEffectClsid, model::ApoKind::StreamEffect, L"Keyword Detector Stream Effect"},
        {PKEY_CompositeFX_KeywordDetector_StreamEffectClsid, model::ApoKind::StreamEffect, L"Composite Keyword Detector Stream Effect"},
        {PKEY_FX_ModeEffectClsid, model::ApoKind::ModeEffect, L"Mode Effect"},
        {PKEY_CompositeFX_ModeEffectClsid, model::ApoKind::ModeEffect, L"Composite Mode Effect"},
        {PKEY_FX_KeywordDetector_ModeEffectClsid, model::ApoKind::ModeEffect, L"Keyword Detector Mode Effect"},
        {PKEY_CompositeFX_KeywordDetector_ModeEffectClsid, model::ApoKind::ModeEffect, L"Composite Keyword Detector Mode Effect"},
        {PKEY_FX_EndpointEffectClsid, model::ApoKind::EndpointEffect, L"Endpoint Effect"},
        {PKEY_CompositeFX_EndpointEffectClsid, model::ApoKind::EndpointEffect, L"Composite Endpoint Effect"},
        {PKEY_FX_KeywordDetector_EndpointEffectClsid, model::ApoKind::EndpointEffect, L"Keyword Detector Endpoint Effect"},
        {PKEY_CompositeFX_KeywordDetector_EndpointEffectClsid, model::ApoKind::EndpointEffect, L"Composite Keyword Detector Endpoint Effect"},
    }};

    std::vector<model::ApoInfo> apos;
    for (const ApoProperty& property : properties) {
        addAposFromProperty(*store.Get(), property, apos);
    }

    return apos;
}


std::vector<model::ApoInfo> ApoReader::readCaptureApos(
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    return readApos(model::DeviceFlow::Capture, endpointId, warningMessage);
}

} // namespace audio_path_inspector::windows
