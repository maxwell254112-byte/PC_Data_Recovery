#include "utils/Logger.h"
#include "utils/PathUtils.h"
#include "utils/FormatUtils.h"

#include <fstream>
#include <ctime>

namespace pdr {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Initialize(const std::wstring& logDirectory) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureDirectory(logDirectory);
    filePath_ = logDirectory + L"\\app.log";
    ready_ = true;
}

void Logger::Info(const std::wstring& message) { Write(L"INFO", message); }
void Logger::Warn(const std::wstring& message) { Write(L"WARN", message); }
void Logger::Error(const std::wstring& message) { Write(L"ERROR", message); }

void Logger::Write(const wchar_t* level, const std::wstring& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_) {
        return;
    }

    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath_.c_str(), L"ab");
    if (!fp) {
        return;
    }

    std::wstring line = FormatNow() + L" [" + level + L"] " + message + L"\r\n";
    std::string utf8;
    int needed = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed > 1) {
        utf8.resize(static_cast<size_t>(needed - 1));
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), needed, nullptr, nullptr);
        fwrite(utf8.data(), 1, utf8.size(), fp);
    }
    fclose(fp);
}

} // namespace pdr
