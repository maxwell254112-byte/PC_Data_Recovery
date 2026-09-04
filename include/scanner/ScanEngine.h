#pragma once

#include "utils/Types.h"
#include "scanner/ScanTypes.h"
#include <memory>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace pdr {

class ScanEngine : public IScanControl {
public:
    ScanEngine();
    ~ScanEngine();

    ScanEngine(const ScanEngine&) = delete;
    ScanEngine& operator=(const ScanEngine&) = delete;

    void Start(const DriveInfo& drive, ScanMode mode, const AppConfig& config,
               HWND uiWindow, FileFoundCallback onFile, ProgressCallback onProgress);
    void Pause();
    void Resume();
    void Cancel();

    bool IsBusy() const;
    ScanState State() const { return state_.load(); }

    bool ShouldStop() const override;
    void WaitIfPaused() const override;

private:
    void Worker();
    void PublishProgress(ScanProgress progress);
    bool RunFileSystemScan();
    bool RunRawScan();

    DriveInfo drive_;
    ScanMode mode_ = ScanMode::Quick;
    AppConfig config_;
    HWND uiWindow_ = nullptr;
    FileFoundCallback onFile_;
    ProgressCallback onProgress_;

    std::thread worker_;
    mutable std::mutex pauseMutex_;
    mutable std::condition_variable pauseCv_;
    std::atomic<ScanState> state_{ScanState::Idle};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> pause_{false};
    uint64_t foundCount_ = 0;
};

} // namespace pdr
