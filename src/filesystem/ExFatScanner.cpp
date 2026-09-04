#include "filesystem/ExFatScanner.h"
#include "drive/VolumeReader.h"
#include "carving/SignatureDb.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <cstring>
#include <queue>
#include <set>
#include <algorithm>

namespace pdr {
namespace {

#pragma pack(push, 1)
struct ExFatBoot {
    uint8_t  jump[3];
    char     oem[8];
    uint8_t  mustBeZero[53];
    uint64_t partitionOffset;
    uint64_t volumeLength;
    uint32_t fatOffset;
    uint32_t fatLength;
    uint32_t clusterHeapOffset;
    uint32_t clusterCount;
    uint32_t rootDirCluster;
    uint32_t serial;
    uint16_t fsRevision;
    uint16_t volumeFlags;
    uint8_t  bytesPerSectorShift;
    uint8_t  sectorsPerClusterShift;
    uint8_t  fatCount;
    uint8_t  driveSelect;
    uint8_t  percentInUse;
};
#pragma pack(pop)

bool LooksLikeExFat(const ExFatBoot& b) {
    return memcmp(b.oem, "EXFAT   ", 8) == 0 &&
           b.bytesPerSectorShift >= 9 && b.bytesPerSectorShift <= 12 &&
           b.sectorsPerClusterShift <= 25;
}

FILETIME ExFatTime(uint32_t stamp) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(1980 + ((stamp >> 25) & 0x7F));
    st.wMonth = static_cast<WORD>((stamp >> 21) & 0x0F);
    st.wDay = static_cast<WORD>((stamp >> 16) & 0x1F);
    st.wHour = static_cast<WORD>((stamp >> 11) & 0x1F);
    st.wMinute = static_cast<WORD>((stamp >> 5) & 0x3F);
    st.wSecond = static_cast<WORD>((stamp & 0x1F) * 2);
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    return ft;
}

} // namespace

bool ExFatScanner::Detect(VolumeReader& reader, DriveInfo& drive) {
    uint8_t raw[512]{};
    if (!reader.Read(0, raw, 512)) {
        return false;
    }
    auto boot = *reinterpret_cast<ExFatBoot*>(raw);
    if (!LooksLikeExFat(boot)) {
        return false;
    }
    drive.fileSystem = FileSystemKind::ExFat;
    drive.fileSystemName = L"exFAT";
    drive.sectorSize = 1u << boot.bytesPerSectorShift;
    drive.clusterSize = drive.sectorSize << boot.sectorsPerClusterShift;
    return true;
}

bool ExFatScanner::ScanDeleted(VolumeReader& reader, const DriveInfo& drive, ScanMode mode,
                               IScanControl& control, const FileFoundCallback& onFile,
                               const ProgressCallback& onProgress) {
    uint8_t raw[512]{};
    if (!reader.Read(0, raw, 512)) {
        return false;
    }
    auto boot = *reinterpret_cast<ExFatBoot*>(raw);
    if (!LooksLikeExFat(boot)) {
        return false;
    }

    const uint32_t sectorSize = 1u << boot.bytesPerSectorShift;
    const uint32_t clusterSize = sectorSize << boot.sectorsPerClusterShift;
    auto clusterByte = [&](uint32_t cl) -> uint64_t {
        return (static_cast<uint64_t>(boot.clusterHeapOffset) +
                static_cast<uint64_t>(cl - 2) * (1ull << boot.sectorsPerClusterShift)) * sectorSize;
    };

    std::queue<uint32_t> dirs;
    std::set<uint32_t> visited;
    dirs.push(boot.rootDirCluster);
    uint64_t found = 0;
    uint64_t scanned = 0;

    while (!dirs.empty()) {
        if (control.ShouldStop()) return true;
        control.WaitIfPaused();
        uint32_t dirCl = dirs.front();
        dirs.pop();
        if (!visited.insert(dirCl).second) continue;
        ++scanned;

        uint32_t current = dirCl;
        for (int hop = 0; hop < 2048 && current >= 2; ++hop) {
            std::vector<uint8_t> cluster(clusterSize);
            if (!reader.Read(clusterByte(current), cluster.data(), clusterSize)) {
                break;
            }

            for (uint32_t off = 0; off + 32 <= clusterSize; ) {
                uint8_t type = cluster[off];
                if (type == 0x00) {
                    goto next_dir;
                }
                const bool inUse = (type & 0x80) != 0;
                uint8_t primary = type & 0x7F;

                if (primary == 0x05) { // file directory entry
                    uint8_t secondaryCount = cluster[off + 1];
                    uint8_t attrsLo = cluster[off + 4];
                    bool isDir = (attrsLo & 0x10) != 0;
                    uint32_t create = *reinterpret_cast<uint32_t*>(&cluster[off + 8]);
                    uint32_t modify = *reinterpret_cast<uint32_t*>(&cluster[off + 12]);

                    uint32_t firstCluster = 0;
                    uint64_t dataLen = 0;
                    std::wstring name;

                    uint32_t cursor = off + 32;
                    for (uint8_t s = 0; s < secondaryCount && cursor + 32 <= clusterSize; ++s, cursor += 32) {
                        uint8_t st = cluster[cursor] & 0x7F;
                        if (st == 0x00) { // stream extension
                            firstCluster = *reinterpret_cast<uint32_t*>(&cluster[cursor + 20]);
                            dataLen = *reinterpret_cast<uint64_t*>(&cluster[cursor + 24]);
                        } else if (st == 0x01) { // file name
                            wchar_t part[16]{};
                            memcpy(part, &cluster[cursor + 2], 30);
                            name.append(part, wcsnlen(part, 15));
                        }
                    }

                    if (!inUse && !isDir && !name.empty()) {
                        RecoveredFile file;
                        file.id = 3000000 + found;
                        file.name = name;
                        file.extension = GetExtension(name);
                        file.originalPath = drive.letter + L"\\" + name;
                        file.size = dataLen;
                        file.dataLength = dataLen;
                        file.sourceVolume = drive.volumePath;
                        file.sourceLetter = drive.letter;
                        file.method = DiscoveryMethod::FileSystem;
                        file.created = ExFatTime(create);
                        file.modified = ExFatTime(modify);
                        file.hasTimestamps = modify != 0;
                        if (firstCluster >= 2) {
                            file.volumeOffset = clusterByte(firstCluster);
                            uint64_t clusters = dataLen ? ((dataLen + clusterSize - 1) / clusterSize) : 1;
                            file.runs.push_back({file.volumeOffset / clusterSize, clusters});
                        }
                        auto ident = SignatureDb::Instance().Identify(file.extension, nullptr, 0);
                        file.category = ident.category;
                        file.typeName = ident.typeName;
                        file.confidence = (firstCluster >= 2 && dataLen > 0) ? Confidence::Good : Confidence::Partial;
                        file.notes = L"Deleted exFAT directory entry (InUse bit cleared)";
                        onFile(file);
                        ++found;
                    } else if (inUse && isDir && firstCluster >= 2) {
                        dirs.push(firstCluster);
                    } else if (!inUse && isDir && mode == ScanMode::Deep && firstCluster >= 2) {
                        dirs.push(firstCluster);
                    }

                    off = cursor;
                    continue;
                }
                off += 32;
            }

            // Walk FAT for next directory cluster.
            uint64_t fatOff = static_cast<uint64_t>(boot.fatOffset) * sectorSize +
                              static_cast<uint64_t>(current) * 4ull;
            uint32_t next = 0;
            if (!reader.Read(fatOff, &next, 4) || next < 2 || next == 0xFFFFFFFF) {
                break;
            }
            current = next;
        }
    next_dir:
        if ((scanned & 0x0F) == 0) {
            ScanProgress p;
            p.state = ScanState::Running;
            p.mode = mode;
            p.stage = L"Scanning exFAT directories";
            p.filesFound = found;
            p.percent = (std::min)(95.0, scanned * 3.0);
            onProgress(p);
        }
    }

    ScanProgress done;
    done.state = ScanState::Running;
    done.mode = mode;
    done.stage = L"exFAT scan complete";
    done.percent = 100.0;
    done.filesFound = found;
    onProgress(done);
    (void)mode;
    return true;
}

} // namespace pdr
