#include "audio_path_inspector/windows/DeviceInfoReader.h"

#include <Windows.h>
#include <propsys.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
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

std::wstring formatWaveTag(const WAVEFORMATEX& format) {
    if (format.wFormatTag == WAVE_FORMAT_PCM) {
        return L"PCM";
    }
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return L"IEEE Float";
    }
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        return L"Extensible";
    }

    std::wstringstream stream;
    stream << L"0x" << std::hex << format.wFormatTag;
    return stream.str();
}

void fillMixFormat(IMMDevice& device, model::DeviceDetails& details, std::wstring& errorMessage) {
    ComPtr<IAudioClient> audioClient;
    HRESULT hr = device.Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient);
    if (FAILED(hr)) {
        errorMessage = formatHresult(L"IMMDevice::Activate(IAudioClient)", hr);
        return;
    }

    WAVEFORMATEX* rawFormat = nullptr;
    hr = audioClient->GetMixFormat(&rawFormat);
    if (FAILED(hr) || rawFormat == nullptr) {
        errorMessage = formatHresult(L"IAudioClient::GetMixFormat", hr);
        return;
    }

    details.sampleRate = rawFormat->nSamplesPerSec;
    details.channelCount = rawFormat->nChannels;
    details.bitsPerSample = rawFormat->wBitsPerSample;
    details.formatTag = formatWaveTag(*rawFormat);

    CoTaskMemFree(rawFormat);
}

} // namespace

model::DeviceDetails DeviceInfoReader::readCaptureDeviceDetails(
    const std::wstring& endpointId,
    std::wstring& errorMessage) const {
    errorMessage.clear();

    model::DeviceDetails details;
    details.endpointId = endpointId;

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        errorMessage = formatHresult(L"CoCreateInstance(MMDeviceEnumerator)", hr);
        return details;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(endpointId.c_str(), &device);
    if (FAILED(hr)) {
        errorMessage = formatHresult(L"IMMDeviceEnumerator::GetDevice", hr);
        return details;
    }

    ComPtr<IPropertyStore> propertyStore;
    hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
    if (FAILED(hr)) {
        errorMessage = formatHresult(L"IMMDevice::OpenPropertyStore", hr);
        return details;
    }

    details.friendlyName = readStringProperty(*propertyStore.Get(), PKEY_Device_FriendlyName);
    details.deviceDescription = readStringProperty(*propertyStore.Get(), PKEY_Device_DeviceDesc);
    details.manufacturer = readStringProperty(*propertyStore.Get(), PKEY_Device_Manufacturer);
    details.driverName = details.deviceDescription;
    details.driverVersion = L"Not available yet";

    fillMixFormat(*device.Get(), details, errorMessage);

    return details;
}

} // namespace audio_path_inspector::windows
