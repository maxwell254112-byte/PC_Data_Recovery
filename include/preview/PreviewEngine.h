#pragma once

#include "utils/Types.h"
#include <windows.h>
#include <objidl.h>
#include <string>
#include <vector>

namespace pdr {

struct PreviewData {
    bool available = false;
    std::wstring title;
    std::wstring detail;
    std::wstring text;
    HBITMAP bitmap = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    std::wstring unsupportedReason;

    void Reset() {
        if (bitmap) {
            DeleteObject(bitmap);
            bitmap = nullptr;
        }
        available = false;
        title.clear();
        detail.clear();
        text.clear();
        width = 0;
        height = 0;
        unsupportedReason.clear();
    }
};

class PreviewEngine {
public:
    PreviewEngine() = default;
    ~PreviewEngine() { current_.Reset(); }

    bool Build(const RecoveredFile& file, const DriveInfo& drive, const AppConfig& config, PreviewData& out);
    static std::vector<uint8_t> LoadSample(const RecoveredFile& file, const DriveInfo& drive, uint32_t maxBytes);

private:
    bool PreviewImage(const std::vector<uint8_t>& data, PreviewData& out);
    bool PreviewText(const std::vector<uint8_t>& data, PreviewData& out);
    bool PreviewPdf(const std::vector<uint8_t>& data, PreviewData& out);

    PreviewData current_;
};

} // namespace pdr
