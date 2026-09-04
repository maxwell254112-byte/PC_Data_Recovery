#include "filesystem/Fat32Scanner.h"
#include "drive/VolumeReader.h"
#include "carving/SignatureDb.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <cctype>
#include <cstring>
#include <queue>
#include <set>
#include <algorithm>

namespace pdr {
namespace {

#pragma pack(push, 1)
struct Fat32Boot {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytesPerSector;
    uint8_t  sectorsPerCluster;
    uint16_t reservedSectors;
    uint8_t  fatCount;
    uint16_t rootEntries;
    uint16_t totalSectors16;
    uint8_t  media;
    uint16_t fatSize16;
    uint16_t sectorsPerTrack;
    uint16_t heads;
    uint32_t hiddenSectors;
    uint32_t totalSectors32;
    uint32_t fatSize32;
    uint16_t extFlags;
    uint16_t fsVersion;
    uint32_t rootCluster;
    uint16_t fsInfoSector;
    uint16_t backupBoot;
    uint8_t  reserved[12];
    uint8_t  driveNumber;
    uint8_t  reserved1;
    uint8_t  bootSig;
    uint32_t volumeId;
    char     volumeLabel[11];
    char     fsType[8];
};
#pragma pack(pop)

struct DirEntry {
    uint8_t  name[11];
    uint8_t  attr;
    uint8_t  ntRes;
    uint8_t  createTenth;
    uint16_t createTime;
    uint16_t createDate;
    uint16_t accessDate;
    uint16_t clusterHi;
    uint16_t writeTime;
    uint16_t writeDate;
    uint16_t clusterLo;
    uint32_t fileSize;
};

constexpr uint8_t ATTR_READ_ONLY = 0x01;
constexpr uint8_t ATTR_HIDDEN    = 0x02;
constexpr uint8_t ATTR_SYSTEM    = 0x04;
constexpr uint8_t ATTR_VOLUME_ID = 0x08;
constexpr uint8_t ATTR_DIRECTORY = 0x10;
constexpr uint8_t ATTR_ARCHIVE   = 0x20;
constexpr uint8_t ATTR_LONG_NAME = 0x0F;

bool LooksLikeFat32(const Fat32Boot& b) {
    if (b.bytesPerSector != 512 && b.bytesPerSector != 1024 &&
        b.bytesPerSector != 2048 && b.bytesPerSector != 4096) {
        return false;
    }
    if (b.sectorsPerCluster == 0 || b.fatCount == 0) {
        return false;
    }
    if (b.fatSize16 != 0 || b.fatSize32 == 0) {
        return false;
    }
    char fs[9]{};
    memcpy(fs, b.fsType, 8);
    std::string type = fs;
    for (char& c : type) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return type.find("fat32") != std::string::npos || b.rootCluster >= 2;
}

std::wstring ShortNameFrom83(const uint8_t name[11], bool deleted) {
    std::wstring out;
    char base[9]{};
    char ext[4]{};
    memcpy(base, name, 8);
    memcpy(ext, name + 8, 3);
    if (deleted) {
        base[0] = '?';
    }
    for (int i = 7; i >= 0 && base[i] == ' '; --i) base[i] = 0;
    for (int i = 2; i >= 0 && ext[i] == ' '; --i) ext[i] = 0;
    for (int i = 0; base[i]; ++i) {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(base[i])));
    }
    if (ext[0]) {
        out.push_back(L'.');
        for (int i = 0; ext[i]; ++i) {
            out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ext[i])));
        }
    }
    return out.empty() ? L"?DELETED" : out;
}

FILETIME FatDateTime(uint16_t date, uint16_t time) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(1980 + ((date >> 9) & 0x7F));
    st.wMonth = static_cast<WORD>((date >> 5) & 0x0F);
    st.wDay = static_cast<WORD>(date & 0x1F);
    st.wHour = static_cast<WORD>((time >> 11) & 0x1F);
    st.wMinute = static_cast<WORD>((time >> 5) & 0x3F);
    st.wSecond = static_cast<WORD>((time & 0x1F) * 2);
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    return ft;
}

uint32_t FirstCluster(const DirEntry& e) {
    return (static_cast<uint32_t>(e.clusterHi) << 16) | e.clusterLo;
}

bool ReadFatEntry(VolumeReader& reader, const Fat32Boot& boot, uint32_t cluster, uint32_t& value) {
    uint64_t fatOffset = static_cast<uint64_t>(boot.reservedSectors) * boot.bytesPerSector;
    uint64_t entryOff = fatOffset + static_cast<uint64_t>(cluster) * 4ull;
    uint32_t raw = 0;
    if (!reader.Read(entryOff, &raw, 4)) {
        return false;
    }
    value = raw & 0x0FFFFFFF;
    return true;
}

std::vector<DataRun> ChainRuns(VolumeReader& reader, const Fat32Boot& boot,
                               uint32_t start, uint32_t fileSize, bool& intact) {
    std::vector<DataRun> runs;
    intact = true;
    if (start < 2) {
        intact = false;
        return runs;
    }
    const uint32_t clusterSize = static_cast<uint32_t>(boot.bytesPerSector) * boot.sectorsPerCluster;
    const uint64_t firstDataSector = boot.reservedSectors +
        static_cast<uint64_t>(boot.fatCount) * boot.fatSize32;
    auto clusterOffset = [&](uint32_t cl) {
        return (firstDataSector + static_cast<uint64_t>(cl - 2) * boot.sectorsPerCluster) * boot.bytesPerSector;
    };

    uint32_t current = start;
    uint32_t runStart = start;
    uint64_t runCount = 0;
    uint64_t covered = 0;
    std::set<uint32_t> seen;
    const uint32_t needClusters = fileSize ? ((fileSize + clusterSize - 1) / clusterSize) : 1;

    for (uint32_t i = 0; i < needClusters + 8 && i < 1'000'000; ++i) {
        if (!seen.insert(current).second) {
            intact = false;
            break;
        }
        if (runCount == 0) {
            runStart = current;
        }
        ++runCount;

        uint32_t next = 0;
        if (!ReadFatEntry(reader, boot, current, next)) {
            intact = false;
            break;
        }
        covered += clusterSize;
        bool eoc = next >= 0x0FFFFFF8;
        bool unused = next == 0;
        if (unused) {
            intact = false;
            break;
        }
        if (eoc || i + 1 >= needClusters) {
            runs.push_back({clusterOffset(runStart) / clusterSize, runCount});
            // Store byte LCN equivalent: we will convert using clusterSize later via volumeOffset
            break;
        }
        if (next != current + 1) {
            runs.push_back({clusterOffset(runStart) / clusterSize, runCount});
            runCount = 0;
        }
        current = next;
    }

    // Convert run start from "cluster number as LCN-like" using data-area cluster index.
    // Recovery engine uses startLcn * clusterSize, so startLcn must be byteOffset/clusterSize.
    for (auto& run : runs) {
        // already stored as clusterOffset(runStart)/clusterSize which is correct LCN-style
        (void)run;
    }
    if (runs.empty() && start >= 2) {
        uint64_t lcn = clusterOffset(start) / clusterSize;
        uint64_t count = needClusters ? needClusters : 1;
        runs.push_back({lcn, count});
        intact = false;
    }
    return runs;
}

} // namespace

bool Fat32Scanner::Detect(VolumeReader& reader, DriveInfo& drive) {
    uint8_t raw[512]{};
    if (!reader.Read(0, raw, 512)) {
        return false;
    }
    auto boot = *reinterpret_cast<Fat32Boot*>(raw);
    if (!LooksLikeFat32(boot)) {
        return false;
    }
    drive.fileSystem = FileSystemKind::Fat32;
    drive.fileSystemName = L"FAT32";
    drive.sectorSize = boot.bytesPerSector;
    drive.clusterSize = static_cast<uint32_t>(boot.bytesPerSector) * boot.sectorsPerCluster;
    return true;
}

bool Fat32Scanner::ScanDeleted(VolumeReader& reader, const DriveInfo& drive, ScanMode mode,
                               IScanControl& control, const FileFoundCallback& onFile,
                               const ProgressCallback& onProgress) {
    uint8_t raw[512]{};
    if (!reader.Read(0, raw, 512)) {
        return false;
    }
    auto boot = *reinterpret_cast<Fat32Boot*>(raw);
    if (!LooksLikeFat32(boot)) {
        return false;
    }

    const uint32_t clusterSize = static_cast<uint32_t>(boot.bytesPerSector) * boot.sectorsPerCluster;
    const uint64_t firstDataSector = boot.reservedSectors +
        static_cast<uint64_t>(boot.fatCount) * boot.fatSize32;
    auto clusterByte = [&](uint32_t cl) -> uint64_t {
        return (firstDataSector + static_cast<uint64_t>(cl - 2) * boot.sectorsPerCluster) * boot.bytesPerSector;
    };

    std::queue<uint32_t> dirs;
    std::set<uint32_t> visited;
    dirs.push(boot.rootCluster);
    uint64_t found = 0;
    uint64_t scannedDirs = 0;

    while (!dirs.empty()) {
        if (control.ShouldStop()) return true;
        control.WaitIfPaused();

        uint32_t dirCluster = dirs.front();
        dirs.pop();
        if (!visited.insert(dirCluster).second) {
            continue;
        }
        ++scannedDirs;

        uint32_t current = dirCluster;
        std::set<uint32_t> chainSeen;
        for (int step = 0; step < 4096 && current >= 2; ++step) {
            if (!chainSeen.insert(current).second) break;
            std::vector<uint8_t> cluster(clusterSize);
            if (!reader.Read(clusterByte(current), cluster.data(), clusterSize)) {
                break;
            }
            for (uint32_t off = 0; off + 32 <= clusterSize; off += 32) {
                auto* e = reinterpret_cast<DirEntry*>(cluster.data() + off);
                if (e->name[0] == 0x00) {
                    goto next_dir;
                }
                if ((e->attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) {
                    continue;
                }
                if (e->attr & ATTR_VOLUME_ID) {
                    continue;
                }
                const bool deleted = (e->name[0] == 0xE5);
                const bool isDir = (e->attr & ATTR_DIRECTORY) != 0;
                uint32_t cl = FirstCluster(*e);
                if (isDir && !deleted && cl >= 2) {
                    std::wstring n = ShortNameFrom83(e->name, false);
                    if (n != L"." && n != L"..") {
                        dirs.push(cl);
                    }
                    continue;
                }
                if (!deleted) {
                    continue;
                }
                if (isDir) {
                    if (mode == ScanMode::Deep && cl >= 2) {
                        dirs.push(cl);
                    }
                    continue;
                }

                RecoveredFile file;
                file.id = 2000000 + found;
                file.name = ShortNameFrom83(e->name, true);
                file.extension = GetExtension(file.name);
                file.originalPath = drive.letter + L"\\" + file.name;
                file.size = e->fileSize;
                file.dataLength = e->fileSize;
                file.sourceVolume = drive.volumePath;
                file.sourceLetter = drive.letter;
                file.method = DiscoveryMethod::FileSystem;
                file.created = FatDateTime(e->createDate, e->createTime);
                file.modified = FatDateTime(e->writeDate, e->writeTime);
                file.hasTimestamps = e->writeDate != 0;
                file.volumeOffset = cl >= 2 ? clusterByte(cl) : 0;

                bool intact = false;
                auto fatRuns = ChainRuns(reader, boot, cl, e->fileSize, intact);
                file.runs.clear();
                for (const auto& r : fatRuns) {
                    file.runs.push_back({r.startLcn, r.clusterCount});
                }
                if (file.runs.empty() && cl >= 2 && e->fileSize) {
                    uint64_t count = (e->fileSize + clusterSize - 1) / clusterSize;
                    file.runs.push_back({file.volumeOffset / clusterSize, count});
                    intact = false;
                }

                auto ident = SignatureDb::Instance().Identify(file.extension, nullptr, 0);
                file.category = ident.category;
                file.typeName = ident.typeName;
                if (intact && e->fileSize > 0) {
                    file.confidence = Confidence::Good;
                    file.notes = L"Deleted FAT32 entry; cluster chain still present";
                } else if (e->fileSize > 0 && cl >= 2) {
                    file.confidence = Confidence::Partial;
                    file.notes = L"Deleted FAT32 entry; first character lost, cluster chain may be reused";
                } else {
                    file.confidence = Confidence::Corrupted;
                    file.notes = L"Deleted FAT32 entry without usable cluster data";
                }
                onFile(file);
                ++found;
            }

            uint32_t next = 0;
            if (!ReadFatEntry(reader, boot, current, next) || next < 2 || next >= 0x0FFFFFF8) {
                break;
            }
            current = next;
        }

    next_dir:
        if ((scannedDirs & 0x1F) == 0) {
            ScanProgress p;
            p.state = ScanState::Running;
            p.mode = mode;
            p.stage = L"Scanning FAT32 directories";
            p.filesFound = found;
            p.percent = (std::min)(95.0, scannedDirs * 2.0);
            onProgress(p);
        }
    }

    ScanProgress done;
    done.state = ScanState::Running;
    done.mode = mode;
    done.stage = L"FAT32 scan complete";
    done.percent = 100.0;
    done.filesFound = found;
    onProgress(done);
    return true;
}

} // namespace pdr
