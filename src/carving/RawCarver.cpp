#include "carving/RawCarver.h"
#include "carving/SignatureDb.h"
#include "drive/VolumeReader.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <cstring>

namespace pdr {

bool RawCarver::Carve(VolumeReader& reader, const DriveInfo& drive, uint64_t maxFileSize,
                      IScanControl& control, const FileFoundCallback& onFile,
                      const ProgressCallback& onProgress) {
    const uint64_t volumeSize = reader.Size();
    if (volumeSize == 0) {
        return false;
    }

    auto signatures = SignatureDb::Instance().Enabled();
    if (signatures.empty()) {
        return true;
    }

    const uint32_t chunkSize = 1024 * 1024;
    const uint32_t overlap = 64 * 1024;
    std::vector<uint8_t> buffer(chunkSize + overlap);
    uint64_t offset = 0;
    uint64_t found = 0;
    uint64_t started = GetTickCount64();
    uint64_t lastReport = started;

    auto report = [&](const wchar_t* stage, double extraPercent = 0) {
        ScanProgress p;
        p.state = ScanState::Running;
        p.mode = ScanMode::Raw;
        p.bytesScanned = offset;
        p.bytesTotal = volumeSize;
        p.percent = volumeSize ? (100.0 * static_cast<double>(offset) / static_cast<double>(volumeSize)) : extraPercent;
        p.filesFound = found;
        p.elapsedMs = GetTickCount64() - started;
        if (p.elapsedMs > 0) {
            p.bytesPerSecond = (static_cast<double>(offset) * 1000.0) / static_cast<double>(p.elapsedMs);
            if (p.bytesPerSecond > 0 && offset < volumeSize) {
                p.etaMs = static_cast<uint64_t>(((volumeSize - offset) * 1000.0) / p.bytesPerSecond);
            }
        }
        p.stage = stage;
        onProgress(p);
    };

    while (offset < volumeSize) {
        if (control.ShouldStop()) {
            return true;
        }
        control.WaitIfPaused();

        uint32_t toRead = chunkSize;
        if (offset + toRead > volumeSize) {
            toRead = static_cast<uint32_t>(volumeSize - offset);
        }
        if (!reader.Read(offset, buffer.data(), toRead)) {
            offset += toRead;
            continue;
        }

        for (const auto& sig : signatures) {
            if (sig.header.empty()) {
                continue;
            }
            const size_t need = sig.headerOffset + sig.header.size();
            if (toRead < need) {
                continue;
            }
            for (uint32_t i = 0; i + need <= toRead; ++i) {
                if (memcmp(buffer.data() + i + sig.headerOffset, sig.header.data(), sig.header.size()) != 0) {
                    continue;
                }
                // Additional MP4/MOV / AVI / WAV checks
                if ((sig.extension == L"mp4" || sig.extension == L"mov") && i + 12 <= toRead) {
                    if (memcmp(buffer.data() + i + 4, "ftyp", 4) != 0) {
                        continue;
                    }
                    if (sig.extension == L"mov") {
                        bool qt = memcmp(buffer.data() + i + 8, "qt  ", 4) == 0;
                        if (!qt) continue;
                    }
                }
                if (sig.extension == L"avi" && i + 12 <= toRead) {
                    if (memcmp(buffer.data() + i + 8, "AVI ", 4) != 0) continue;
                }
                if (sig.extension == L"wav" && i + 12 <= toRead) {
                    if (memcmp(buffer.data() + i + 8, "WAVE", 4) != 0) continue;
                }

                const uint64_t fileOffset = offset + i;
                const uint64_t remain = volumeSize - fileOffset;
                const uint32_t peek = (std::min<uint32_t>)(toRead - i, 512 * 1024);
                uint64_t inferred = SignatureDb::InferSize(sig, buffer.data() + i, peek, remain);
                if (inferred == 0) {
                    inferred = (std::min)(sig.maxSize, maxFileSize);
                }
                inferred = (std::min)(inferred, maxFileSize);
                inferred = (std::min)(inferred, remain);

                RecoveredFile file;
                file.id = 5000000 + found;
                file.name = L"carved_" + std::to_wstring(fileOffset) + L"." + sig.extension;
                file.extension = sig.extension;
                file.originalPath.clear();
                file.size = inferred;
                file.dataLength = inferred;
                file.category = sig.category;
                file.typeName = sig.typeName;
                file.sourceVolume = drive.volumePath;
                file.sourceLetter = drive.letter;
                file.method = DiscoveryMethod::RawCarve;
                file.volumeOffset = fileOffset;
                file.confidence = sig.footer.empty() ? Confidence::Partial : Confidence::Good;
                file.notes = L"Found by file signature carving; original name/path unavailable";
                if (drive.clusterSize) {
                    file.runs.push_back({fileOffset / drive.clusterSize,
                                         (inferred + drive.clusterSize - 1) / drive.clusterSize});
                } else {
                    file.runs.push_back({fileOffset / 512, (inferred + 511) / 512});
                }
                onFile(file);
                ++found;
                i += 15; // skip ahead slightly after a hit
            }
        }

        if (toRead < chunkSize) {
            offset += toRead;
        } else {
            offset += (chunkSize > overlap) ? (chunkSize - overlap) : chunkSize;
        }

        uint64_t now = GetTickCount64();
        if (now - lastReport > 250) {
            report(L"RAW signature scan");
            lastReport = now;
        }
    }

    report(L"RAW scan complete");
    Logger::Instance().Info(L"RAW carve finished, files=" + std::to_wstring(found));
    return true;
}

} // namespace pdr
