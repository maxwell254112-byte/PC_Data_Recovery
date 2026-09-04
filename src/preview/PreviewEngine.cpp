#include "preview/PreviewEngine.h"
#include "drive/VolumeReader.h"
#include "utils/StringUtils.h"
#include "utils/FormatUtils.h"

#include <wincodec.h>
#include <shlwapi.h>
#include <algorithm>
#include <sstream>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")

namespace pdr {
namespace {

bool IsTextExt(const std::wstring& ext) {
    return ext == L"txt" || ext == L"csv" || ext == L"log" || ext == L"ini" || ext == L"xml" || ext == L"json";
}

bool IsImageExt(const std::wstring& ext) {
    return ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"gif" ||
           ext == L"bmp" || ext == L"tif" || ext == L"tiff";
}

std::wstring LatinOrUtf8(const std::vector<uint8_t>& data) {
    bool utf16 = data.size() >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF));
    if (utf16) {
        size_t chars = (data.size() - 2) / 2;
        return std::wstring(reinterpret_cast<const wchar_t*>(data.data() + 2), chars);
    }
    bool ascii = true;
    for (size_t i = 0; i < data.size() && i < 4096; ++i) {
        if (data[i] == 0) { ascii = false; break; }
    }
    if (ascii) {
        return Utf8ToWide(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    }
    return L"";
}

} // namespace

std::vector<uint8_t> PreviewEngine::LoadSample(const RecoveredFile& file, const DriveInfo& drive, uint32_t maxBytes) {
    if (!file.residentData.empty()) {
        uint32_t n = (std::min)(maxBytes, static_cast<uint32_t>(file.residentData.size()));
        return std::vector<uint8_t>(file.residentData.begin(), file.residentData.begin() + n);
    }
    if (!file.localSourcePath.empty()) {
        HANDLE h = CreateFileW(file.localSourcePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return {};
        std::vector<uint8_t> buf(maxBytes);
        DWORD got = 0;
        ReadFile(h, buf.data(), maxBytes, &got, nullptr);
        CloseHandle(h);
        buf.resize(got);
        return buf;
    }

    VolumeReader reader;
    if (!reader.Open(drive.volumePath.empty() ? file.sourceVolume : drive.volumePath)) {
        return {};
    }
    uint64_t offset = file.volumeOffset;
    if (offset == 0 && !file.runs.empty() && drive.clusterSize) {
        offset = file.runs[0].startLcn * drive.clusterSize;
    }
    if (offset == 0 && file.method != DiscoveryMethod::RawCarve) {
        return {};
    }
    uint32_t take = maxBytes;
    if (file.dataLength && file.dataLength < take) {
        take = static_cast<uint32_t>(file.dataLength);
    }
    std::vector<uint8_t> buf;
    reader.Read(offset, buf, take);
    return buf;
}

bool PreviewEngine::PreviewImage(const std::vector<uint8_t>& data, PreviewData& out) {
    if (data.size() < 16) {
        return false;
    }
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return false;
    }
    IStream* stream = SHCreateMemStream(data.data(), static_cast<UINT>(data.size()));
    if (!stream) {
        factory->Release();
        return false;
    }
    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    stream->Release();
    if (FAILED(hr) || !decoder) {
        factory->Release();
        return false;
    }
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
        decoder->Release();
        factory->Release();
        return false;
    }
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    out.width = w;
    out.height = h;

    IWICFormatConverter* conv = nullptr;
    factory->CreateFormatConverter(&conv);
    if (!conv) {
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }
    conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                     nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT stride = w * 4;
    UINT bufSize = stride * h;
    std::vector<uint8_t> pixels(bufSize);
    if (FAILED(conv->CopyPixels(nullptr, stride, bufSize, pixels.data()))) {
        conv->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(w);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (dib && bits) {
        memcpy(bits, pixels.data(), bufSize);
        out.bitmap = dib;
        out.available = true;
    }
    conv->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return out.available;
}

bool PreviewEngine::PreviewText(const std::vector<uint8_t>& data, PreviewData& out) {
    std::wstring text = LatinOrUtf8(data);
    if (text.empty()) {
        return false;
    }
    if (text.size() > 8000) {
        text.resize(8000);
        text += L"\r\n\r\n[Preview truncated]";
    }
    out.text = text;
    out.available = true;
    return true;
}

bool PreviewEngine::PreviewPdf(const std::vector<uint8_t>& data, PreviewData& out) {
    if (data.size() < 8 || memcmp(data.data(), "%PDF", 4) != 0) {
        return false;
    }
    std::wstring header = L"PDF header: ";
    header += Utf8ToWide(std::string(reinterpret_cast<const char*>(data.data()),
                                     (std::min<size_t>)(data.size(), 16)));
    std::string raw(reinterpret_cast<const char*>(data.data()), data.size());
    std::wstring extracted;
    size_t pos = 0;
    int chunks = 0;
    while (chunks < 8) {
        auto s = raw.find("stream", pos);
        if (s == std::string::npos) break;
        auto e = raw.find("endstream", s);
        if (e == std::string::npos) break;
        std::string body = raw.substr(s + 6, e - (s + 6));
        bool printable = true;
        int letters = 0;
        for (unsigned char c : body) {
            if (c == 0) { printable = false; break; }
            if (c >= 32 && c < 127) ++letters;
        }
        if (printable && letters > 20) {
            extracted += Utf8ToWide(body);
            extracted += L"\r\n";
            ++chunks;
        }
        pos = e + 9;
    }
    out.text = header + L"\r\n\r\n";
    if (extracted.empty()) {
        out.text += L"This PDF is compressed or binary. Page rendering is unsupported without an external PDF engine.\r\n";
        out.text += L"Metadata preview only — the file can still be recovered as binary PDF.";
        out.unsupportedReason = L"PDF page render unsupported; showing header/text streams only";
    } else {
        if (extracted.size() > 6000) extracted.resize(6000);
        out.text += extracted;
    }
    out.available = true;
    return true;
}

bool PreviewEngine::Build(const RecoveredFile& file, const DriveInfo& drive, const AppConfig& config, PreviewData& out) {
    out.Reset();
    out.title = file.name;
    std::wstringstream detail;
    detail << L"Type: " << file.typeName << L"\r\n"
           << L"Size: " << FormatBytes(file.size) << L"\r\n"
           << L"Path: " << (file.originalPath.empty() ? L"(unknown)" : file.originalPath) << L"\r\n"
           << L"Confidence: " << ToString(file.confidence) << L"\r\n"
           << L"Found by: " << ToString(file.method);
    out.detail = detail.str();

    auto ext = ToLower(file.extension);
    auto sample = LoadSample(file, drive, 2 * 1024 * 1024);
    if (sample.empty()) {
        out.unsupportedReason = L"Unable to read file data for preview";
        return false;
    }

    if (IsImageExt(ext)) {
        if (!config.previewImages) {
            out.unsupportedReason = L"Image preview disabled in settings";
            return false;
        }
        if (PreviewImage(sample, out)) {
            out.detail += L"\r\nDimensions: " + std::to_wstring(out.width) + L" x " + std::to_wstring(out.height);
            return true;
        }
        out.unsupportedReason = L"Image could not be decoded (file may be partial or corrupted)";
        return false;
    }
    if (ext == L"pdf") {
        if (!config.previewPdf) {
            out.unsupportedReason = L"PDF preview disabled in settings";
            return false;
        }
        return PreviewPdf(sample, out);
    }
    if (IsTextExt(ext) || file.category == FileCategory::Text) {
        if (!config.previewText) {
            out.unsupportedReason = L"Text preview disabled in settings";
            return false;
        }
        return PreviewText(sample, out);
    }

    out.unsupportedReason = L"Preview not available for this file type";
    out.text = L"Preview is supported for images (JPG, PNG, GIF, BMP, TIFF), PDF metadata/text, and text files.";
    return false;
}

} // namespace pdr
