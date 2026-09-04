#include "utils/FormatUtils.h"

#include <sstream>
#include <iomanip>
#include <cmath>

namespace pdr {

std::wstring FormatBytes(uint64_t bytes) {
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    std::wstringstream ss;
    if (unit == 0) {
        ss << bytes << L" B";
    } else {
        ss << std::fixed << std::setprecision(value >= 100.0 ? 1 : 2) << value << L" " << units[unit];
    }
    return ss.str();
}

std::wstring FormatDuration(uint64_t milliseconds) {
    uint64_t totalSec = milliseconds / 1000;
    uint64_t hours = totalSec / 3600;
    uint64_t minutes = (totalSec % 3600) / 60;
    uint64_t seconds = totalSec % 60;
    wchar_t buf[32];
    swprintf_s(buf, L"%02llu:%02llu:%02llu", hours, minutes, seconds);
    return buf;
}

std::wstring FormatSpeed(double bytesPerSecond) {
    if (bytesPerSecond <= 0.0 || !std::isfinite(bytesPerSecond)) {
        return L"0 B/s";
    }
    return FormatBytes(static_cast<uint64_t>(bytesPerSecond)) + L"/s";
}

std::wstring FormatPercent(double percent) {
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;
    wchar_t buf[16];
    swprintf_s(buf, L"%.1f%%", percent);
    return buf;
}

std::wstring FormatFileTime(const FILETIME& ft) {
    if (ft.dwHighDateTime == 0 && ft.dwLowDateTime == 0) {
        return L"—";
    }
    FILETIME local{};
    FileTimeToLocalFileTime(&ft, &local);
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&local, &st)) {
        return L"—";
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring FormatNow() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring DriveSummary(const DriveInfo& drive) {
    std::wstring text;
    if (!drive.letter.empty()) {
        text += drive.letter;
        text += L"  ";
    }
    text += drive.label.empty() ? L"Local Disk" : drive.label;
    text += L"  ·  ";
    text += ToString(drive.fileSystem);
    text += L"  ·  ";
    text += FormatBytes(drive.totalBytes);
    return text;
}

} // namespace pdr
