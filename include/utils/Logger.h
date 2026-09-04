#pragma once

#include <string>
#include <mutex>

namespace pdr {

class Logger {
public:
    static Logger& Instance();

    void Initialize(const std::wstring& logDirectory);
    void Info(const std::wstring& message);
    void Warn(const std::wstring& message);
    void Error(const std::wstring& message);

private:
    Logger() = default;
    void Write(const wchar_t* level, const std::wstring& message);

    std::mutex mutex_;
    std::wstring filePath_;
    bool ready_ = false;
};

} // namespace pdr
