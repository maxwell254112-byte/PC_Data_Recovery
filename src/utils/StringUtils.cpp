#include "utils/StringUtils.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace pdr {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (needed <= 1) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), needed);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string utf8(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

std::wstring ToLower(const std::wstring& value) {
    std::wstring out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return out;
}

std::wstring Trim(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && iswspace(value[start])) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && iswspace(value[end - 1])) {
        --end;
    }
    return value.substr(start, end - start);
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return ToLower(a) == ToLower(b);
}

bool ContainsIgnoreCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

std::wstring GetExtension(const std::wstring& name) {
    auto pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos || pos + 1 >= name.size()) {
        return {};
    }
    return ToLower(name.substr(pos + 1));
}

std::wstring GetFileName(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    wchar_t last = a.back();
    if (last == L'\\' || last == L'/') {
        return a + b;
    }
    return a + L"\\" + b;
}

std::wstring SanitizeFileName(const std::wstring& name) {
    std::wstring out;
    out.reserve(name.size());
    const wchar_t* invalid = L"<>:\"/\\|?*";
    for (wchar_t c : name) {
        if (c < 32 || wcschr(invalid, c)) {
            out.push_back(L'_');
        } else {
            out.push_back(c);
        }
    }
    if (out.empty()) {
        out = L"recovered_file";
    }
    return out;
}

std::wstring UniquePath(const std::wstring& directory, const std::wstring& name) {
    std::wstring clean = SanitizeFileName(name);
    std::wstring path = JoinPath(directory, clean);
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return path;
    }

    std::wstring stem = clean;
    std::wstring ext;
    auto dot = clean.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        stem = clean.substr(0, dot);
        ext = clean.substr(dot);
    }

    for (int i = 1; i < 100000; ++i) {
        std::wstring candidate = stem + L"_" + std::to_wstring(i) + ext;
        path = JoinPath(directory, candidate);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }
    return JoinPath(directory, stem + L"_copy" + ext);
}

std::wstring LastErrorMessage(DWORD error) {
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message = L"Error " + std::to_wstring(error);
    if (len && buffer) {
        message += L": ";
        message += Trim(buffer);
        LocalFree(buffer);
    }
    return message;
}

} // namespace pdr
