#include "drive/VolumeReader.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <winioctl.h>
#include <cstring>

namespace pdr {

bool VolumeReader::QueryGeometry() {
    DISK_GEOMETRY_EX geo{};
    DWORD bytes = 0;
    if (DeviceIoControl(handle_.Get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0,
                        &geo, sizeof(geo), &bytes, nullptr)) {
        sectorSize_ = geo.Geometry.BytesPerSector ? geo.Geometry.BytesPerSector : 512;
        sizeBytes_ = static_cast<uint64_t>(geo.DiskSize.QuadPart);
        return true;
    }

    GET_LENGTH_INFORMATION length{};
    if (DeviceIoControl(handle_.Get(), IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                        &length, sizeof(length), &bytes, nullptr)) {
        sizeBytes_ = static_cast<uint64_t>(length.Length.QuadPart);
    }
    return sizeBytes_ > 0;
}

bool VolumeReader::Open(const std::wstring& volumePath) {
    Close();
    path_ = volumePath;
    lastError_.clear();

    HANDLE h = CreateFileW(
        volumePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS,
        nullptr);

    if (h == INVALID_HANDLE_VALUE) {
        lastError_ = LastErrorMessage();
        Logger::Instance().Error(L"Failed to open " + volumePath + L" read-only: " + lastError_);
        return false;
    }

    handle_.Reset(h);
    readOnly_ = true;
    if (!QueryGeometry()) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(handle_.Get(), &size)) {
            sizeBytes_ = static_cast<uint64_t>(size.QuadPart);
        }
    }
    if (sectorSize_ == 0) {
        sectorSize_ = 512;
    }
    Logger::Instance().Info(L"Opened volume read-only: " + volumePath +
                            L" size=" + std::to_wstring(sizeBytes_));
    return true;
}

void VolumeReader::Close() {
    handle_.Reset();
    path_.clear();
    sizeBytes_ = 0;
}

bool VolumeReader::Read(uint64_t offset, void* buffer, uint32_t size) {
    if (!handle_.Valid() || !buffer || size == 0) {
        return false;
    }
    if (sizeBytes_ > 0 && offset + size > sizeBytes_) {
        if (offset >= sizeBytes_) {
            return false;
        }
        size = static_cast<uint32_t>(sizeBytes_ - offset);
    }

    const uint32_t sector = sectorSize_ ? sectorSize_ : 512;
    const uint64_t alignedOffset = offset - (offset % sector);
    const uint32_t lead = static_cast<uint32_t>(offset - alignedOffset);
    const uint32_t total = ((lead + size + sector - 1) / sector) * sector;

    if (lead == 0 && (size % sector) == 0) {
        LARGE_INTEGER li{};
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_.Get(), li, nullptr, FILE_BEGIN)) {
            lastError_ = LastErrorMessage();
            return false;
        }
        DWORD read = 0;
        if (!ReadFile(handle_.Get(), buffer, size, &read, nullptr) || read < size) {
            lastError_ = LastErrorMessage();
            return false;
        }
        return true;
    }

    if (alignBuffer_.size() < total) {
        alignBuffer_.resize(total);
    }

    LARGE_INTEGER li{};
    li.QuadPart = static_cast<LONGLONG>(alignedOffset);
    if (!SetFilePointerEx(handle_.Get(), li, nullptr, FILE_BEGIN)) {
        lastError_ = LastErrorMessage();
        return false;
    }
    DWORD read = 0;
    if (!ReadFile(handle_.Get(), alignBuffer_.data(), total, &read, nullptr) || read < lead + size) {
        lastError_ = LastErrorMessage();
        return false;
    }
    memcpy(buffer, alignBuffer_.data() + lead, size);
    return true;
}

bool VolumeReader::Read(uint64_t offset, std::vector<uint8_t>& buffer, uint32_t size) {
    buffer.resize(size);
    if (!Read(offset, buffer.data(), size)) {
        buffer.clear();
        return false;
    }
    return true;
}

} // namespace pdr
