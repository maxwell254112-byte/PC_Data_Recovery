#pragma once

#include <string>
#include <cstdint>
#include <windows.h>
#include "utils/Types.h"

namespace pdr {

std::wstring FormatBytes(uint64_t bytes);
std::wstring FormatDuration(uint64_t milliseconds);
std::wstring FormatSpeed(double bytesPerSecond);
std::wstring FormatPercent(double percent);
std::wstring FormatFileTime(const FILETIME& ft);
std::wstring FormatNow();
std::wstring DriveSummary(const DriveInfo& drive);

} // namespace pdr
