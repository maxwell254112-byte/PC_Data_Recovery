#include "filesystem/RecycleBinScanner.h"
#include "carving/SignatureDb.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <windows.h>
#include <vector>
#include <algorithm>

namespace pdr {
namespace {

bool ParseInfoFile(const std::wstring& path, uint64_t& size, FILETIME& deleted, std::wstring& original) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 24) {
        CloseHandle(h);
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(li.QuadPart));
    DWORD read = 0;
    if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr)) {
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    if (read < 24) {
        return false;
    }

    uint64_t version = *reinterpret_cast<uint64_t*>(buf.data());
    if (version == 2 || (buf[0] == 2 && buf[1] == 0)) {
        if (read < 28) return false;
        size = *reinterpret_cast<uint64_t*>(buf.data() + 8);
        deleted = *reinterpret_cast<FILETIME*>(buf.data() + 16);
        original.assign(reinterpret_cast<wchar_t*>(buf.data() + 24),
                        (read - 24) / 2);
        if (!original.empty() && original.back() == 0) original.pop_back();
        return !original.empty();
    }

    // Version 1
    size = *reinterpret_cast<uint64_t*>(buf.data() + 8);
    deleted = *reinterpret_cast<FILETIME*>(buf.data() + 16);
    if (read >= 24 + 520) {
        original.assign(reinterpret_cast<wchar_t*>(buf.data() + 24), 260);
        auto z = original.find(L'\0');
        if (z != std::wstring::npos) original.resize(z);
        return !original.empty();
    }
    return false;
}

void ScanSidFolder(const DriveInfo& drive, const std::wstring& folder,
                   IScanControl& control, const FileFoundCallback& onFile, uint64_t& id) {
    std::wstring pattern = folder + L"\\$I*";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (control.ShouldStop()) break;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        std::wstring infoPath = folder + L"\\" + fd.cFileName;
        std::wstring dataName = fd.cFileName;
        if (dataName.size() >= 2) {
            dataName[1] = L'R';
        }
        std::wstring dataPath = folder + L"\\" + dataName;

        uint64_t size = 0;
        FILETIME deleted{};
        std::wstring original;
        if (!ParseInfoFile(infoPath, size, deleted, original)) {
            continue;
        }
        WIN32_FILE_ATTRIBUTE_DATA attr{};
        if (!GetFileAttributesExW(dataPath.c_str(), GetFileExInfoStandard, &attr)) {
            continue;
        }
        ULARGE_INTEGER actual{};
        actual.LowPart = attr.nFileSizeLow;
        actual.HighPart = attr.nFileSizeHigh;
        if (size == 0) {
            size = actual.QuadPart;
        }

        RecoveredFile file;
        file.id = id++;
        file.name = GetFileName(original);
        file.extension = GetExtension(file.name);
        file.originalPath = original;
        file.size = size;
        file.dataLength = actual.QuadPart;
        file.sourceVolume = drive.volumePath;
        file.sourceLetter = drive.letter;
        file.method = DiscoveryMethod::RecycleBin;
        file.modified = deleted;
        file.created = attr.ftCreationTime;
        file.hasTimestamps = true;
        file.notes = L"Located in Recycle Bin: " + dataPath;
        file.localSourcePath = dataPath;
        file.volumeOffset = 0;
        file.resident = true;

        HANDLE h = CreateFileW(dataPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            uint64_t take = (std::min<uint64_t>)(actual.QuadPart, 4ull * 1024ull * 1024ull);
            file.residentData.resize(static_cast<size_t>(take));
            DWORD got = 0;
            ReadFile(h, file.residentData.data(), static_cast<DWORD>(take), &got, nullptr);
            file.residentData.resize(got);
            CloseHandle(h);
            if (actual.QuadPart > take) {
                file.resident = false;
                file.notes += L" (content larger than preview cache; recovered from Recycle Bin path)";
            }
        }

        auto ident = SignatureDb::Instance().Identify(file.extension,
            file.residentData.empty() ? nullptr : file.residentData.data(),
            file.residentData.size());
        file.category = ident.category;
        file.typeName = ident.typeName;
        file.confidence = Confidence::Excellent;
        onFile(file);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

} // namespace

void RecycleBinScanner::Scan(const DriveInfo& drive, IScanControl& control,
                             const FileFoundCallback& onFile) {
    if (drive.letter.empty()) {
        return;
    }
    std::wstring root = drive.letter + L"\\$Recycle.Bin";
    DWORD attr = GetFileAttributesW(root.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        root = drive.letter + L"\\RECYCLER";
        attr = GetFileAttributesW(root.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            return;
        }
    }

    uint64_t id = 4000000;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (control.ShouldStop()) break;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        ScanSidFolder(drive, root + L"\\" + fd.cFileName, control, onFile, id);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    Logger::Instance().Info(L"Recycle Bin scan finished on " + drive.letter);
}

} // namespace pdr
