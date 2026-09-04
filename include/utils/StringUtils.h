#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace pdr {

std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);
std::wstring ToLower(const std::wstring& value);
std::wstring Trim(const std::wstring& value);
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b);
bool ContainsIgnoreCase(const std::wstring& haystack, const std::wstring& needle);
std::wstring GetExtension(const std::wstring& name);
std::wstring GetFileName(const std::wstring& path);
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring SanitizeFileName(const std::wstring& name);
std::wstring UniquePath(const std::wstring& directory, const std::wstring& name);
std::wstring LastErrorMessage(DWORD error = GetLastError());

} // namespace pdr
