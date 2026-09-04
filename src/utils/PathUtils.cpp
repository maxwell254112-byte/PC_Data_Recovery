#include "utils/PathUtils.h"
#include "utils/StringUtils.h"

#include <windows.h>
#include <shlobj.h>

namespace pdr {

std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full = path;
    auto pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    return full.substr(0, pos);
}

std::wstring GetConfigDirectory() {
    return JoinPath(GetExeDirectory(), L"config");
}

std::wstring GetDataDirectory() {
    return JoinPath(GetExeDirectory(), L"data");
}

std::wstring GetLogDirectory() {
    return JoinPath(GetExeDirectory(), L"logs");
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

bool IsPathOnVolume(const std::wstring& path, const std::wstring& letter) {
    if (path.size() < 2 || letter.size() < 2) {
        return false;
    }
    return towupper(path[0]) == towupper(letter[0]) && path[1] == L':';
}

std::wstring VolumeLetterFromPath(const std::wstring& path) {
    if (path.size() >= 2 && path[1] == L':') {
        std::wstring letter(1, static_cast<wchar_t>(towupper(path[0])));
        letter += L":";
        return letter;
    }
    return {};
}

} // namespace pdr
