#include "audio_path_inspector/windows/AudioEnhancementsReader.h"

#include <initguid.h>
#include <Windows.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <wrl/client.h>

#include <sstream>

namespace audio_path_inspector::windows {

namespace {

using Microsoft::WRL::ComPtr;

std::wstring formatHresult(const wchar_t* operation, const HRESULT hr) {
    std::wstringstream stream;
    stream << operation << L" failed. HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
    return stream.str();
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

model::AudioEnhancementsInfo AudioEnhancementsReader::readCaptureAudioEnhancements(
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    warningMessage.clear();

    model::AudioEnhancementsInfo info;

    if (endpointId.empty()) {
        info.state = model::AudioEnhancementsState::NotAvailable;
        info.detail = L"Endpoint ID is empty.";
        warningMessage = L"Audio Enhancements reader skipped because endpoint ID is empty.";
        return info;
    }

    ComPtr<IMMDevice> device = getDevice(endpointId, warningMessage);
    if (!device) {
        info.state = model::AudioEnhancementsState::NotAvailable;
        info.detail = warningMessage;
        return info;
    }

    ComPtr<IPropertyStore> store;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr)) {
        warningMessage = formatHresult(L"IMMDevice::OpenPropertyStore", hr);
        info.state = model::AudioEnhancementsState::NotAvailable;
        info.detail = warningMessage;
        return info;
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    hr = store->GetValue(PKEY_AudioEndpoint_Disable_SysFx, &value);
    if (FAILED(hr) || value.vt == VT_EMPTY) {
        PropVariantClear(&value);
        info.state = model::AudioEnhancementsState::Unknown;
        info.detail = L"PKEY_AudioEndpoint_Disable_SysFx is not available for this endpoint.";
        return info;
    }

    DWORD sysFxValue = 0;
    if (value.vt == VT_UI4) {
        sysFxValue = value.ulVal;
    } else if (value.vt == VT_I4) {
        sysFxValue = static_cast<DWORD>(value.lVal);
    } else if (value.vt == VT_BOOL) {
        sysFxValue = value.boolVal == VARIANT_TRUE ? ENDPOINT_SYSFX_DISABLED : ENDPOINT_SYSFX_ENABLED;
    } else {
        PropVariantClear(&value);
        info.state = model::AudioEnhancementsState::Unknown;
        info.detail = L"PKEY_AudioEndpoint_Disable_SysFx has an unsupported value type.";
        return info;
    }

    PropVariantClear(&value);

    if (sysFxValue == ENDPOINT_SYSFX_ENABLED) {
        info.state = model::AudioEnhancementsState::On;
        info.detail = L"System effects are enabled for this endpoint.";
    } else if (sysFxValue == ENDPOINT_SYSFX_DISABLED) {
        info.state = model::AudioEnhancementsState::Off;
        info.detail = L"System effects are disabled for this endpoint.";
    } else {
        info.state = model::AudioEnhancementsState::Unknown;
        std::wstringstream stream;
        stream << L"Unknown PKEY_AudioEndpoint_Disable_SysFx value: " << sysFxValue;
        info.detail = stream.str();
    }

    return info;
}

} // namespace audio_path_inspector::windows
