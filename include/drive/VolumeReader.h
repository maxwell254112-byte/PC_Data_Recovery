#pragma once

#include "utils/WinHandle.h"
#include <cstdint>
#include <string>
#include <vector>

namespace pdr {

class VolumeReader {
public:
    VolumeReader() = default;
    ~VolumeReader() = default;

    VolumeReader(const VolumeReader&) = delete;
    VolumeReader& operator=(const VolumeReader&) = delete;

    bool Open(const std::wstring& volumePath);
    void Close();

    bool IsOpen() const { return handle_.Valid(); }
    bool IsReadOnly() const { return readOnly_; }
    uint32_t SectorSize() const { return sectorSize_; }
    uint64_t Size() const { return sizeBytes_; }
    const std::wstring& Path() const { return path_; }
    const std::wstring& LastError() const { return lastError_; }

    bool Read(uint64_t offset, void* buffer, uint32_t size);
    bool Read(uint64_t offset, std::vector<uint8_t>& buffer, uint32_t size);

private:
    bool QueryGeometry();

    WinHandle handle_;
    std::wstring path_;
    std::wstring lastError_;
    uint32_t sectorSize_ = 512;
    uint64_t sizeBytes_ = 0;
    bool readOnly_ = true;
    std::vector<uint8_t> alignBuffer_;
};

} // namespace pdr
