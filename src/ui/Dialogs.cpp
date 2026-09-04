#include "ui/Dialogs.h"
#include "ui/Theme.h"
#include "settings/AppSettings.h"
#include "database/HistoryStore.h"
#include "utils/FormatUtils.h"
#include "utils/StringUtils.h"

#include <commctrl.h>
#include <shlobj.h>
#include <sstream>

namespace pdr {
namespace {

constexpr int IDC_SET_FOLDER = 4001;
constexpr int IDC_SET_BROWSE = 4002;
constexpr int IDC_SET_IMG = 4003;
constexpr int IDC_SET_PDF = 4004;
constexpr int IDC_SET_TXT = 4005;
constexpr int IDC_SET_RECYCLE = 4006;
constexpr int IDC_SET_WARN = 4007;
constexpr int IDC_SET_MAXSIZE = 4008;
constexpr int IDC_SET_EXTS = 4009;
constexpr int IDC_HIST_LIST = 4101;

std::wstring g_folderStart;

int CALLBACK BrowseCallback(HWND hwnd, UINT msg, LPARAM, LPARAM) {
    if (msg == BFFM_INITIALIZED && !g_folderStart.empty()) {
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, reinterpret_cast<LPARAM>(g_folderStart.c_str()));
    }
    return 0;
}

HWND Child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id,
           int x, int y, int w, int h, HFONT font) {
    HWND hwnd = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                x, y, w, h, parent,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hwnd;
}

bool RunModal(HWND hwnd, HWND parent) {
    EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    MSG msg{};
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hwnd)) {
            break;
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    return true;
}

struct SettingsState {
    AppConfig* cfg = nullptr;
    bool done = false;
};

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        state = reinterpret_cast<SettingsState*>(cs->lpCreateParams);
        HFONT font = theme::CreateUiFont(9, false);
        SetWindowLongPtrW(hwnd, 0, reinterpret_cast<LONG_PTR>(font));

        Child(hwnd, L"STATIC", L"Default recovery folder", SS_LEFT, -1, 16, 16, 400, 18, font);
        Child(hwnd, L"EDIT", state->cfg->defaultRecoveryFolder.c_str(),
              ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, IDC_SET_FOLDER, 16, 38, 430, 24, font);
        Child(hwnd, L"BUTTON", L"Browse", BS_PUSHBUTTON | WS_TABSTOP, IDC_SET_BROWSE, 454, 36, 80, 28, font);
        Child(hwnd, L"BUTTON", L"Preview images", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SET_IMG, 16, 80, 180, 22, font);
        Child(hwnd, L"BUTTON", L"Preview PDF", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SET_PDF, 220, 80, 160, 22, font);
        Child(hwnd, L"BUTTON", L"Preview text", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SET_TXT, 16, 106, 180, 22, font);
        Child(hwnd, L"BUTTON", L"Scan Recycle Bin", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SET_RECYCLE, 220, 106, 180, 22, font);
        Child(hwnd, L"BUTTON", L"Warn before scanning", BS_AUTOCHECKBOX | WS_TABSTOP, IDC_SET_WARN, 16, 132, 240, 22, font);
        Child(hwnd, L"STATIC", L"Maximum file size (MB)", SS_LEFT, -1, 16, 168, 240, 18, font);
        Child(hwnd, L"EDIT", std::to_wstring(state->cfg->maxFileSize / (1024 * 1024)).c_str(),
              ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | ES_NUMBER, IDC_SET_MAXSIZE, 16, 190, 120, 24, font);
        Child(hwnd, L"STATIC", L"Enabled file types (comma-separated). Empty = all signatures.",
              SS_LEFT, -1, 16, 228, 500, 18, font);
        std::wstring exts;
        for (size_t i = 0; i < state->cfg->enabledExtensions.size(); ++i) {
            if (i) exts += L", ";
            exts += state->cfg->enabledExtensions[i];
        }
        Child(hwnd, L"EDIT", exts.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, IDC_SET_EXTS, 16, 250, 518, 24, font);
        Child(hwnd, L"STATIC", L"Theme: Dark (built-in native UI)", SS_LEFT, -1, 16, 286, 400, 18, font);
        Child(hwnd, L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 370, 330, 80, 30, font);
        Child(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 454, 330, 80, 30, font);

        CheckDlgButton(hwnd, IDC_SET_IMG, state->cfg->previewImages ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SET_PDF, state->cfg->previewPdf ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SET_TXT, state->cfg->previewText ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SET_RECYCLE, state->cfg->scanRecycleBin ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SET_WARN, state->cfg->warnBeforeScan ? BST_CHECKED : BST_UNCHECKED);
        return 0;
    }
    if (msg == WM_COMMAND && state) {
        int id = LOWORD(wParam);
        if (id == IDC_SET_BROWSE) {
            wchar_t current[MAX_PATH]{};
            GetDlgItemTextW(hwnd, IDC_SET_FOLDER, current, MAX_PATH);
            auto folder = BrowseForFolder(hwnd, L"Default recovery folder", current);
            if (!folder.empty()) SetDlgItemTextW(hwnd, IDC_SET_FOLDER, folder.c_str());
            return 0;
        }
        if (id == IDOK) {
            wchar_t buf[2048]{};
            GetDlgItemTextW(hwnd, IDC_SET_FOLDER, buf, 2048);
            state->cfg->defaultRecoveryFolder = buf;
            state->cfg->previewImages = IsDlgButtonChecked(hwnd, IDC_SET_IMG) == BST_CHECKED;
            state->cfg->previewPdf = IsDlgButtonChecked(hwnd, IDC_SET_PDF) == BST_CHECKED;
            state->cfg->previewText = IsDlgButtonChecked(hwnd, IDC_SET_TXT) == BST_CHECKED;
            state->cfg->scanRecycleBin = IsDlgButtonChecked(hwnd, IDC_SET_RECYCLE) == BST_CHECKED;
            state->cfg->warnBeforeScan = IsDlgButtonChecked(hwnd, IDC_SET_WARN) == BST_CHECKED;
            GetDlgItemTextW(hwnd, IDC_SET_MAXSIZE, buf, 64);
            try {
                uint64_t mb = std::stoull(buf);
                if (mb < 1) mb = 1;
                state->cfg->maxFileSize = mb * 1024ull * 1024ull;
            } catch (...) {}
            GetDlgItemTextW(hwnd, IDC_SET_EXTS, buf, 2048);
            state->cfg->enabledExtensions.clear();
            std::wstringstream ss(buf);
            std::wstring item;
            while (std::getline(ss, item, L',')) {
                item = Trim(item);
                if (!item.empty()) state->cfg->enabledExtensions.push_back(ToLower(item));
            }
            AppSettings::Save(*state->cfg);
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (msg == WM_CTLCOLORSTATIC) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, theme::Text);
        SetBkColor(hdc, theme::Panel);
        return reinterpret_cast<LRESULT>(theme::Brush(theme::Panel));
    }
    if (msg == WM_ERASEBKGND) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, theme::Brush(theme::Panel));
        return 1;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        HFONT font = reinterpret_cast<HFONT>(GetWindowLongPtrW(hwnd, 0));
        if (font) DeleteObject(font);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK HistoryWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        HFONT font = theme::CreateUiFont(9, false);
        SetWindowLongPtrW(hwnd, 0, reinterpret_cast<LONG_PTR>(font));
        HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            12, 12, 760, 360, hwnd, reinterpret_cast<HMENU>(IDC_HIST_LIST),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(lv, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        const wchar_t* titles[] = {L"Date/Time", L"Source", L"Destination", L"Mode", L"Recovered", L"Failed", L"Duration"};
        int widths[] = {140, 70, 220, 100, 80, 70, 80};
        for (int i = 0; i < 7; ++i) {
            col.pszText = const_cast<wchar_t*>(titles[i]);
            col.cx = widths[i];
            ListView_InsertColumn(lv, i, &col);
        }
        auto rows = HistoryStore::All();
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const auto& r = rows[static_cast<size_t>(rows.size() - 1 - i)];
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = i;
            it.pszText = const_cast<wchar_t*>(r.timestamp.c_str());
            ListView_InsertItem(lv, &it);
            auto rec = std::to_wstring(r.filesRecovered);
            auto fail = std::to_wstring(r.filesFailed);
            auto dur = FormatDuration(r.durationMs);
            ListView_SetItemText(lv, i, 1, const_cast<wchar_t*>(r.sourceDrive.c_str()));
            ListView_SetItemText(lv, i, 2, const_cast<wchar_t*>(r.destination.c_str()));
            ListView_SetItemText(lv, i, 3, const_cast<wchar_t*>(r.scanMode.c_str()));
            ListView_SetItemText(lv, i, 4, const_cast<wchar_t*>(rec.c_str()));
            ListView_SetItemText(lv, i, 5, const_cast<wchar_t*>(fail.c_str()));
            ListView_SetItemText(lv, i, 6, const_cast<wchar_t*>(dur.c_str()));
        }
        Child(hwnd, L"BUTTON", L"Close", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 692, 384, 80, 28, font);
        return 0;
    }
    if (msg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_ERASEBKGND) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, theme::Brush(theme::Panel));
        return 1;
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        HFONT font = reinterpret_cast<HFONT>(GetWindowLongPtrW(hwnd, 0));
        if (font) DeleteObject(font);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureClass(const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    if (GetClassInfoExW(GetModuleHandleW(nullptr), name, &wc)) {
        return;
    }
    wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.cbWndExtra = sizeof(LONG_PTR);
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = theme::Brush(theme::Panel);
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

} // namespace

std::wstring BrowseForFolder(HWND parent, const std::wstring& title, const std::wstring& start) {
    g_folderStart = start;
    BROWSEINFOW bi{};
    bi.hwndOwner = parent;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = BrowseCallback;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

bool ShowSafetyWarning(HWND parent, bool forScan) {
    const wchar_t* text = forScan
        ? L"Safety warning\r\n\r\n"
          L"Scanning a drive that is still in use can overwrite deleted data.\r\n"
          L"This application opens storage devices in read-only mode and never writes to the source drive.\r\n\r\n"
          L"Recommendations:\r\n"
          L"• Stop writing to the source drive immediately\r\n"
          L"• Recover files to a different physical disk\r\n"
          L"• Do not recover files onto the same volume you are scanning\r\n\r\n"
          L"Continue with the scan?"
        : L"Recovery warning\r\n\r\n"
          L"Recovered files will be written only to the destination you choose.\r\n"
          L"The source drive is never modified.\r\n"
          L"Recovering onto the same source volume is blocked.\r\n\r\n"
          L"Continue?";
    int r = MessageBoxW(parent, text, L"PC Data Recovery — Safety", MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON2);
    return r == IDOK;
}

bool ShowRecoverConfirm(HWND parent, size_t count, const std::wstring& destination, const std::wstring& sourceLetter) {
    std::wstring msg = L"Recover " + std::to_wstring(count) + L" selected file(s)\r\n\r\n";
    msg += L"From: " + sourceLetter + L"\r\n";
    msg += L"To:   " + destination + L"\r\n\r\n";
    msg += L"The source volume will not be written.\r\nContinue?";
    return MessageBoxW(parent, msg.c_str(), L"Confirm Recovery", MB_ICONQUESTION | MB_OKCANCEL) == IDOK;
}

void ShowSettingsDialog(HWND parent, AppConfig& config) {
    EnsureClass(L"PDRSettingsWnd", SettingsWndProc);
    SettingsState state;
    state.cfg = &config;
    RECT pr{};
    GetWindowRect(parent, &pr);
    int x = pr.left + 80;
    int y = pr.top + 80;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PDRSettingsWnd", L"Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 570, 410, parent, nullptr,
        GetModuleHandleW(nullptr), &state);
    if (hwnd) {
        RunModal(hwnd, parent);
    }
}

void ShowHistoryDialog(HWND parent) {
    EnsureClass(L"PDRHistoryWnd", HistoryWndProc);
    RECT pr{};
    GetWindowRect(parent, &pr);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"PDRHistoryWnd", L"Recovery History",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, pr.left + 60, pr.top + 60, 800, 460,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd) {
        RunModal(hwnd, parent);
    }
}

void ShowAboutDialog(HWND parent) {
    MessageBoxW(parent,
        L"PC Data Recovery 1.0.0\r\n"
        L"Native portable Windows recovery tool\r\n\r\n"
        L"Opens volumes read-only. Never writes to the source drive.\r\n"
        L"Supports NTFS, FAT32, exFAT metadata scans and RAW signature carving.\r\n\r\n"
        L"If a file cannot be reconstructed, it is labeled Partial, Corrupted, or Unsupported.",
        L"About PC Data Recovery", MB_OK | MB_ICONINFORMATION);
}

} // namespace pdr
