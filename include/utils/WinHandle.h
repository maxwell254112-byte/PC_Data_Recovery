#pragma once

#include <windows.h>

namespace pdr {

class WinHandle {
public:
    WinHandle() = default;
    explicit WinHandle(HANDLE handle) : handle_(handle) {}
    ~WinHandle() { Close(); }

    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;

    WinHandle(WinHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    WinHandle& operator=(WinHandle&& other) noexcept {
        if (this != &other) {
            Close();
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    HANDLE Get() const { return handle_; }
    bool Valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    HANDLE* Receive() {
        Close();
        return &handle_;
    }

    void Reset(HANDLE handle = INVALID_HANDLE_VALUE) {
        Close();
        handle_ = handle;
    }

    HANDLE Release() {
        HANDLE tmp = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return tmp;
    }

private:
    void Close() {
        if (Valid()) {
            CloseHandle(handle_);
        }
        handle_ = INVALID_HANDLE_VALUE;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

} // namespace pdr
