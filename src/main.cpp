#include "ui/MainWindow.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"
#include "settings/AppSettings.h"
#include "database/HistoryStore.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <gdiplus.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES |
                ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_USEREX_CLASSES;
    InitCommonControlsEx(&icc);

    pdr::EnsureDirectory(pdr::GetConfigDirectory());
    pdr::EnsureDirectory(pdr::GetDataDirectory());
    pdr::EnsureDirectory(pdr::GetLogDirectory());
    pdr::Logger::Instance().Initialize(pdr::GetLogDirectory());
    pdr::Logger::Instance().Info(L"PC Data Recovery starting");
    pdr::AppSettings::Load();
    pdr::HistoryStore::Initialize();

    if (!pdr::MainWindow::RegisterClass(instance)) {
        MessageBoxW(nullptr, L"Failed to register window class.", L"PC Data Recovery", MB_ICONERROR);
        return 1;
    }

    HWND hwnd = pdr::MainWindow::Create(instance);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create main window.", L"PC Data Recovery", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (gdiplusToken) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return static_cast<int>(msg.wParam);
}
