#pragma once

#include <objbase.h>

namespace audio_path_inspector::windows {

class ComApartment final {
public:
    explicit ComApartment(const DWORD coinit) noexcept
        : hr_(CoInitializeEx(nullptr, coinit)),
          initialized_(SUCCEEDED(hr_)) {
    }

    ~ComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    ComApartment(ComApartment&&) = delete;
    ComApartment& operator=(ComApartment&&) = delete;

    [[nodiscard]] HRESULT result() const noexcept {
        return hr_;
    }

    [[nodiscard]] bool ok() const noexcept {
        return initialized_ || hr_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT hr_;
    bool initialized_;
};

} // namespace audio_path_inspector::windows
