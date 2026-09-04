#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <windows.h>

namespace pdr {

enum class DriveKind {
    Unknown,
    InternalHdd,
    InternalSsd,
    ExternalHdd,
    UsbFlash,
    UsbSsd,
    MemoryCard,
    Optical,
    Virtual
};

enum class FileSystemKind {
    Unknown,
    Ntfs,
    Fat32,
    Fat16,
    ExFat,
    ReFs,
    Other
};

enum class ScanMode {
    Quick,
    Deep,
    Raw
};

enum class ScanState {
    Idle,
    Running,
    Paused,
    Cancelling,
    Completed,
    Failed
};

enum class Confidence {
    Excellent,
    Good,
    Partial,
    Corrupted
};

enum class DiscoveryMethod {
    FileSystem,
    RecycleBin,
    RawCarve
};

enum class RecoveryStatus {
    Pending,
    Recovered,
    Failed,
    Skipped,
    Unsupported
};

enum class FileCategory {
    Image,
    Video,
    Audio,
    Document,
    Archive,
    Database,
    Text,
    Other
};

struct PhysicalDiskInfo {
    uint32_t diskNumber = 0xFFFFFFFF;
    std::wstring model;
    std::wstring serial;
    std::wstring busType;
    std::wstring mediaType;
    uint64_t sizeBytes = 0;
    bool rotational = true;
};

struct DriveInfo {
    std::wstring letter;          // "C:" or empty for unmounted
    std::wstring volumePath;      // "\\\\.\\C:"
    std::wstring guidPath;
    std::wstring label;
    std::wstring fileSystemName;
    FileSystemKind fileSystem = FileSystemKind::Unknown;
    DriveKind kind = DriveKind::Unknown;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
    uint32_t clusterSize = 0;
    uint32_t sectorSize = 512;
    bool removable = false;
    bool ready = false;
    PhysicalDiskInfo physical;
};

struct DataRun {
    uint64_t startLcn = 0;
    uint64_t clusterCount = 0;
};

struct RecoveredFile {
    uint64_t id = 0;
    std::wstring name;
    std::wstring extension;
    std::wstring originalPath;
    uint64_t size = 0;
    FileCategory category = FileCategory::Other;
    std::wstring typeName;
    FILETIME created{};
    FILETIME modified{};
    bool hasTimestamps = false;
    Confidence confidence = Confidence::Partial;
    DiscoveryMethod method = DiscoveryMethod::FileSystem;
    RecoveryStatus status = RecoveryStatus::Pending;
    std::wstring sourceVolume;
    std::wstring sourceLetter;
    uint64_t volumeOffset = 0;
    uint64_t dataLength = 0;
    std::vector<DataRun> runs;
    std::vector<uint8_t> residentData;
    bool resident = false;
    bool directory = false;
    bool checked = false;
    std::wstring localSourcePath;
    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
    std::wstring notes;
};

struct ScanProgress {
    ScanState state = ScanState::Idle;
    ScanMode mode = ScanMode::Quick;
    double percent = 0.0;
    uint64_t filesFound = 0;
    uint64_t bytesScanned = 0;
    uint64_t bytesTotal = 0;
    double bytesPerSecond = 0.0;
    uint64_t elapsedMs = 0;
    uint64_t etaMs = 0;
    std::wstring stage;
    std::wstring error;
};

struct RecoveryItemResult {
    uint64_t fileId = 0;
    std::wstring name;
    std::wstring destPath;
    bool success = false;
    std::wstring error;
};

struct RecoveryProgress {
    uint64_t current = 0;
    uint64_t total = 0;
    uint64_t succeeded = 0;
    uint64_t failed = 0;
    std::wstring currentName;
};

struct HistoryRecord {
    int64_t id = 0;
    std::wstring timestamp;
    std::wstring sourceDrive;
    std::wstring destination;
    std::wstring scanMode;
    uint64_t filesRecovered = 0;
    uint64_t filesFailed = 0;
    uint64_t durationMs = 0;
};

struct AppConfig {
    std::wstring defaultRecoveryFolder;
    std::wstring theme = L"dark";
    bool previewImages = true;
    bool previewPdf = true;
    bool previewText = true;
    uint64_t maxFileSize = 4ull * 1024ull * 1024ull * 1024ull;
    uint32_t carveChunkKb = 1024;
    bool scanRecycleBin = true;
    bool warnBeforeScan = true;
    std::vector<std::wstring> enabledExtensions;
};

inline const wchar_t* ToString(DriveKind k) {
    switch (k) {
        case DriveKind::InternalHdd: return L"Internal HDD";
        case DriveKind::InternalSsd: return L"Internal SSD";
        case DriveKind::ExternalHdd: return L"External HDD";
        case DriveKind::UsbFlash:    return L"USB Flash Drive";
        case DriveKind::UsbSsd:      return L"USB External SSD";
        case DriveKind::MemoryCard:  return L"Memory Card";
        case DriveKind::Optical:     return L"Optical Drive";
        case DriveKind::Virtual:     return L"Virtual Drive";
        default:                     return L"Unknown";
    }
}

inline const wchar_t* ToString(FileSystemKind k) {
    switch (k) {
        case FileSystemKind::Ntfs:  return L"NTFS";
        case FileSystemKind::Fat32: return L"FAT32";
        case FileSystemKind::Fat16: return L"FAT16";
        case FileSystemKind::ExFat: return L"exFAT";
        case FileSystemKind::ReFs:  return L"ReFS";
        case FileSystemKind::Other: return L"Other";
        default:                    return L"Unknown";
    }
}

inline const wchar_t* ToString(ScanMode m) {
    switch (m) {
        case ScanMode::Quick: return L"Quick Scan";
        case ScanMode::Deep:  return L"Deep Scan";
        case ScanMode::Raw:   return L"RAW Recovery";
        default:              return L"Unknown";
    }
}

inline const wchar_t* ToString(Confidence c) {
    switch (c) {
        case Confidence::Excellent: return L"Excellent";
        case Confidence::Good:      return L"Good";
        case Confidence::Partial:   return L"Partial";
        case Confidence::Corrupted: return L"Corrupted";
        default:                    return L"Partial";
    }
}

inline const wchar_t* ToString(DiscoveryMethod m) {
    switch (m) {
        case DiscoveryMethod::FileSystem: return L"Filesystem";
        case DiscoveryMethod::RecycleBin: return L"Recycle Bin";
        case DiscoveryMethod::RawCarve:   return L"RAW";
        default:                          return L"Unknown";
    }
}

inline const wchar_t* ToString(RecoveryStatus s) {
    switch (s) {
        case RecoveryStatus::Pending:     return L"Pending";
        case RecoveryStatus::Recovered:   return L"Recovered";
        case RecoveryStatus::Failed:      return L"Failed";
        case RecoveryStatus::Skipped:     return L"Skipped";
        case RecoveryStatus::Unsupported: return L"Unsupported";
        default:                          return L"Pending";
    }
}

inline const wchar_t* ToString(FileCategory c) {
    switch (c) {
        case FileCategory::Image:    return L"Image";
        case FileCategory::Video:    return L"Video";
        case FileCategory::Audio:    return L"Audio";
        case FileCategory::Document: return L"Document";
        case FileCategory::Archive:  return L"Archive";
        case FileCategory::Database: return L"Database";
        case FileCategory::Text:     return L"Text";
        default:                     return L"Other";
    }
}

} // namespace pdr
