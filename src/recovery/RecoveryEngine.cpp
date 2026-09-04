#include "recovery/RecoveryEngine.h"
#include "drive/VolumeReader.h"
#include "utils/PathUtils.h"
#include "utils/StringUtils.h"
#include "utils/FormatUtils.h"
#include "utils/Logger.h"

#include <algorithm>
#include <fstream>

namespace pdr {
namespace {

bool WriteAll(HANDLE h, const void* data, uint32_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t left = size;
    while (left) {
        DWORD written = 0;
        if (!WriteFile(h, p, left, &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        left -= written;
    }
    return true;
}

bool CopyLocalFile(const std::wstring& src, const std::wstring& dest, uint64_t maxSize, std::wstring& error) {
    if (!CopyFileW(src.c_str(), dest.c_str(), TRUE)) {
        // allow overwrite unique path already chosen
        if (!CopyFileW(src.c_str(), dest.c_str(), FALSE)) {
            error = LastErrorMessage();
            return false;
        }
    }
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (GetFileAttributesExW(dest.c_str(), GetFileExInfoStandard, &attr)) {
        ULARGE_INTEGER sz;
        sz.LowPart = attr.nFileSizeLow;
        sz.HighPart = attr.nFileSizeHigh;
        if (sz.QuadPart > maxSize) {
            DeleteFileW(dest.c_str());
            error = L"File exceeds maximum recovery size";
            return false;
        }
    }
    return true;
}

} // namespace

bool RecoveryEngine::IsSameVolume(const std::wstring& destination, const std::wstring& sourceLetter) {
    return IsPathOnVolume(destination, sourceLetter);
}

std::wstring RecoveryEngine::WriteReport(const std::wstring& destination,
                                         const DriveInfo& source,
                                         const std::vector<RecoveryItemResult>& results,
                                         uint64_t durationMs) {
    EnsureDirectory(destination);
    std::wstring path = UniquePath(destination, L"recovery_report.txt");
    FILE* fp = nullptr;
    _wfopen_s(&fp, path.c_str(), L"wb");
    if (!fp) {
        return {};
    }
    auto writeLine = [&](const std::wstring& line) {
        std::string u = WideToUtf8(line + L"\r\n");
        fwrite(u.data(), 1, u.size(), fp);
    };
    writeLine(L"PC Data Recovery Report");
    writeLine(L"=======================");
    writeLine(L"Time: " + FormatNow());
    writeLine(L"Source: " + DriveSummary(source));
    writeLine(L"Destination: " + destination);
    writeLine(L"Duration: " + FormatDuration(durationMs));
    uint64_t ok = 0, fail = 0;
    for (const auto& r : results) {
        if (r.success) ++ok; else ++fail;
    }
    writeLine(L"Succeeded: " + std::to_wstring(ok));
    writeLine(L"Failed: " + std::to_wstring(fail));
    writeLine(L"");
    writeLine(L"Name\tResult\tPath / Error");
    for (const auto& r : results) {
        writeLine(r.name + L"\t" + (r.success ? L"OK" : L"FAILED") + L"\t" +
                  (r.success ? r.destPath : r.error));
    }
    fclose(fp);
    return path;
}

std::vector<RecoveryItemResult> RecoveryEngine::Recover(const DriveInfo& source,
                                                        const std::vector<RecoveredFile>& files,
                                                        const std::wstring& destination,
                                                        uint64_t maxFileSize,
                                                        const ProgressFn& onProgress) {
    std::vector<RecoveryItemResult> results;
    EnsureDirectory(destination);

    VolumeReader reader;
    bool volumeOpen = false;

    uint64_t index = 0;
    for (const auto& file : files) {
        RecoveryItemResult item;
        item.fileId = file.id;
        item.name = file.name;

        RecoveryProgress pg;
        pg.current = index + 1;
        pg.total = files.size();
        pg.succeeded = 0;
        pg.failed = 0;
        for (const auto& r : results) {
            if (r.success) ++pg.succeeded; else ++pg.failed;
        }
        pg.currentName = file.name;
        if (onProgress) onProgress(pg);

        if (file.size > maxFileSize && file.dataLength > maxFileSize) {
            item.success = false;
            item.error = L"Exceeds maximum file size setting";
            results.push_back(item);
            ++index;
            continue;
        }

        std::wstring destPath = UniquePath(destination, file.name.empty() ? L"recovered.bin" : file.name);
        item.destPath = destPath;
        std::wstring error;

        bool ok = false;
        if (!file.localSourcePath.empty()) {
            ok = CopyLocalFile(file.localSourcePath, destPath, maxFileSize, error);
        } else if (file.resident && !file.residentData.empty() &&
                   (file.dataLength == 0 || file.residentData.size() >= file.dataLength ||
                    file.residentData.size() == file.size)) {
            HANDLE out = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) {
                error = LastErrorMessage();
            } else {
                uint32_t n = static_cast<uint32_t>(file.residentData.size());
                ok = WriteAll(out, file.residentData.data(), n);
                CloseHandle(out);
                if (!ok) error = L"Failed writing resident data";
            }
        } else {
            if (!volumeOpen) {
                volumeOpen = reader.Open(source.volumePath);
                if (!volumeOpen) {
                    error = L"Cannot open source volume read-only: " + reader.LastError();
                }
            }
            if (volumeOpen) {
                HANDLE out = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
                if (out == INVALID_HANDLE_VALUE) {
                    error = LastErrorMessage();
                } else {
                    const uint64_t total = file.dataLength ? file.dataLength : file.size;
                    const uint32_t cluster = source.clusterSize ? source.clusterSize : 4096;
                    std::vector<uint8_t> buf(1024 * 1024);
                    uint64_t remaining = total;
                    ok = true;

                    if (file.method == DiscoveryMethod::RawCarve || file.runs.empty()) {
                        uint64_t off = file.volumeOffset;
                        while (remaining > 0) {
                            uint32_t chunk = static_cast<uint32_t>((std::min<uint64_t>)(remaining, buf.size()));
                            if (!reader.Read(off, buf.data(), chunk) || !WriteAll(out, buf.data(), chunk)) {
                                ok = false;
                                error = L"Read/write failed at offset " + std::to_wstring(off);
                                break;
                            }
                            off += chunk;
                            remaining -= chunk;
                        }
                    } else {
                        for (const auto& run : file.runs) {
                            if (remaining == 0) break;
                            uint64_t runBytes = run.clusterCount * cluster;
                            uint64_t off = run.startLcn * cluster;
                            uint64_t take = (std::min)(runBytes, remaining);
                            uint64_t done = 0;
                            while (done < take) {
                                uint32_t chunk = static_cast<uint32_t>((std::min<uint64_t>)(take - done, buf.size()));
                                if (!reader.Read(off + done, buf.data(), chunk) || !WriteAll(out, buf.data(), chunk)) {
                                    ok = false;
                                    error = L"Cluster run read failed";
                                    break;
                                }
                                done += chunk;
                            }
                            if (!ok) break;
                            remaining -= take;
                        }
                    }
                    CloseHandle(out);
                    if (!ok) {
                        DeleteFileW(destPath.c_str());
                    }
                }
            }
        }

        item.success = ok;
        item.error = error;
        if (!ok && item.error.empty()) {
            item.error = L"Recovery failed";
        }
        results.push_back(item);
        ++index;
    }

    RecoveryProgress pg;
    pg.current = files.size();
    pg.total = files.size();
    for (const auto& r : results) {
        if (r.success) ++pg.succeeded; else ++pg.failed;
    }
    if (onProgress) onProgress(pg);
    Logger::Instance().Info(L"Recovery finished ok=" + std::to_wstring(pg.succeeded) +
                            L" fail=" + std::to_wstring(pg.failed));
    return results;
}

} // namespace pdr
