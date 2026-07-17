#include "audio_path_inspector/windows/RegistryEvidenceReader.h"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace audio_path_inspector::windows {

namespace {

constexpr int MaxRegistryDepth = 4;
constexpr DWORD MaxRegistryValueBytes = 4096;

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

bool isAudioRelated(const std::wstring& path, const std::wstring& name, const std::wstring& value) {
    static const std::vector<std::wstring> needles{
        L"audio", L"fx", L"apo", L"effect", L"enhancement", L"sysfx", L"clsid", L"stream", L"mode",
        L"endpoint", L"render", L"capture", L"realtek", L"dolby", L"dts", L"waves", L"nahimic", L"avolute",
        L"voice", L"clarity", L"noise", L"microphone", L"speaker", L"inprocserver32", L"dll"
    };
    return containsAny(path, needles) || containsAny(name, needles) || containsAny(value, needles);
}

std::wstring registryTypeName(const DWORD type) {
    switch (type) {
    case REG_SZ:
        return L"REG_SZ";
    case REG_EXPAND_SZ:
        return L"REG_EXPAND_SZ";
    case REG_MULTI_SZ:
        return L"REG_MULTI_SZ";
    case REG_DWORD:
        return L"REG_DWORD";
    case REG_QWORD:
        return L"REG_QWORD";
    case REG_BINARY:
        return L"REG_BINARY";
    default:
        return L"REG_" + std::to_wstring(type);
    }
}

std::wstring rootName(HKEY root) {
    if (root == HKEY_LOCAL_MACHINE) {
        return L"HKLM";
    }
    if (root == HKEY_CLASSES_ROOT) {
        return L"HKCR";
    }
    return L"HKEY";
}

std::wstring bytesToHex(const BYTE* data, const DWORD bytes) {
    std::wstringstream stream;
    const DWORD limit = std::min<DWORD>(bytes, 64);
    for (DWORD index = 0; index < limit; ++index) {
        if (index > 0) {
            stream << L" ";
        }
        stream << std::hex;
        if (data[index] < 16) {
            stream << L"0";
        }
        stream << static_cast<unsigned int>(data[index]);
    }
    if (bytes > limit) {
        stream << L" ... (" << std::dec << bytes << L" bytes)";
    }
    return stream.str();
}

std::wstring registryValueToString(const DWORD type, const std::vector<BYTE>& data) {
    if (data.empty()) {
        return L"";
    }

    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        std::wstring value(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
        while (!value.empty() && value.back() == L'\0') {
            value.pop_back();
        }
        return value;
    }

    if (type == REG_MULTI_SZ) {
        const wchar_t* current = reinterpret_cast<const wchar_t*>(data.data());
        const wchar_t* end = reinterpret_cast<const wchar_t*>(data.data() + data.size());
        std::wstringstream stream;
        bool first = true;
        while (current < end && *current != L'\0') {
            if (!first) {
                stream << L"; ";
            }
            stream << current;
            first = false;
            current += wcslen(current) + 1;
        }
        return stream.str();
    }

    if (type == REG_DWORD && data.size() >= sizeof(DWORD)) {
        DWORD value = 0;
        memcpy(&value, data.data(), sizeof(value));
        return std::to_wstring(value);
    }

    if (type == REG_QWORD && data.size() >= sizeof(unsigned long long)) {
        unsigned long long value = 0;
        memcpy(&value, data.data(), sizeof(value));
        return std::to_wstring(value);
    }

    return bytesToHex(data.data(), static_cast<DWORD>(data.size()));
}

void appendWarning(std::wstring& destination, const std::wstring& message) {
    if (message.empty()) {
        return;
    }
    if (!destination.empty()) {
        destination += L"\n";
    }
    destination += message;
}

std::wstring formatWin32Error(const std::wstring& operation, const DWORD error) {
    std::wstringstream stream;
    stream << operation << L" failed. Win32 error=" << error;
    return stream.str();
}

void addEvidence(
    std::vector<model::RegistryEvidenceInfo>& evidence,
    HKEY root,
    const std::wstring& path,
    const std::wstring& name,
    const DWORD type,
    const std::wstring& value) {
    model::RegistryEvidenceInfo info;
    info.root = rootName(root);
    info.path = path;
    info.name = name.empty() ? L"(default)" : name;
    info.type = registryTypeName(type);
    info.value = value;
    info.audioRelated = isAudioRelated(path, name, value);
    evidence.push_back(std::move(info));
}

void readRegistryTree(
    HKEY root,
    const std::wstring& path,
    const int depth,
    std::vector<model::RegistryEvidenceInfo>& evidence,
    std::wstring& warningMessage,
    std::set<std::wstring>& visited,
    const bool warnMissing) {
    if (depth > MaxRegistryDepth) {
        return;
    }

    const std::wstring visitKey = rootName(root) + L"\\" + lower(path);
    if (!visited.insert(visitKey).second) {
        return;
    }

    HKEY key = nullptr;
    LONG status = RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
    if (status != ERROR_SUCCESS) {
        if (status != ERROR_FILE_NOT_FOUND || warnMissing) {
            appendWarning(warningMessage, formatWin32Error(rootName(root) + L"\\" + path, static_cast<DWORD>(status)));
        }
        return;
    }

    DWORD valueCount = 0;
    DWORD maxValueNameChars = 0;
    DWORD maxValueBytes = 0;
    DWORD subKeyCount = 0;
    DWORD maxSubKeyChars = 0;
    status = RegQueryInfoKeyW(
        key,
        nullptr,
        nullptr,
        nullptr,
        &subKeyCount,
        &maxSubKeyChars,
        nullptr,
        &valueCount,
        &maxValueNameChars,
        &maxValueBytes,
        nullptr,
        nullptr);
    if (status != ERROR_SUCCESS) {
        appendWarning(warningMessage, formatWin32Error(L"RegQueryInfoKey(" + rootName(root) + L"\\" + path + L")", static_cast<DWORD>(status)));
        RegCloseKey(key);
        return;
    }

    for (DWORD index = 0; index < valueCount; ++index) {
        std::wstring valueName(maxValueNameChars + 1, L'\0');
        DWORD valueNameChars = static_cast<DWORD>(valueName.size());
        DWORD type = 0;
        DWORD valueBytes = std::min<DWORD>(maxValueBytes, MaxRegistryValueBytes);
        std::vector<BYTE> data(valueBytes == 0 ? 1 : valueBytes);
        status = RegEnumValueW(key, index, valueName.data(), &valueNameChars, nullptr, &type, data.data(), &valueBytes);
        if (status == ERROR_MORE_DATA) {
            addEvidence(evidence, root, path, L"(large value skipped)", type, L"Value is larger than diagnostic read limit.");
            continue;
        }
        if (status != ERROR_SUCCESS) {
            continue;
        }

        valueName.resize(valueNameChars);
        data.resize(valueBytes);
        addEvidence(evidence, root, path, valueName, type, registryValueToString(type, data));
    }

    for (DWORD index = 0; index < subKeyCount; ++index) {
        std::wstring subKeyName(maxSubKeyChars + 1, L'\0');
        DWORD subKeyNameChars = static_cast<DWORD>(subKeyName.size());
        status = RegEnumKeyExW(key, index, subKeyName.data(), &subKeyNameChars, nullptr, nullptr, nullptr, nullptr);
        if (status != ERROR_SUCCESS) {
            continue;
        }
        subKeyName.resize(subKeyNameChars);
        readRegistryTree(root, path + L"\\" + subKeyName, depth + 1, evidence, warningMessage, visited, warnMissing);
    }

    RegCloseKey(key);
}

std::vector<std::wstring> endpointGuidsFromId(const std::wstring& endpointId) {
    std::vector<std::wstring> result;
    std::size_t position = 0;
    while (true) {
        const std::size_t start = endpointId.find(L'{', position);
        if (start == std::wstring::npos) {
            break;
        }
        const std::size_t end = endpointId.find(L'}', start);
        if (end == std::wstring::npos) {
            break;
        }
        result.push_back(endpointId.substr(start, end - start + 1));
        position = end + 1;
    }
    return result;
}

bool looksLikeDeviceInstanceId(const std::wstring& value) {
    const std::wstring text = lower(value);
    return text.find(L"hdaudio\\") != std::wstring::npos
        || text.find(L"usb\\") != std::wstring::npos
        || text.find(L"swd\\") != std::wstring::npos
        || text.find(L"intelaudio\\") != std::wstring::npos
        || text.find(L"root\\") != std::wstring::npos;
}

std::wstring normalizeDeviceInstancePath(std::wstring value) {
    while (!value.empty() && (value.front() == L'"' || value.front() == L' ')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == L'"' || value.back() == L' ' || value.back() == L'\0')) {
        value.pop_back();
    }
    return value;
}

std::vector<std::wstring> enumPathsFromProperties(const std::vector<model::EndpointPropertyInfo>& endpointProperties) {
    std::set<std::wstring> paths;
    for (const model::EndpointPropertyInfo& property : endpointProperties) {
        if (!looksLikeDeviceInstanceId(property.value)) {
            continue;
        }

        const std::wstring normalized = normalizeDeviceInstancePath(property.value);
        if (!normalized.empty()) {
            paths.insert(L"SYSTEM\\CurrentControlSet\\Enum\\" + normalized);
        }
    }
    return {paths.begin(), paths.end()};
}

bool isGuidChar(const wchar_t ch) {
    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F') || ch == L'-';
}

bool looksLikeGuid(const std::wstring& value, const std::size_t start) {
    if (start + 38 > value.size() || value[start] != L'{' || value[start + 37] != L'}') {
        return false;
    }
    static constexpr std::size_t dashPositions[]{9, 14, 19, 24};
    for (std::size_t index = 1; index < 37; ++index) {
        const bool shouldBeDash = std::find(std::begin(dashPositions), std::end(dashPositions), index) != std::end(dashPositions);
        if (shouldBeDash) {
            if (value[start + index] != L'-') {
                return false;
            }
            continue;
        }
        if (!isGuidChar(value[start + index]) || value[start + index] == L'-') {
            return false;
        }
    }
    return true;
}

std::vector<std::wstring> clsidsFromProperties(const std::vector<model::EndpointPropertyInfo>& endpointProperties) {
    std::set<std::wstring> clsids;
    for (const model::EndpointPropertyInfo& property : endpointProperties) {
        std::size_t position = 0;
        while (true) {
            const std::size_t start = property.value.find(L'{', position);
            if (start == std::wstring::npos) {
                break;
            }
            if (looksLikeGuid(property.value, start)) {
                clsids.insert(property.value.substr(start, 38));
                position = start + 38;
                continue;
            }
            position = start + 1;
        }
    }
    return {clsids.begin(), clsids.end()};
}

} // namespace

std::vector<model::RegistryEvidenceInfo> RegistryEvidenceReader::readRegistryEvidence(
    const std::wstring& endpointId,
    const std::vector<model::EndpointPropertyInfo>& endpointProperties,
    std::wstring& warningMessage) const {
    warningMessage.clear();

    std::vector<model::RegistryEvidenceInfo> evidence;
    std::set<std::wstring> visited;

    for (const std::wstring& endpointGuid : endpointGuidsFromId(endpointId)) {
        readRegistryTree(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Capture\\" + endpointGuid,
            0,
            evidence,
            warningMessage,
            visited,
            false);
        readRegistryTree(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\" + endpointGuid,
            0,
            evidence,
            warningMessage,
            visited,
            false);
    }

    for (const std::wstring& enumPath : enumPathsFromProperties(endpointProperties)) {
        readRegistryTree(HKEY_LOCAL_MACHINE, enumPath, 0, evidence, warningMessage, visited, true);
    }

    for (const std::wstring& clsid : clsidsFromProperties(endpointProperties)) {
        readRegistryTree(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid, 0, evidence, warningMessage, visited, false);
    }

    std::sort(evidence.begin(), evidence.end(), [](const model::RegistryEvidenceInfo& left, const model::RegistryEvidenceInfo& right) {
        if (left.audioRelated != right.audioRelated) {
            return left.audioRelated && !right.audioRelated;
        }
        if (left.root != right.root) {
            return left.root < right.root;
        }
        if (left.path != right.path) {
            return _wcsicmp(left.path.c_str(), right.path.c_str()) < 0;
        }
        return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
    });

    return evidence;
}

} // namespace audio_path_inspector::windows


