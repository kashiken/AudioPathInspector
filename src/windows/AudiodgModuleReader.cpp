#include "audio_path_inspector/windows/AudiodgModuleReader.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <psapi.h>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace audio_path_inspector::windows {

namespace {

std::wstring formatWin32Error(const wchar_t* operation, const DWORD error) {
    std::wstringstream stream;
    stream << operation << L" failed. Win32 error=" << error;
    return stream.str();
}

std::wstring baseName(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::vector<DWORD> findAudiodgProcessIds(std::wstring& warningMessage) {
    std::vector<DWORD> processIds;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        warningMessage = formatWin32Error(L"CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)", GetLastError());
        return processIds;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        warningMessage = formatWin32Error(L"Process32FirstW", GetLastError());
        CloseHandle(snapshot);
        return processIds;
    }

    do {
        if (_wcsicmp(entry.szExeFile, L"audiodg.exe") == 0) {
            processIds.push_back(entry.th32ProcessID);
        }
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    return processIds;
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

std::vector<model::ModuleInfo> readModulesForProcess(const DWORD processId, std::wstring& warningMessage) {
    std::vector<model::ModuleInfo> modules;

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (process == nullptr) {
        std::wstringstream operation;
        operation << L"OpenProcess(audiodg.exe pid " << processId << L")";
        warningMessage = formatWin32Error(operation.str().c_str(), GetLastError());
        return modules;
    }

    DWORD bytesNeeded = 0;
    std::vector<HMODULE> handles(256);
    if (!EnumProcessModulesEx(
            process,
            handles.data(),
            static_cast<DWORD>(handles.size() * sizeof(HMODULE)),
            &bytesNeeded,
            LIST_MODULES_ALL)) {
        std::wstringstream operation;
        operation << L"EnumProcessModulesEx(audiodg.exe pid " << processId << L")";
        warningMessage = formatWin32Error(operation.str().c_str(), GetLastError());
        CloseHandle(process);
        return modules;
    }

    const DWORD moduleCount = bytesNeeded / sizeof(HMODULE);
    if (moduleCount > handles.size()) {
        handles.resize(moduleCount);
        if (!EnumProcessModulesEx(
                process,
                handles.data(),
                static_cast<DWORD>(handles.size() * sizeof(HMODULE)),
                &bytesNeeded,
                LIST_MODULES_ALL)) {
            std::wstringstream operation;
            operation << L"EnumProcessModulesEx(audiodg.exe pid " << processId << L")";
            warningMessage = formatWin32Error(operation.str().c_str(), GetLastError());
            CloseHandle(process);
            return modules;
        }
    }

    const DWORD finalCount = bytesNeeded / sizeof(HMODULE);
    modules.reserve(finalCount);
    for (DWORD index = 0; index < finalCount; ++index) {
        wchar_t pathBuffer[MAX_PATH] = {};
        if (GetModuleFileNameExW(process, handles[index], pathBuffer, MAX_PATH) == 0) {
            continue;
        }

        model::ModuleInfo module;
        module.processId = processId;
        module.path = pathBuffer;
        module.name = baseName(module.path);
        modules.push_back(std::move(module));
    }

    CloseHandle(process);
    return modules;
}

} // namespace

std::vector<model::ModuleInfo> AudiodgModuleReader::readLoadedModules(std::wstring& warningMessage) const {
    warningMessage.clear();

    std::wstring processWarning;
    const std::vector<DWORD> processIds = findAudiodgProcessIds(processWarning);
    appendWarning(warningMessage, processWarning);

    if (processIds.empty()) {
        if (warningMessage.empty()) {
            warningMessage = L"audiodg.exe is not currently running.";
        }
        return {};
    }

    std::vector<model::ModuleInfo> allModules;
    std::set<std::wstring> seen;
    for (const DWORD processId : processIds) {
        std::wstring moduleWarning;
        std::vector<model::ModuleInfo> modules = readModulesForProcess(processId, moduleWarning);
        appendWarning(warningMessage, moduleWarning);

        for (model::ModuleInfo& module : modules) {
            std::wstringstream key;
            key << module.processId << L"|" << module.path;
            if (seen.insert(key.str()).second) {
                allModules.push_back(std::move(module));
            }
        }
    }

    std::sort(allModules.begin(), allModules.end(), [](const model::ModuleInfo& left, const model::ModuleInfo& right) {
        if (left.processId != right.processId) {
            return left.processId < right.processId;
        }
        return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
    });

    return allModules;
}

} // namespace audio_path_inspector::windows