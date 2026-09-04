#pragma once

#include "utils/Types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace pdr {

struct FileSignature {
    std::wstring extension;
    std::wstring typeName;
    FileCategory category = FileCategory::Other;
    std::vector<uint8_t> header;
    size_t headerOffset = 0;
    std::vector<uint8_t> footer;
    uint64_t maxSize = 64ull * 1024ull * 1024ull;
    bool enabled = true;
};

struct SignatureMatch {
    FileCategory category = FileCategory::Other;
    std::wstring typeName = L"Unknown";
    std::wstring extension;
};

class SignatureDb {
public:
    static SignatureDb& Instance();

    const std::vector<FileSignature>& All() const { return signatures_; }
    std::vector<FileSignature> Enabled() const;
    void SetEnabledExtensions(const std::vector<std::wstring>& extensions);
    SignatureMatch Identify(const std::wstring& extension, const uint8_t* data, size_t size) const;
    const FileSignature* FindByExtension(const std::wstring& extension) const;
    static FileCategory CategoryFromExtension(const std::wstring& extension);
    static uint64_t InferSize(const FileSignature& sig, const uint8_t* data, size_t available, uint64_t volumeRemain);

private:
    SignatureDb();
    void Add(const wchar_t* ext, const wchar_t* type, FileCategory cat,
             std::initializer_list<uint8_t> header, size_t headerOffset,
             std::initializer_list<uint8_t> footer, uint64_t maxSize);

    std::vector<FileSignature> signatures_;
};

} // namespace pdr
