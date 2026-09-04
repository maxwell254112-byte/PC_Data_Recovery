#include "drive/DriveEnumerator.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <winioctl.h>

namespace pdr {
namespace {

std::wstring BusTypeName(STORAGE_BUS_TYPE bus) {
    switch (bus) {
        case BusTypeScsi:   return L"SCSI";
        case BusTypeAtapi:  return L"ATAPI";
        case BusTypeAta:    return L"ATA";
        case BusType1394:   return L"IEEE1394";
        case BusTypeSsa:    return L"SSA";
        case BusTypeFibre:  return L"Fibre";
        case BusTypeUsb:    return L"USB";
        case BusTypeRAID:   return L"RAID";
        case BusTypeiScsi:  return L"iSCSI";
        case BusTypeSas:    return L"SAS";
        case BusTypeSata:   return L"SATA";
        case BusTypeSd:     return L"SD";
        case BusTypeMmc:    return L"MMC";
        case BusTypeVirtual:return L"Virtual";
        case BusTypeFileBackedVirtual: return L"Virtual";
        case BusTypeSpaces: return L"Spaces";
        case BusTypeNvme:   return L"NVMe";
        case BusTypeSCM:    return L"SCM";
        case BusTypeUfs:    return L"UFS";
        default:            return L"Unknown";
    }
}

bool QuerySeekPenalty(HANDLE handle, bool& rotational) {
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR desc{};
    DWORD bytes = 0;
    if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                        &desc, sizeof(desc), &bytes, nullptr)) {
        rotational = desc.IncursSeekPenalty != FALSE;
        return true;
    }
    return false;
}

} // namespace

FileSystemKind DriveEnumerator::ParseFileSystem(const std::wstring& name) {
    auto lower = ToLower(name);
    if (lower == L"ntfs") return FileSystemKind::Ntfs;
    if (lower == L"fat32") return FileSystemKind::Fat32;
    if (lower == L"fat" || lower == L"fat16") return FileSystemKind::Fat16;
    if (lower == L"exfat") return FileSystemKind::ExFat;
    if (lower == L"refs") return FileSystemKind::ReFs;
    if (!lower.empty()) return FileSystemKind::Other;
    return FileSystemKind::Unknown;
}

DriveKind DriveEnumerator::Classify(const DriveInfo& drive, DWORD bus, bool rotational) {
    if (bus == BusTypeVirtual || bus == BusTypeFileBackedVirtual) {
        return DriveKind::Virtual;
    }
    if (bus == BusTypeSd || bus == BusTypeMmc) {
        return DriveKind::MemoryCard;
    }
    const bool usb = (bus == BusTypeUsb);
    if (usb) {
        if (drive.removable && drive.totalBytes > 0 && drive.totalBytes <= 256ull * 1024ull * 1024ull * 1024ull) {
            return rotational ? DriveKind::ExternalHdd : DriveKind::UsbFlash;
        }
        return rotational ? DriveKind::ExternalHdd : DriveKind::UsbSsd;
    }
    if (drive.removable) {
        return rotational ? DriveKind::ExternalHdd : DriveKind::UsbFlash;
    }
    return rotational ? DriveKind::InternalHdd : DriveKind::InternalSsd;
}

void DriveEnumerator::FillVolumeDetails(DriveInfo& drive) {
    std::wstring root = drive.letter + L"\\";
    wchar_t label[MAX_PATH]{};
    wchar_t fsName[MAX_PATH]{};
    DWORD serial = 0, maxComp = 0, flags = 0;

    if (GetVolumeInformationW(root.c_str(), label, MAX_PATH, &serial, &maxComp, &flags, fsName, MAX_PATH)) {
        drive.label = label;
        drive.fileSystemName = fsName;
        drive.fileSystem = ParseFileSystem(fsName);
        drive.ready = true;
    }

    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
    if (GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, &totalFree)) {
        drive.totalBytes = totalBytes.QuadPart;
        drive.freeBytes = freeBytes.QuadPart;
        drive.usedBytes = drive.totalBytes >= drive.freeBytes ? drive.totalBytes - drive.freeBytes : 0;
    }

    DWORD sectorsPerCluster = 0, bytesPerSector = 0, freeClusters = 0, totalClusters = 0;
    if (GetDiskFreeSpaceW(root.c_str(), &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)) {
        drive.sectorSize = bytesPerSector;
        drive.clusterSize = sectorsPerCluster * bytesPerSector;
    }

    UINT type = GetDriveTypeW(root.c_str());
    drive.removable = (type == DRIVE_REMOVABLE);
    if (type == DRIVE_CDROM) {
        drive.kind = DriveKind::Optical;
    }
}

void DriveEnumerator::FillPhysicalDetails(DriveInfo& drive) {
    std::wstring path = L"\\\\.\\" + drive.letter;
    HANDLE handle = CreateFileW(
        path.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    STORAGE_DEVICE_NUMBER sdn{};
    DWORD bytes = 0;
    if (DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
                        &sdn, sizeof(sdn), &bytes, nullptr)) {
        drive.physical.diskNumber = sdn.DeviceNumber;
    }

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    std::vector<uint8_t> buffer(1024);
    if (DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                        buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) {
        auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
        drive.physical.busType = BusTypeName(desc->BusType);
        if (desc->ProductIdOffset) {
            drive.physical.model = Utf8ToWide(reinterpret_cast<const char*>(buffer.data() + desc->ProductIdOffset));
            drive.physical.model = Trim(drive.physical.model);
        }
        if (desc->SerialNumberOffset) {
            drive.physical.serial = Utf8ToWide(reinterpret_cast<const char*>(buffer.data() + desc->SerialNumberOffset));
            drive.physical.serial = Trim(drive.physical.serial);
        }
        bool rotational = true;
        if (!QuerySeekPenalty(handle, rotational)) {
            rotational = (desc->BusType != BusTypeNvme && desc->BusType != BusTypeSd && desc->BusType != BusTypeMmc);
            if (desc->BusType == BusTypeUsb) {
                rotational = false;
            }
        }
        drive.physical.rotational = rotational;
        drive.physical.mediaType = rotational ? L"HDD" : L"SSD";
        if (drive.kind != DriveKind::Optical) {
            drive.kind = Classify(drive, desc->BusType, rotational);
        }
    }

    DISK_GEOMETRY_EX geo{};
    if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0,
                        &geo, sizeof(geo), &bytes, nullptr)) {
        drive.physical.sizeBytes = static_cast<uint64_t>(geo.DiskSize.QuadPart);
        if (drive.sectorSize == 0) {
            drive.sectorSize = geo.Geometry.BytesPerSector;
        }
    }

    CloseHandle(handle);
}

std::vector<DriveInfo> DriveEnumerator::Enumerate() {
    std::vector<DriveInfo> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) {
            continue;
        }
        DriveInfo drive;
        drive.letter = std::wstring(1, static_cast<wchar_t>(L'A' + i)) + L":";
        drive.volumePath = L"\\\\.\\" + drive.letter;
        UINT type = GetDriveTypeW((drive.letter + L"\\").c_str());
        if (type == DRIVE_NO_ROOT_DIR) {
            continue;
        }
        FillVolumeDetails(drive);
        FillPhysicalDetails(drive);
        drives.push_back(std::move(drive));
    }
    Logger::Instance().Info(L"Enumerated " + std::to_wstring(drives.size()) + L" drives");
    return drives;
}

} // namespace pdr
