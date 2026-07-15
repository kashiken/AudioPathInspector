#include "audio_path_inspector/windows/DeviceEnumerator.h"

#include <Windows.h>
#include <propsys.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <sstream>
#include <utility>

namespace audio_path_inspector::windows {

namespace {

using Microsoft::WRL::ComPtr;

std::wstring formatHresult(const wchar_t* operation, const HRESULT hr) {
    std::wstringstream stream;
    stream << operation << L" failed. HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
    return stream.str();
}

std::wstring readStringProperty(IPropertyStore& store, const PROPERTYKEY& key) {
    PROPVARIANT value;
    PropVariantInit(&value);

    const HRESULT hr = store.GetValue(key, &value);
    if (FAILED(hr)) {
        PropVariantClear(&value);
        return L"";
    }

    std::wstring result;
    if (value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        result = value.pwszVal;
    }

    PropVariantClear(&value);
    return result;
}

std::wstring getDeviceId(IMMDevice& device) {
    wchar_t* rawId = nullptr;
    const HRESULT hr = device.GetId(&rawId);
    if (FAILED(hr) || rawId == nullptr) {
        return L"";
    }

    std::wstring id(rawId);
    CoTaskMemFree(rawId);
    return id;
}

} // namespace

std::vector<model::DeviceSummary> DeviceEnumerator::enumerateCaptureDevices(std::wstring& errorMessage) const {
    errorMessage.clear();

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        errorMessage = formatHresult(L"CoCreateInstance(MMDeviceEnumerator)", hr);
        return {};
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        errorMessage = formatHresult(L"EnumAudioEndpoints(eCapture)", hr);
        return {};
    }

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr)) {
        errorMessage = formatHresult(L"IMMDeviceCollection::GetCount", hr);
        return {};
    }

    std::vector<model::DeviceSummary> devices;
    devices.reserve(count);

    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        hr = collection->Item(index, &device);
        if (FAILED(hr)) {
            continue;
        }

        ComPtr<IPropertyStore> propertyStore;
        hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
        if (FAILED(hr)) {
            continue;
        }

        model::DeviceSummary summary;
        summary.friendlyName = readStringProperty(*propertyStore.Get(), PKEY_Device_FriendlyName);
        summary.deviceId = readStringProperty(*propertyStore.Get(), PKEY_Device_DeviceDesc);
        summary.endpointId = getDeviceId(*device.Get());

        if (summary.friendlyName.empty()) {
            summary.friendlyName = L"(Unnamed capture device)";
        }
        if (summary.deviceId.empty()) {
            summary.deviceId = summary.endpointId;
        }

        devices.push_back(std::move(summary));
    }

    return devices;
}

} // namespace audio_path_inspector::windows
