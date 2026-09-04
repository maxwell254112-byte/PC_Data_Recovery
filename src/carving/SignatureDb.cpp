#include "carving/SignatureDb.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <cstring>

namespace pdr {
namespace {

uint32_t Read32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t Read32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

size_t FindFooter(const uint8_t* data, size_t available, const std::vector<uint8_t>& footer, size_t start) {
    if (footer.empty() || available < footer.size()) {
        return static_cast<size_t>(-1);
    }
    for (size_t i = start; i + footer.size() <= available; ++i) {
        if (memcmp(data + i, footer.data(), footer.size()) == 0) {
            return i + footer.size();
        }
    }
    return static_cast<size_t>(-1);
}

} // namespace

SignatureDb& SignatureDb::Instance() {
    static SignatureDb db;
    return db;
}

SignatureDb::SignatureDb() {
    Add(L"jpg",  L"JPEG Image",     FileCategory::Image,    {0xFF,0xD8,0xFF}, 0, {0xFF,0xD9}, 50ull<<20);
    Add(L"jpeg", L"JPEG Image",     FileCategory::Image,    {}, 0, {}, 50ull<<20);
    Add(L"png",  L"PNG Image",      FileCategory::Image,    {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A}, 0, {0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82}, 80ull<<20);
    Add(L"gif",  L"GIF Image",      FileCategory::Image,    {0x47,0x49,0x46,0x38}, 0, {0x00,0x3B}, 40ull<<20);
    Add(L"bmp",  L"Bitmap Image",   FileCategory::Image,    {0x42,0x4D}, 0, {}, 100ull<<20);
    Add(L"tif",  L"TIFF Image",     FileCategory::Image,    {0x49,0x49,0x2A,0x00}, 0, {}, 200ull<<20);
    Add(L"tiff", L"TIFF Image",     FileCategory::Image,    {0x4D,0x4D,0x00,0x2A}, 0, {}, 200ull<<20);
    Add(L"mp4",  L"MP4 Video",      FileCategory::Video,    {0x66,0x74,0x79,0x70}, 4, {}, 2ull<<30);
    Add(L"mov",  L"QuickTime Video",FileCategory::Video,    {0x66,0x74,0x79,0x70}, 4, {}, 2ull<<30);
    Add(L"avi",  L"AVI Video",      FileCategory::Video,    {0x52,0x49,0x46,0x46}, 0, {}, 2ull<<30);
    Add(L"mkv",  L"Matroska Video", FileCategory::Video,    {0x1A,0x45,0xDF,0xA3}, 0, {}, 4ull<<30);
    Add(L"mp3",  L"MP3 Audio",      FileCategory::Audio,    {0x49,0x44,0x33}, 0, {}, 100ull<<20);
    Add(L"wav",  L"WAV Audio",      FileCategory::Audio,    {0x52,0x49,0x46,0x46}, 0, {}, 200ull<<20);
    Add(L"pdf",  L"PDF Document",   FileCategory::Document, {0x25,0x50,0x44,0x46}, 0, {0x25,0x25,0x45,0x4F,0x46}, 100ull<<20);
    Add(L"doc",  L"Word Document",  FileCategory::Document, {0xD0,0xCF,0x11,0xE0,0xA1,0xB1,0x1A,0xE1}, 0, {}, 50ull<<20);
    Add(L"xls",  L"Excel Spreadsheet", FileCategory::Document, {}, 0, {}, 50ull<<20);
    Add(L"ppt",  L"PowerPoint",     FileCategory::Document, {}, 0, {}, 80ull<<20);
    Add(L"docx", L"Word Document",  FileCategory::Document, {}, 0, {}, 80ull<<20);
    Add(L"xlsx", L"Excel Spreadsheet", FileCategory::Document, {}, 0, {}, 80ull<<20);
    Add(L"pptx", L"PowerPoint",     FileCategory::Document, {}, 0, {}, 80ull<<20);
    Add(L"zip",  L"ZIP Archive",    FileCategory::Archive,  {0x50,0x4B,0x03,0x04}, 0, {0x50,0x4B,0x05,0x06}, 2ull<<30);
    Add(L"rar",  L"RAR Archive",    FileCategory::Archive,  {0x52,0x61,0x72,0x21,0x1A,0x07}, 0, {}, 2ull<<30);
    Add(L"7z",   L"7-Zip Archive",  FileCategory::Archive,  {0x37,0x7A,0xBC,0xAF,0x27,0x1C}, 0, {}, 2ull<<30);
    Add(L"iso",  L"ISO Image",      FileCategory::Archive,  {0x43,0x44,0x30,0x30,0x31}, 0x8001, {}, 8ull<<30);
    Add(L"db",   L"SQLite Database",FileCategory::Database, {0x53,0x51,0x4C,0x69,0x74,0x65,0x20,0x66,0x6F,0x72,0x6D,0x61,0x74,0x20,0x33,0x00}, 0, {}, 1ull<<30);
    Add(L"sqlite", L"SQLite Database", FileCategory::Database, {0x53,0x51,0x4C,0x69,0x74,0x65,0x20,0x66,0x6F,0x72,0x6D,0x61,0x74,0x20,0x33,0x00}, 0, {}, 1ull<<30);
    Add(L"txt",  L"Text File",      FileCategory::Text,     {}, 0, {}, 20ull<<20);
    Add(L"csv",  L"CSV File",       FileCategory::Text,     {}, 0, {}, 20ull<<20);
}

void SignatureDb::Add(const wchar_t* ext, const wchar_t* type, FileCategory cat,
                      std::initializer_list<uint8_t> header, size_t headerOffset,
                      std::initializer_list<uint8_t> footer, uint64_t maxSize) {
    FileSignature sig;
    sig.extension = ext;
    sig.typeName = type;
    sig.category = cat;
    sig.header.assign(header);
    sig.headerOffset = headerOffset;
    sig.footer.assign(footer);
    sig.maxSize = maxSize;
    signatures_.push_back(std::move(sig));
}

std::vector<FileSignature> SignatureDb::Enabled() const {
    std::vector<FileSignature> out;
    for (const auto& s : signatures_) {
        if (s.enabled && !s.header.empty()) {
            out.push_back(s);
        }
    }
    return out;
}

void SignatureDb::SetEnabledExtensions(const std::vector<std::wstring>& extensions) {
    if (extensions.empty()) {
        for (auto& s : signatures_) s.enabled = true;
        return;
    }
    for (auto& s : signatures_) {
        s.enabled = false;
        for (const auto& ext : extensions) {
            if (EqualsIgnoreCase(s.extension, ext)) {
                s.enabled = true;
                break;
            }
        }
    }
}

FileCategory SignatureDb::CategoryFromExtension(const std::wstring& extension) {
    auto ext = ToLower(extension);
    if (ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"gif" ||
        ext == L"bmp" || ext == L"tif" || ext == L"tiff") return FileCategory::Image;
    if (ext == L"mp4" || ext == L"mov" || ext == L"avi" || ext == L"mkv") return FileCategory::Video;
    if (ext == L"mp3" || ext == L"wav") return FileCategory::Audio;
    if (ext == L"pdf" || ext == L"doc" || ext == L"docx" || ext == L"xls" ||
        ext == L"xlsx" || ext == L"ppt" || ext == L"pptx") return FileCategory::Document;
    if (ext == L"zip" || ext == L"rar" || ext == L"7z" || ext == L"iso") return FileCategory::Archive;
    if (ext == L"db" || ext == L"sqlite") return FileCategory::Database;
    if (ext == L"txt" || ext == L"csv") return FileCategory::Text;
    return FileCategory::Other;
}

const FileSignature* SignatureDb::FindByExtension(const std::wstring& extension) const {
    auto ext = ToLower(extension);
    for (const auto& s : signatures_) {
        if (s.extension == ext) {
            return &s;
        }
    }
    return nullptr;
}

SignatureMatch SignatureDb::Identify(const std::wstring& extension, const uint8_t* data, size_t size) const {
    SignatureMatch match;
    match.extension = ToLower(extension);
    if (const FileSignature* sig = FindByExtension(extension)) {
        match.category = sig->category;
        match.typeName = sig->typeName;
        match.extension = sig->extension;
        return match;
    }
    if (data && size) {
        for (const auto& s : signatures_) {
            if (s.header.empty()) continue;
            if (s.headerOffset + s.header.size() > size) continue;
            if (memcmp(data + s.headerOffset, s.header.data(), s.header.size()) == 0) {
                match.category = s.category;
                match.typeName = s.typeName;
                match.extension = s.extension;
                return match;
            }
        }
    }
    match.category = CategoryFromExtension(extension);
    match.typeName = match.category == FileCategory::Other ? L"Unknown" : ToString(match.category);
    return match;
}

uint64_t SignatureDb::InferSize(const FileSignature& sig, const uint8_t* data, size_t available, uint64_t volumeRemain) {
    uint64_t cap = sig.maxSize;
    if (volumeRemain > 0 && volumeRemain < cap) {
        cap = volumeRemain;
    }

    if (sig.extension == L"bmp" && available >= 6) {
        uint32_t size = Read32LE(data + 2);
        if (size >= 14 && size <= cap) return size;
    }
    if ((sig.extension == L"wav" || sig.extension == L"avi") && available >= 8) {
        uint32_t size = Read32LE(data + 4) + 8;
        if (size >= 12 && size <= cap) return size;
    }
    if (sig.extension == L"png" || sig.extension == L"jpg" || sig.extension == L"jpeg" ||
        sig.extension == L"gif" || sig.extension == L"pdf" || sig.extension == L"zip") {
        size_t found = FindFooter(data, (std::min)(available, static_cast<size_t>(cap)), sig.footer, sig.header.size());
        if (found != static_cast<size_t>(-1)) {
            return found;
        }
    }
    if ((sig.extension == L"mp4" || sig.extension == L"mov") && available >= 8) {
        uint32_t box = Read32BE(data);
        if (box >= 8 && box <= cap) {
            return (std::min)(cap, 32ull * 1024ull * 1024ull);
        }
    }

    if (!sig.footer.empty()) {
        return (std::min)(cap, 8ull * 1024ull * 1024ull);
    }
    return (std::min)(cap, 4ull * 1024ull * 1024ull);
}

} // namespace pdr
