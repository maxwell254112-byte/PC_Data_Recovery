#include "scanner/ScanEngine.h"
#include "drive/VolumeReader.h"
#include "filesystem/NtfsScanner.h"
#include "filesystem/Fat32Scanner.h"
#include "filesystem/ExFatScanner.h"
#include "filesystem/RecycleBinScanner.h"
#include "carving/RawCarver.h"
#include "carving/SignatureDb.h"
#include "utils/Logger.h"

namespace pdr {

ScanEngine::ScanEngine() = default;

ScanEngine::~ScanEngine() {
    Cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool ScanEngine::IsBusy() const {
    auto s = state_.load();
    return s == ScanState::Running || s == ScanState::Paused || s == ScanState::Cancelling;
}

bool ScanEngine::ShouldStop() const {
    return cancel_.load();
}

void ScanEngine::WaitIfPaused() const {
    std::unique_lock<std::mutex> lock(pauseMutex_);
    pauseCv_.wait(lock, [this] {
        return !pause_.load() || cancel_.load();
    });
}

void ScanEngine::Pause() {
    if (state_.load() == ScanState::Running) {
        pause_.store(true);
        state_.store(ScanState::Paused);
    }
}

void ScanEngine::Resume() {
    if (state_.load() == ScanState::Paused) {
        pause_.store(false);
        state_.store(ScanState::Running);
        pauseCv_.notify_all();
    }
}

void ScanEngine::Cancel() {
    cancel_.store(true);
    pause_.store(false);
    if (IsBusy()) {
        state_.store(ScanState::Cancelling);
    }
    pauseCv_.notify_all();
}

void ScanEngine::Start(const DriveInfo& drive, ScanMode mode, const AppConfig& config,
                       HWND uiWindow, FileFoundCallback onFile, ProgressCallback onProgress) {
    if (IsBusy()) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    drive_ = drive;
    mode_ = mode;
    config_ = config;
    uiWindow_ = uiWindow;
    onFile_ = std::move(onFile);
    onProgress_ = std::move(onProgress);
    cancel_.store(false);
    pause_.store(false);
    foundCount_ = 0;
    state_.store(ScanState::Running);
    SignatureDb::Instance().SetEnabledExtensions(config.enabledExtensions);
    worker_ = std::thread(&ScanEngine::Worker, this);
}

void ScanEngine::PublishProgress(ScanProgress progress) {
    progress.filesFound = foundCount_;
    if (onProgress_) {
        onProgress_(progress);
    }
}

bool ScanEngine::RunFileSystemScan() {
    VolumeReader reader;
    bool opened = reader.Open(drive_.volumePath);
    if (!opened) {
        if (config_.scanRecycleBin) {
            Logger::Instance().Warn(L"Raw volume unavailable, Recycle Bin scan only: " + reader.LastError());
            RecycleBinScanner::Scan(drive_, *this, [this](const RecoveredFile& f) {
                ++foundCount_;
                if (onFile_) onFile_(f);
            });
            return true;
        }
        ScanProgress p;
        p.state = ScanState::Failed;
        p.error = L"Cannot open volume read-only. Restart as Administrator for deleted-file and RAW scans. " + reader.LastError();
        PublishProgress(p);
        return false;
    }

    if (config_.scanRecycleBin) {
        ScanProgress p;
        p.state = ScanState::Running;
        p.mode = mode_;
        p.stage = L"Scanning Recycle Bin";
        PublishProgress(p);
        RecycleBinScanner::Scan(drive_, *this, [this](const RecoveredFile& f) {
            ++foundCount_;
            if (onFile_) onFile_(f);
        });
    }

    if (ShouldStop()) {
        return true;
    }

    std::unique_ptr<IFileSystemScanner> scanner;
    NtfsScanner ntfs;
    Fat32Scanner fat32;
    ExFatScanner exfat;
    DriveInfo detected = drive_;

    IFileSystemScanner* active = nullptr;
    if (ntfs.Detect(reader, detected)) {
        active = &ntfs;
    } else if (exfat.Detect(reader, detected)) {
        active = &exfat;
    } else if (fat32.Detect(reader, detected)) {
        active = &fat32;
    }

    if (!active) {
        Logger::Instance().Warn(L"No supported filesystem parser for " + drive_.letter +
                                L" (" + drive_.fileSystemName + L")");
        if (mode_ == ScanMode::Quick) {
            ScanProgress p;
            p.state = ScanState::Running;
            p.stage = L"Filesystem not supported for metadata scan (NTFS/FAT32/exFAT only)";
            PublishProgress(p);
        }
        return true;
    }

    drive_.clusterSize = detected.clusterSize ? detected.clusterSize : drive_.clusterSize;
    drive_.sectorSize = detected.sectorSize ? detected.sectorSize : drive_.sectorSize;

    return active->ScanDeleted(reader, drive_, mode_, *this,
        [this](const RecoveredFile& f) {
            ++foundCount_;
            if (onFile_) onFile_(f);
        },
        [this](const ScanProgress& p) { PublishProgress(p); });
}

bool ScanEngine::RunRawScan() {
    VolumeReader reader;
    if (!reader.Open(drive_.volumePath)) {
        ScanProgress p;
        p.state = ScanState::Failed;
        p.error = L"RAW Recovery requires read-only volume access. Restart as Administrator. " + reader.LastError();
        PublishProgress(p);
        return false;
    }
    if (drive_.clusterSize == 0) {
        drive_.clusterSize = 4096;
    }
    RawCarver carver;
    return carver.Carve(reader, drive_, config_.maxFileSize, *this,
        [this](const RecoveredFile& f) {
            ++foundCount_;
            if (onFile_) onFile_(f);
        },
        [this](const ScanProgress& p) { PublishProgress(p); });
}

void ScanEngine::Worker() {
    Logger::Instance().Info(std::wstring(L"Scan started: ") + ToString(mode_) + L" on " + drive_.letter);
    bool ok = true;
    try {
        if (mode_ == ScanMode::Raw) {
            ok = RunRawScan();
        } else {
            ok = RunFileSystemScan();
            if (ok && mode_ == ScanMode::Deep && !ShouldStop()) {
                ScanProgress p;
                p.state = ScanState::Running;
                p.mode = mode_;
                p.stage = L"Deep scan: carving unallocated/raw signatures";
                PublishProgress(p);
                ok = RunRawScan();
            }
        }
    } catch (...) {
        ok = false;
        ScanProgress p;
        p.state = ScanState::Failed;
        p.error = L"Unexpected error during scan";
        PublishProgress(p);
    }

    ScanProgress done;
    if (cancel_.load()) {
        done.state = ScanState::Completed;
        done.stage = L"Scan cancelled";
    } else if (!ok) {
        done.state = ScanState::Failed;
        done.stage = L"Scan failed";
    } else {
        done.state = ScanState::Completed;
        done.stage = L"Scan complete";
        done.percent = 100.0;
    }
    done.mode = mode_;
    done.filesFound = foundCount_;
    PublishProgress(done);
    state_.store(done.state == ScanState::Failed ? ScanState::Failed : ScanState::Completed);
    Logger::Instance().Info(L"Scan finished: found=" + std::to_wstring(foundCount_));
}

} // namespace pdr
