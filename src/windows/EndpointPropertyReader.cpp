#include "audio_path_inspector/windows/EndpointPropertyReader.h"

#include <Windows.h>
#include <propsys.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <audioenginebaseapo.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

namespace audio_path_inspector::windows {

namespace {

using Microsoft::WRL::ComPtr;

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

std::wstring propertyKeyToString(const PROPERTYKEY& key) {
    wchar_t buffer[80] = {};
    if (SUCCEEDED(PSStringFromPropertyKey(key, buffer, static_cast<UINT>(std::size(buffer))))) {
        return buffer;
    }

    std::wstringstream stream;
    stream << guidToString(key.fmtid) << L"," << key.pid;
    return stream.str();
}

std::wstring vtToString(const VARTYPE vt) {
    std::wstringstream stream;
    stream << L"VT_" << vt;
    return stream.str();
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool containsAny(const std::wstring& text, const std::vector<std::wstring>& needles) {
    const std::wstring haystack = lower(text);
    for (const std::wstring& needle : needles) {
        if (haystack.find(needle) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

bool isAudioRelatedProperty(const std::wstring& key, const std::wstring& name, const std::wstring& value) {
    static const std::vector<std::wstring> needles{
        L"audio", L"fx", L"apo", L"effect", L"enhancement", L"sysfx", L"clsid", L"mode", L"endpoint", L"stream",
        L"keyword", L"processing", L"render", L"capture", L"realtek", L"dolby", L"dts", L"waves", L"nahimic", L"avolute"
    };
    return containsAny(key, needles) || containsAny(name, needles) || containsAny(value, needles);
}

std::wstring knownPropertyName(const PROPERTYKEY& key) {
    struct KnownProperty {
        const PROPERTYKEY* key;
        const wchar_t* name;
    };

    static const std::array<KnownProperty, 19> known{{
        {&PKEY_Device_FriendlyName, L"PKEY_Device_FriendlyName"},
        {&PKEY_Device_DeviceDesc, L"PKEY_Device_DeviceDesc"},
        {&PKEY_Device_Manufacturer, L"PKEY_Device_Manufacturer"},
        {&PKEY_AudioEndpoint_Disable_SysFx, L"PKEY_AudioEndpoint_Disable_SysFx"},
        {&PKEY_FX_StreamEffectClsid, L"PKEY_FX_StreamEffectClsid"},
        {&PKEY_CompositeFX_StreamEffectClsid, L"PKEY_CompositeFX_StreamEffectClsid"},
        {&PKEY_FX_KeywordDetector_StreamEffectClsid, L"PKEY_FX_KeywordDetector_StreamEffectClsid"},
        {&PKEY_CompositeFX_KeywordDetector_StreamEffectClsid, L"PKEY_CompositeFX_KeywordDetector_StreamEffectClsid"},
        {&PKEY_FX_ModeEffectClsid, L"PKEY_FX_ModeEffectClsid"},
        {&PKEY_CompositeFX_ModeEffectClsid, L"PKEY_CompositeFX_ModeEffectClsid"},
        {&PKEY_FX_KeywordDetector_ModeEffectClsid, L"PKEY_FX_KeywordDetector_ModeEffectClsid"},
        {&PKEY_CompositeFX_KeywordDetector_ModeEffectClsid, L"PKEY_CompositeFX_KeywordDetector_ModeEffectClsid"},
        {&PKEY_FX_EndpointEffectClsid, L"PKEY_FX_EndpointEffectClsid"},
        {&PKEY_CompositeFX_EndpointEffectClsid, L"PKEY_CompositeFX_EndpointEffectClsid"},
        {&PKEY_FX_KeywordDetector_EndpointEffectClsid, L"PKEY_FX_KeywordDetector_EndpointEffectClsid"},
        {&PKEY_CompositeFX_KeywordDetector_EndpointEffectClsid, L"PKEY_CompositeFX_KeywordDetector_EndpointEffectClsid"},
        {&PKEY_AudioEngine_DeviceFormat, L"PKEY_AudioEngine_DeviceFormat"},
        {&PKEY_AudioEngine_OEMFormat, L"PKEY_AudioEngine_OEMFormat"},
        {&PKEY_AudioEndpoint_GUID, L"PKEY_AudioEndpoint_GUID"},
    }};

    for (const KnownProperty& property : known) {
        if (IsEqualPropertyKey(key, *property.key)) {
            return property.name;
        }
    }

    PWSTR canonicalName = nullptr;
    if (SUCCEEDED(PSGetNameFromPropertyKey(key, &canonicalName)) && canonicalName != nullptr) {
        std::wstring name = canonicalName;
        CoTaskMemFree(canonicalName);
        return name;
    }

    return L"";
}

std::wstring blobSummary(const PROPVARIANT& value) {
    std::wstringstream stream;
    stream << L"Binary blob (" << value.blob.cbSize << L" bytes)";
    return stream.str();
}

std::wstring propVariantToString(const PROPVARIANT& value) {
    switch (value.vt) {
    case VT_EMPTY:
        return L"(empty)";
    case VT_NULL:
        return L"(null)";
    case VT_LPWSTR:
        return value.pwszVal != nullptr ? value.pwszVal : L"";
    case VT_BSTR:
        return value.bstrVal != nullptr ? value.bstrVal : L"";
    case VT_CLSID:
        return value.puuid != nullptr ? guidToString(*value.puuid) : L"";
    case VT_BOOL:
        return value.boolVal == VARIANT_TRUE ? L"true" : L"false";
    case VT_UI4:
        return std::to_wstring(value.ulVal);
    case VT_I4:
        return std::to_wstring(value.lVal);
    case VT_UI8:
        return std::to_wstring(value.uhVal.QuadPart);
    case VT_I8:
        return std::to_wstring(value.hVal.QuadPart);
    case VT_R8: {
        std::wstringstream stream;
        stream << value.dblVal;
        return stream.str();
    }
    case VT_BLOB:
        return blobSummary(value);
    default:
        break;
    }

    if (value.vt == (VT_VECTOR | VT_LPWSTR)) {
        std::wstringstream stream;
        for (ULONG index = 0; index < value.calpwstr.cElems; ++index) {
            if (index > 0) {
                stream << L"; ";
            }
            if (value.calpwstr.pElems[index] != nullptr) {
                stream << value.calpwstr.pElems[index];
            }
        }
        return stream.str();
    }

    if (value.vt == (VT_VECTOR | VT_CLSID)) {
        std::wstringstream stream;
        for (ULONG index = 0; index < value.cauuid.cElems; ++index) {
            if (index > 0) {
                stream << L"; ";
            }
            stream << guidToString(value.cauuid.pElems[index]);
        }
        return stream.str();
    }

    PWSTR stringValue = nullptr;
    if (SUCCEEDED(PropVariantToStringAlloc(value, &stringValue)) && stringValue != nullptr) {
        std::wstring result = stringValue;
        CoTaskMemFree(stringValue);
        return result;
    }

    return L"(unsupported value type)";
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

} // namespace

std::vector<model::EndpointPropertyInfo> EndpointPropertyReader::readEndpointProperties(
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    warningMessage.clear();

    std::vector<model::EndpointPropertyInfo> properties;
    if (endpointId.empty()) {
        warningMessage = L"Endpoint property reader skipped because endpoint ID is empty.";
        return properties;
    }

    ComPtr<IMMDevice> device = getDevice(endpointId, warningMessage);
    if (!device) {
        return properties;
    }

    ComPtr<IPropertyStore> store;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr)) {
        warningMessage = formatHresult(L"IMMDevice::OpenPropertyStore", hr);
        return properties;
    }

    DWORD count = 0;
    hr = store->GetCount(&count);
    if (FAILED(hr)) {
        warningMessage = formatHresult(L"IPropertyStore::GetCount", hr);
        return properties;
    }

    properties.reserve(count);
    for (DWORD index = 0; index < count; ++index) {
        PROPERTYKEY key{};
        hr = store->GetAt(index, &key);
        if (FAILED(hr)) {
            continue;
        }

        PROPVARIANT value;
        PropVariantInit(&value);
        hr = store->GetValue(key, &value);
        if (FAILED(hr)) {
            PropVariantClear(&value);
            continue;
        }

        model::EndpointPropertyInfo info;
        info.key = propertyKeyToString(key);
        info.name = knownPropertyName(key);
        info.value = propVariantToString(value);
        info.valueType = vtToString(value.vt);
        info.audioRelated = isAudioRelatedProperty(info.key, info.name, info.value);
        properties.push_back(std::move(info));

        PropVariantClear(&value);
    }

    std::sort(properties.begin(), properties.end(), [](const model::EndpointPropertyInfo& left, const model::EndpointPropertyInfo& right) {
        if (left.audioRelated != right.audioRelated) {
            return left.audioRelated && !right.audioRelated;
        }
        const std::wstring leftName = left.name.empty() ? left.key : left.name;
        const std::wstring rightName = right.name.empty() ? right.key : right.name;
        return _wcsicmp(leftName.c_str(), rightName.c_str()) < 0;
    });

    return properties;
}

} // namespace audio_path_inspector::windows