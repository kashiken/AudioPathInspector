#include "audio_path_inspector/windows/AudioEffectsReader.h"

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <iterator>
#include <sstream>
#include <string>
#include <utility>
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

std::wstring effectName(const GUID& guid) {
    return L"Windows Audio Effect " + guidToString(guid);
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

std::vector<model::AudioEffectInfo> AudioEffectsReader::readAudioEffects(
    const model::DeviceFlow flow,
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    static_cast<void>(flow);
    warningMessage.clear();

    if (endpointId.empty()) {
        warningMessage = L"Audio Effects reader skipped because endpoint ID is empty.";
        return {};
    }

    ComPtr<IMMDevice> device = getDevice(endpointId, warningMessage);
    if (!device) {
        return {};
    }

    ComPtr<IAudioEffectsManager> manager;
    HRESULT hr = device->Activate(__uuidof(IAudioEffectsManager), CLSCTX_ALL, nullptr, &manager);
    if (FAILED(hr)) {
        if (hr == E_NOINTERFACE) {
            warningMessage = L"Windows Audio Effects API is not supported by this endpoint. HRESULT=0x80004002";
        } else {
            warningMessage = formatHresult(L"IMMDevice::Activate(IAudioEffectsManager)", hr);
        }
        return {};
    }

    AUDIO_EFFECT* rawEffects = nullptr;
    UINT32 effectCount = 0;
    hr = manager->GetAudioEffects(&rawEffects, &effectCount);
    if (FAILED(hr)) {
        warningMessage = formatHresult(L"IAudioEffectsManager::GetAudioEffects", hr);
        return {};
    }

    std::vector<model::AudioEffectInfo> effects;
    effects.reserve(effectCount);
    for (UINT32 index = 0; index < effectCount; ++index) {
        const AUDIO_EFFECT& rawEffect = rawEffects[index];

        model::AudioEffectInfo info;
        info.name = effectName(rawEffect.id);
        info.enabled = rawEffect.state == AUDIO_EFFECT_STATE_ON;
        info.status = rawEffect.canSetState ? L"state can be changed" : L"state is read-only";
        effects.push_back(std::move(info));
    }

    CoTaskMemFree(rawEffects);

    return effects;
}


std::vector<model::AudioEffectInfo> AudioEffectsReader::readCaptureAudioEffects(
    const std::wstring& endpointId,
    std::wstring& warningMessage) const {
    return readAudioEffects(model::DeviceFlow::Capture, endpointId, warningMessage);
}

} // namespace audio_path_inspector::windows
