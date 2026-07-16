#include "audio_path_inspector/audio/DeviceInspectionService.h"

#include "audio_path_inspector/windows/ApoReader.h"
#include "audio_path_inspector/windows/AudioEnhancementsReader.h"
#include "audio_path_inspector/windows/AudioEffectsReader.h"
#include "audio_path_inspector/windows/DeviceEnumerator.h"
#include "audio_path_inspector/windows/DeviceInfoReader.h"
#include "audio_path_inspector/windows/StreamOpenTester.h"

namespace {

void appendWarning(std::wstring& destination, const std::wstring& message) {
    if (message.empty()) {
        return;
    }

    if (!destination.empty()) {
        destination += L"\n";
    }
    destination += message;
}

} // namespace

namespace audio_path_inspector::audio {

std::vector<model::DeviceSummary> DeviceInspectionService::enumerateDevices(
    const model::DeviceFlow flow,
    std::wstring& errorMessage) const {
    const windows::DeviceEnumerator enumerator;
    return enumerator.enumerateDevices(flow, errorMessage);
}

std::vector<model::DeviceSummary> DeviceInspectionService::enumerateCaptureDevices(std::wstring& errorMessage) const {
    return enumerateDevices(model::DeviceFlow::Capture, errorMessage);
}

model::DeviceInspection DeviceInspectionService::inspectDevice(
    const model::DeviceFlow flow,
    const model::DeviceSummary& device,
    std::wstring& errorMessage) const {
    errorMessage.clear();

    model::DeviceInspection inspection;
    inspection.summary = device;

    const windows::DeviceInfoReader infoReader;
    std::wstring warningMessage;
    inspection.details = infoReader.readDeviceDetails(flow, device.endpointId, warningMessage);
    appendWarning(inspection.warningMessage, warningMessage);

    const windows::ApoReader apoReader;
    inspection.apos = apoReader.readApos(flow, device.endpointId, warningMessage);
    inspection.apoMessage = warningMessage;
    appendWarning(inspection.warningMessage, warningMessage);

    const windows::AudioEffectsReader effectsReader;
    inspection.audioEffects = effectsReader.readAudioEffects(flow, device.endpointId, warningMessage);
    inspection.audioEffectsMessage = warningMessage;
    appendWarning(inspection.warningMessage, warningMessage);

    const windows::AudioEnhancementsReader enhancementsReader;
    inspection.audioEnhancements = enhancementsReader.readAudioEnhancements(flow, device.endpointId, warningMessage);
    inspection.audioEnhancementsMessage = warningMessage;
    appendWarning(inspection.warningMessage, warningMessage);

    const windows::StreamOpenTester streamOpenTester;
    inspection.streamOpenResult = streamOpenTester.testStreamOpen(flow, device.endpointId);

    errorMessage = inspection.warningMessage;

    return inspection;
}

model::DeviceInspection DeviceInspectionService::inspectCaptureDevice(
    const model::DeviceSummary& device,
    std::wstring& errorMessage) const {
    return inspectDevice(model::DeviceFlow::Capture, device, errorMessage);
}

} // namespace audio_path_inspector::audio
