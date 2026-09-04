#include "filesystem/NtfsScanner.h"
#include "drive/VolumeReader.h"
#include "carving/SignatureDb.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <unordered_map>
#include <cstring>
#include <algorithm>

namespace pdr {
namespace {

#pragma pack(push, 1)
struct NtfsBoot {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytesPerSector;
    uint8_t  sectorsPerCluster;
    uint8_t  reserved[7];
    uint8_t  media;
    uint8_t  unused1[2];
    uint16_t sectorsPerTrack;
    uint16_t heads;
    uint8_t  unused2[8];
    uint8_t  unused3[4];
    uint64_t totalSectors;
    uint64_t mftLcn;
    uint64_t mftMirrLcn;
    int8_t   clustersPerMftRecord;
    uint8_t  pad1[3];
    int8_t   clustersPerIndex;
    uint8_t  pad2[3];
    uint64_t serial;
};

struct MftHeader {
    char     magic[4];
    uint16_t usaOffset;
    uint16_t usaCount;
    uint64_t lsn;
    uint16_t sequence;
    uint16_t hardLinks;
    uint16_t firstAttr;
    uint16_t flags;
    uint32_t usedSize;
    uint32_t allocSize;
    uint64_t baseRecord;
    uint16_t nextInstance;
};
#pragma pack(pop)

struct ParsedAttr {
    uint32_t type = 0;
    bool nonResident = false;
    bool named = false;
    std::wstring name;
    uint64_t realSize = 0;
    std::vector<uint8_t> resident;
    std::vector<DataRun> runs;
};

bool IsNtfsOem(const char* oem) {
    return memcmp(oem, "NTFS    ", 8) == 0;
}

uint32_t RecordSizeFromBoot(const NtfsBoot& boot) {
    if (boot.clustersPerMftRecord < 0) {
        return 1u << (-boot.clustersPerMftRecord);
    }
    uint32_t cluster = static_cast<uint32_t>(boot.bytesPerSector) * boot.sectorsPerCluster;
    return cluster * static_cast<uint32_t>(boot.clustersPerMftRecord);
}

bool ApplyFixup(uint8_t* record, uint32_t recordSize, uint32_t sectorSize) {
    if (recordSize < sizeof(MftHeader)) {
        return false;
    }
    auto* hdr = reinterpret_cast<MftHeader*>(record);
    if (hdr->usaOffset < sizeof(MftHeader) || hdr->usaCount < 2) {
        return memcmp(hdr->magic, "FILE", 4) == 0;
    }
    uint32_t usaEnd = static_cast<uint32_t>(hdr->usaOffset) + static_cast<uint32_t>(hdr->usaCount) * 2u;
    if (usaEnd > recordSize) {
        return false;
    }
    const uint16_t usn = *reinterpret_cast<uint16_t*>(record + hdr->usaOffset);
    for (uint16_t i = 1; i < hdr->usaCount; ++i) {
        uint32_t pos = static_cast<uint32_t>(i) * sectorSize;
        if (pos < 2 || pos > recordSize) {
            return false;
        }
        uint16_t* tail = reinterpret_cast<uint16_t*>(record + pos - 2);
        if (*tail != usn) {
            // Damaged sector; still attempt parse
        }
        *tail = *reinterpret_cast<uint16_t*>(record + hdr->usaOffset + i * 2);
    }
    return true;
}

int64_t ReadSigned(const uint8_t* p, int size) {
    if (size <= 0 || size > 8) {
        return 0;
    }
    uint64_t v = 0;
    memcpy(&v, p, static_cast<size_t>(size));
    if (p[size - 1] & 0x80) {
        for (int i = size; i < 8; ++i) {
            v |= 0xFFull << (i * 8);
        }
    }
    return static_cast<int64_t>(v);
}

uint64_t ReadUnsigned(const uint8_t* p, int size) {
    if (size <= 0 || size > 8) {
        return 0;
    }
    uint64_t v = 0;
    memcpy(&v, p, static_cast<size_t>(size));
    return v;
}

std::vector<DataRun> ParseRuns(const uint8_t* data, uint32_t length) {
    std::vector<DataRun> runs;
    uint64_t lcn = 0;
    uint32_t i = 0;
    while (i < length) {
        uint8_t header = data[i++];
        if (header == 0) {
            break;
        }
        int lenSize = header & 0x0F;
        int offSize = (header >> 4) & 0x0F;
        if (i + lenSize + offSize > length || lenSize == 0) {
            break;
        }
        uint64_t count = ReadUnsigned(data + i, lenSize);
        i += lenSize;
        if (offSize > 0) {
            lcn = static_cast<uint64_t>(static_cast<int64_t>(lcn) + ReadSigned(data + i, offSize));
            i += offSize;
            if (count > 0) {
                runs.push_back({lcn, count});
            }
        } else {
            i += offSize;
            // sparse run: skip
        }
    }
    return runs;
}

std::vector<ParsedAttr> ParseAttributes(const uint8_t* record, uint32_t usedSize) {
    std::vector<ParsedAttr> attrs;
    if (usedSize < sizeof(MftHeader)) {
        return attrs;
    }
    auto* hdr = reinterpret_cast<const MftHeader*>(record);
    uint32_t offset = hdr->firstAttr;
    while (offset + 8 < usedSize && offset + 8 < 16 * 1024) {
        uint32_t type = *reinterpret_cast<const uint32_t*>(record + offset);
        uint32_t length = *reinterpret_cast<const uint32_t*>(record + offset + 4);
        if (type == 0xFFFFFFFF || length < 16) {
            break;
        }
        if (offset + length > usedSize) {
            break;
        }
        ParsedAttr attr;
        attr.type = type;
        attr.nonResident = record[offset + 8] != 0;
        uint8_t nameLen = record[offset + 9];
        uint16_t nameOff = *reinterpret_cast<const uint16_t*>(record + offset + 10);
        attr.named = nameLen > 0;
        if (nameLen && nameOff + nameLen * 2u <= length) {
            attr.name.assign(reinterpret_cast<const wchar_t*>(record + offset + nameOff), nameLen);
        }
        if (!attr.nonResident) {
            uint32_t contentLen = *reinterpret_cast<const uint32_t*>(record + offset + 16);
            uint16_t contentOff = *reinterpret_cast<const uint16_t*>(record + offset + 20);
            if (contentOff + contentLen <= length) {
                attr.resident.assign(record + offset + contentOff, record + offset + contentOff + contentLen);
                attr.realSize = contentLen;
            }
        } else if (length >= 0x40) {
            uint16_t runOff = *reinterpret_cast<const uint16_t*>(record + offset + 32);
            attr.realSize = *reinterpret_cast<const uint64_t*>(record + offset + 48);
            if (runOff < length) {
                attr.runs = ParseRuns(record + offset + runOff, length - runOff);
            }
        }
        attrs.push_back(std::move(attr));
        offset += length;
    }
    return attrs;
}

FILETIME FileTimeFromNtfs(uint64_t stamp) {
    FILETIME ft{};
    ft.dwLowDateTime = static_cast<DWORD>(stamp);
    ft.dwHighDateTime = static_cast<DWORD>(stamp >> 32);
    return ft;
}

struct NameInfo {
    std::wstring name;
    uint64_t parentRef = 0;
    uint8_t nameSpace = 0;
};

NameInfo BestFileName(const std::vector<ParsedAttr>& attrs) {
    NameInfo best;
    for (const auto& attr : attrs) {
        if (attr.type != 0x30 || attr.resident.size() < 0x42) {
            continue;
        }
        const uint8_t* p = attr.resident.data();
        uint64_t parent = *reinterpret_cast<const uint64_t*>(p) & 0x0000FFFFFFFFFFFFULL;
        uint8_t nameLen = p[0x40];
        uint8_t ns = p[0x41];
        if (0x42 + nameLen * 2u > attr.resident.size()) {
            continue;
        }
        std::wstring name(reinterpret_cast<const wchar_t*>(p + 0x42), nameLen);
        if (best.name.empty() || ns == 1 || ns == 3) {
            best.name = name;
            best.parentRef = parent;
            best.nameSpace = ns;
            if (ns == 1 || ns == 3) {
                break;
            }
        }
    }
    return best;
}

void FillTimes(RecoveredFile& file, const std::vector<ParsedAttr>& attrs) {
    for (const auto& attr : attrs) {
        if (attr.type == 0x10 && attr.resident.size() >= 0x20) {
            const uint8_t* p = attr.resident.data();
            file.created = FileTimeFromNtfs(*reinterpret_cast<const uint64_t*>(p));
            file.modified = FileTimeFromNtfs(*reinterpret_cast<const uint64_t*>(p + 8));
            file.hasTimestamps = true;
            break;
        }
    }
}

const ParsedAttr* UnnamedData(const std::vector<ParsedAttr>& attrs) {
    const ParsedAttr* first = nullptr;
    for (const auto& attr : attrs) {
        if (attr.type == 0x80) {
            if (!attr.named) {
                return &attr;
            }
            if (!first) {
                first = &attr;
            }
        }
    }
    return first;
}

Confidence ScoreNtfs(const RecoveredFile& file, bool inUse) {
    if (file.size == 0 && file.residentData.empty() && file.runs.empty()) {
        return Confidence::Corrupted;
    }
    if (inUse) {
        return Confidence::Good;
    }
    if (file.resident || (!file.runs.empty() && file.size > 0)) {
        return file.size < 64 ? Confidence::Good : Confidence::Excellent;
    }
    return Confidence::Partial;
}

} // namespace

bool NtfsScanner::Detect(VolumeReader& reader, DriveInfo& drive) {
    uint8_t boot[512]{};
    if (!reader.Read(0, boot, 512)) {
        return false;
    }
    auto* b = reinterpret_cast<NtfsBoot*>(boot);
    if (!IsNtfsOem(b->oem) || b->bytesPerSector == 0 || b->sectorsPerCluster == 0) {
        return false;
    }
    drive.fileSystem = FileSystemKind::Ntfs;
    drive.fileSystemName = L"NTFS";
    drive.sectorSize = b->bytesPerSector;
    drive.clusterSize = static_cast<uint32_t>(b->bytesPerSector) * b->sectorsPerCluster;
    return true;
}

bool NtfsScanner::ScanDeleted(VolumeReader& reader, const DriveInfo& drive, ScanMode mode,
                              IScanControl& control, const FileFoundCallback& onFile,
                              const ProgressCallback& onProgress) {
    uint8_t bootRaw[512]{};
    if (!reader.Read(0, bootRaw, 512)) {
        return false;
    }
    auto boot = *reinterpret_cast<NtfsBoot*>(bootRaw);
    if (!IsNtfsOem(boot.oem)) {
        return false;
    }

    const uint32_t sectorSize = boot.bytesPerSector ? boot.bytesPerSector : 512;
    const uint32_t clusterSize = sectorSize * (boot.sectorsPerCluster ? boot.sectorsPerCluster : 8);
    const uint32_t recordSize = RecordSizeFromBoot(boot);
    if (recordSize < 1024 || recordSize > 4096) {
        Logger::Instance().Error(L"Unsupported NTFS MFT record size");
        return false;
    }

    const uint64_t mftOffset = boot.mftLcn * clusterSize;

    // Read first MFT record ($MFT) to locate the full table.
    std::vector<uint8_t> first(recordSize);
    if (!reader.Read(mftOffset, first.data(), recordSize)) {
        return false;
    }
    ApplyFixup(first.data(), recordSize, sectorSize);
    auto firstAttrs = ParseAttributes(first.data(),
        std::min(recordSize, reinterpret_cast<MftHeader*>(first.data())->usedSize));
    const ParsedAttr* mftData = UnnamedData(firstAttrs);

    std::vector<DataRun> mftRuns;
    uint64_t mftBytes = 0;
    if (mftData && !mftData->runs.empty()) {
        mftRuns = mftData->runs;
        mftBytes = mftData->realSize;
    } else {
        mftRuns.push_back({boot.mftLcn, 16});
        mftBytes = 16ull * clusterSize;
    }
    if (mftBytes == 0) {
        for (const auto& run : mftRuns) {
            mftBytes += run.clusterCount * clusterSize;
        }
    }

    const uint64_t recordCount = mftBytes / recordSize;
    std::unordered_map<uint64_t, std::wstring> names;
    names[5] = L".";

    uint64_t processed = 0;
    auto report = [&](const wchar_t* stage) {
        ScanProgress p;
        p.state = ScanState::Running;
        p.mode = mode;
        p.bytesTotal = mftBytes;
        p.bytesScanned = processed * recordSize;
        p.percent = recordCount ? (100.0 * processed / recordCount) : 0.0;
        p.filesFound = 0;
        p.stage = stage;
        onProgress(p);
    };

    std::vector<uint8_t> record(recordSize);
    uint64_t nextIdBase = 1000000;

    auto readRecord = [&](uint64_t index, uint8_t* dest) -> bool {
        uint64_t byteOff = index * recordSize;
        uint64_t remaining = byteOff;
        for (const auto& run : mftRuns) {
            uint64_t runBytes = run.clusterCount * clusterSize;
            if (remaining < runBytes) {
                return reader.Read(run.startLcn * clusterSize + remaining, dest, recordSize);
            }
            remaining -= runBytes;
        }
        return false;
    };

    // First pass: collect names of in-use records to rebuild paths.
    for (uint64_t i = 0; i < recordCount; ++i) {
        if (control.ShouldStop()) return true;
        control.WaitIfPaused();
        if (!readRecord(i, record.data())) {
            continue;
        }
        if (memcmp(record.data(), "FILE", 4) != 0) {
            continue;
        }
        ApplyFixup(record.data(), recordSize, sectorSize);
        auto* hdr = reinterpret_cast<MftHeader*>(record.data());
        if ((hdr->flags & 0x01) == 0) {
            continue;
        }
        uint32_t used = std::min(hdr->usedSize, recordSize);
        auto attrs = ParseAttributes(record.data(), used);
        auto ni = BestFileName(attrs);
        if (!ni.name.empty()) {
            names[i] = ni.name;
        }
        if ((i & 0x3FF) == 0) {
            processed = i;
            report(L"Indexing NTFS MFT");
        }
    }

    auto buildPath = [&](uint64_t parent, const std::wstring& name) {
        std::wstring path = drive.letter + L"\\";
        std::vector<std::wstring> parts;
        uint64_t cur = parent;
        for (int depth = 0; depth < 32 && cur != 5 && cur != 0; ++depth) {
            auto it = names.find(cur);
            if (it == names.end()) {
                break;
            }
            parts.push_back(it->second);
            break; // parent-of-parent needs extra map; one level is honest
        }
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            path += *it;
            path += L"\\";
        }
        path += name;
        return path;
    };

    for (uint64_t i = 16; i < recordCount; ++i) {
        if (control.ShouldStop()) return true;
        control.WaitIfPaused();
        if (!readRecord(i, record.data())) {
            continue;
        }
        if (memcmp(record.data(), "FILE", 4) != 0) {
            continue;
        }
        ApplyFixup(record.data(), recordSize, sectorSize);
        auto* hdr = reinterpret_cast<MftHeader*>(record.data());
        const bool inUse = (hdr->flags & 0x01) != 0;
        const bool isDir = (hdr->flags & 0x02) != 0;
        if (isDir) {
            continue;
        }
        if (mode == ScanMode::Quick && inUse) {
            continue;
        }
        if (mode == ScanMode::Deep && inUse) {
            continue; // Deep still targets deleted/orphaned, not live files
        }

        uint32_t used = std::min(hdr->usedSize, recordSize);
        auto attrs = ParseAttributes(record.data(), used);
        auto ni = BestFileName(attrs);
        if (ni.name.empty()) {
            ni.name = L"File_" + std::to_wstring(i);
        }

        const ParsedAttr* data = UnnamedData(attrs);
        RecoveredFile file;
        file.id = nextIdBase + i;
        file.name = ni.name;
        file.extension = GetExtension(ni.name);
        file.originalPath = buildPath(ni.parentRef, ni.name);
        file.sourceVolume = drive.volumePath;
        file.sourceLetter = drive.letter;
        file.method = DiscoveryMethod::FileSystem;
        file.status = RecoveryStatus::Pending;
        file.directory = false;
        FillTimes(file, attrs);

        if (data) {
            file.size = data->realSize;
            file.dataLength = data->realSize;
            file.resident = !data->nonResident;
            file.residentData = data->resident;
            file.runs = data->runs;
            if (!file.runs.empty()) {
                file.volumeOffset = file.runs[0].startLcn * clusterSize;
            }
        }

        auto sig = SignatureDb::Instance().Identify(file.extension, file.resident ? file.residentData.data() : nullptr,
                                                    file.resident ? file.residentData.size() : 0);
        file.category = sig.category;
        file.typeName = sig.typeName;
        file.confidence = ScoreNtfs(file, inUse);
        if (file.size == 0 && file.residentData.empty()) {
            file.confidence = Confidence::Corrupted;
            file.notes = L"No recoverable $DATA attribute";
        }

        onFile(file);
        processed = i;
        if ((i & 0x1FF) == 0) {
            report(L"Scanning deleted NTFS records");
        }
    }

    processed = recordCount;
    report(L"NTFS scan complete");
    return true;
}

} // namespace pdr
