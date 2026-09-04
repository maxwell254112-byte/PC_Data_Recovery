#pragma once

#include "utils/Types.h"
#include "settings/AppSettings.h"
#include "scanner/ScanEngine.h"
#include "preview/PreviewEngine.h"
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>

namespace pdr {

class MainWindow {
public:
    static bool RegisterClass(HINSTANCE instance);
    static HWND Create(HINSTANCE instance);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    explicit MainWindow(HWND hwnd);
    ~MainWindow();

    void OnCreate();
    void OnSize(int width, int height);
    void OnPaint();
    void OnCommand(int id);
    LRESULT OnNotify(LPARAM lParam);
    void OnGetDispInfo(LPARAM lParam);
    void OnDrawItem(LPARAM lParam);
    void OnTimer();
    void OnDestroy();

    void RefreshDrives();
    void UpdateDriveSelection();
    void StartScan();
    void PauseScan();
    void ResumeScan();
    void CancelScan();
    void RecoverSelected(bool all);
    void ApplyFilters();
    void UpdatePreview();
    void UpdateStatusBar();
    void SetButtonsForState();
    void RequestAdmin();
    int SelectedDriveIndex() const;
    std::vector<size_t> CheckedIndices() const;

    HWND hwnd_ = nullptr;
    HWND driveList_ = nullptr;
    HWND resultList_ = nullptr;
    HWND searchEdit_ = nullptr;
    HWND typeFilter_ = nullptr;
    HWND confFilter_ = nullptr;
    HWND sizeFilter_ = nullptr;
    HWND dateFilter_ = nullptr;
    HWND sortCombo_ = nullptr;
    HWND infoEdit_ = nullptr;
    HWND previewHost_ = nullptr;
    HWND progressBar_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND btnStart_ = nullptr;
    HWND btnPause_ = nullptr;
    HWND btnResume_ = nullptr;
    HWND btnCancel_ = nullptr;
    HWND btnRecover_ = nullptr;
    HWND btnSelectAll_ = nullptr;
    HWND btnSettings_ = nullptr;
    HWND btnHistory_ = nullptr;
    HWND btnRefresh_ = nullptr;
    HWND btnAdmin_ = nullptr;
    HWND btnAbout_ = nullptr;
    HWND modeQuick_ = nullptr;
    HWND modeDeep_ = nullptr;
    HWND modeRaw_ = nullptr;

    HFONT font_ = nullptr;
    HFONT fontBold_ = nullptr;
    HFONT fontTitle_ = nullptr;
    HBRUSH bgBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HBITMAP previewBmp_ = nullptr;

    AppConfig config_;
    std::vector<DriveInfo> drives_;
    int selectedDrive_ = -1;
    ScanMode scanMode_ = ScanMode::Quick;
    ScanEngine engine_;
    PreviewEngine preview_;
    PreviewData previewData_;

    std::mutex resultsMutex_;
    std::vector<RecoveredFile> results_;
    std::vector<size_t> visible_;
    ScanProgress lastProgress_{};
    uint64_t scanStartTick_ = 0;
    std::wstring statusText_ = L"Ready. Select a drive and scan mode.";
};

} // namespace pdr
