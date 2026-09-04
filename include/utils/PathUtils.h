#pragma once

#include <string>

namespace pdr {

std::wstring GetExeDirectory();
std::wstring GetConfigDirectory();
std::wstring GetDataDirectory();
std::wstring GetLogDirectory();
bool EnsureDirectory(const std::wstring& path);
bool IsPathOnVolume(const std::wstring& path, const std::wstring& letter);
std::wstring VolumeLetterFromPath(const std::wstring& path);

} // namespace pdr
