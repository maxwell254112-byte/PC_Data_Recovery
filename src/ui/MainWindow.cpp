#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/Dialogs.h"
#include "drive/DriveEnumerator.h"
#include "recovery/RecoveryEngine.h"
#include "database/HistoryStore.h"
#include "utils/FormatUtils.h"
#include "utils/StringUtils.h"
#include "utils/PathUtils.h"
#include "utils/Logger.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <thread>

namespace pdr {
namespace {

constexpr int IDC_DRIVE_LIST    = 1001;
constexpr int IDC_MODE_QUICK    = 1002;
constexpr int IDC_MODE_DEEP     = 1003;
constexpr int IDC_MODE_RAW      = 1004;
constexpr int IDC_BTN_START     = 1005;
constexpr int IDC_BTN_PAUSE     = 1006;
constexpr int IDC_BTN_RESUME    = 1007;
constexpr int IDC_BTN_CANCEL    = 1008;
constexpr int IDC_BTN_REFRESH   = 1009;
constexpr int IDC_SEARCH        = 1010;
constexpr int IDC_FILTER_TYPE   = 1011;
constexpr int IDC_FILTER_CONF   = 1012;
constexpr int IDC_FILTER_SIZE   = 1013;
constexpr int IDC_FILTER_DATE   = 1014;
constexpr int IDC_SORT          = 1015;
constexpr int IDC_RESULTS       = 1016;
constexpr int IDC_INFO          = 1017;
constexpr int IDC_PROGRESS      = 1018;
constexpr int IDC_STATUS        = 1019;
constexpr int IDC_BTN_SETTINGS  = 1020;
constexpr int IDC_BTN_HISTORY   = 1021;
constexpr int IDC_BTN_RECOVER   = 1022;
constexpr int IDC_BTN_SELECTALL = 1023;
constexpr int IDC_BTN_ADMIN     = 1024;
constexpr int IDC_PREVIEW       = 1025;
constexpr int IDC_BTN_ABOUT     = 1026;

constexpr UINT WM_SCAN_PROGRESS = WM_APP + 1;
constexpr UINT WM_SCAN_FILE     = WM_APP + 2;
constexpr UINT WM_RECOVER_PROG  = WM_APP + 4;
constexpr UINT WM_RECOVER_DONE  = WM_APP + 5;

constexpr wchar_t kClassName[] = L"PCDataRecoveryMain";

HWND CreateChild(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void AddCombo(HWND combo, const wchar_t* text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

COLORREF ConfidenceColor(Confidence c) {
    switch (c) {
        case Confidence::Excellent: return theme::Excellent;
        case Confidence::Good:      return theme::Good;
        case Confidence::Partial:   return theme::Partial;
        default:                    return theme::Corrupted;
    }
}

bool IsAdmin() {
    return IsUserAnAdmin() != FALSE;
}

} // namespace

bool MainWindow::RegisterClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = theme::Brush(theme::Bg);
    wc.lpszClassName = kClassName;
    if (!wc.hIcon) {
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
    }
    return RegisterClassExW(&wc) != 0;
}

HWND MainWindow::Create(HINSTANCE instance) {
    HWND hwnd = CreateWindowExW(
        0, kClassName, L"PC Data Recovery",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
        nullptr, nullptr, instance, nullptr);
    return hwnd;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = new MainWindow(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_CREATE:
        self->OnCreate();
        return 0;
    case WM_SIZE:
        self->OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_PAINT:
        self->OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_COMMAND:
        self->OnCommand(LOWORD(wParam));
        if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == EN_CHANGE) {
            if (LOWORD(wParam) >= IDC_SEARCH && LOWORD(wParam) <= IDC_SORT) {
                self->ApplyFilters();
                self->UpdateStatusBar();
            }
        }
        return 0;
    case WM_NOTIFY:
        return self->OnNotify(lParam);
    case WM_DRAWITEM:
        self->OnDrawItem(lParam);
        return TRUE;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, theme::Text);
        SetBkColor(hdc, theme::InputBg);
        return reinterpret_cast<LRESULT>(theme::Brush(theme::InputBg));
    }
    case WM_TIMER:
        self->OnTimer();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* m = reinterpret_cast<MINMAXINFO*>(lParam);
        m->ptMinTrackSize = {1100, 700};
        return 0;
    }
    case WM_SCAN_PROGRESS: {
        auto* p = reinterpret_cast<ScanProgress*>(lParam);
        self->lastProgress_ = *p;
        self->statusText_ = p->error.empty() ? p->stage : p->error;
        self->SetButtonsForState();
        if (p->state == ScanState::Completed || p->state == ScanState::Failed) {
            KillTimer(hwnd, 1);
            self->ApplyFilters();
            if (p->state == ScanState::Failed && !p->error.empty()) {
                MessageBoxW(hwnd, p->error.c_str(), L"Scan", MB_ICONWARNING);
            }
        }
        self->UpdateStatusBar();
        delete p;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_SCAN_FILE: {
        auto* f = reinterpret_cast<RecoveredFile*>(lParam);
        {
            std::lock_guard<std::mutex> lock(self->resultsMutex_);
            self->results_.push_back(std::move(*f));
        }
        delete f;
        if ((self->results_.size() % 16) == 0) {
            self->ApplyFilters();
            self->UpdateStatusBar();
        }
        return 0;
    }
    case WM_RECOVER_PROG: {
        auto* p = reinterpret_cast<RecoveryProgress*>(lParam);
        self->statusText_ = L"Recovering " + p->currentName + L" (" +
                            std::to_wstring(p->current) + L"/" + std::to_wstring(p->total) + L")";
        SendMessageW(self->progressBar_, PBM_SETRANGE32, 0, static_cast<LPARAM>(p->total));
        SendMessageW(self->progressBar_, PBM_SETPOS, static_cast<WPARAM>(p->current), 0);
        self->UpdateStatusBar();
        delete p;
        return 0;
    }
    case WM_RECOVER_DONE: {
        auto* msgText = reinterpret_cast<std::wstring*>(lParam);
        self->statusText_ = *msgText;
        self->UpdateStatusBar();
        MessageBoxW(hwnd, msgText->c_str(), L"Recovery", MB_OK | MB_ICONINFORMATION);
        delete msgText;
        EnableWindow(self->btnRecover_, TRUE);
        return 0;
    }
    case WM_DESTROY:
        self->OnDestroy();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete self;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

MainWindow::MainWindow(HWND hwnd) : hwnd_(hwnd) {}

MainWindow::~MainWindow() {
    previewData_.Reset();
    if (font_) DeleteObject(font_);
    if (fontBold_) DeleteObject(fontBold_);
    if (fontTitle_) DeleteObject(fontTitle_);
    if (bgBrush_) DeleteObject(bgBrush_);
    if (panelBrush_) DeleteObject(panelBrush_);
}

void MainWindow::OnCreate() {
    config_ = AppSettings::Load();
    HistoryStore::Initialize();
    font_ = theme::CreateUiFont(9, false);
    fontBold_ = theme::CreateUiFont(9, true);
    fontTitle_ = theme::CreateUiFont(16, true);
    bgBrush_ = CreateSolidBrush(theme::Bg);
    panelBrush_ = CreateSolidBrush(theme::Panel);

    driveList_ = CreateChild(hwnd_, WC_LISTBOXW, L"",
        WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, IDC_DRIVE_LIST);
    modeQuick_ = CreateChild(hwnd_, L"BUTTON", L"Quick Scan", BS_AUTORADIOBUTTON | WS_GROUP, IDC_MODE_QUICK);
    modeDeep_  = CreateChild(hwnd_, L"BUTTON", L"Deep Scan", BS_AUTORADIOBUTTON, IDC_MODE_DEEP);
    modeRaw_   = CreateChild(hwnd_, L"BUTTON", L"RAW Recovery", BS_AUTORADIOBUTTON, IDC_MODE_RAW);
    SendMessageW(modeQuick_, BM_SETCHECK, BST_CHECKED, 0);

    btnStart_  = CreateChild(hwnd_, L"BUTTON", L"Start", BS_PUSHBUTTON, IDC_BTN_START);
    btnPause_  = CreateChild(hwnd_, L"BUTTON", L"Pause", BS_PUSHBUTTON, IDC_BTN_PAUSE);
    btnResume_ = CreateChild(hwnd_, L"BUTTON", L"Resume", BS_PUSHBUTTON, IDC_BTN_RESUME);
    btnCancel_ = CreateChild(hwnd_, L"BUTTON", L"Cancel", BS_PUSHBUTTON, IDC_BTN_CANCEL);
    btnRefresh_= CreateChild(hwnd_, L"BUTTON", L"Refresh Drives", BS_PUSHBUTTON, IDC_BTN_REFRESH);
    btnAdmin_  = CreateChild(hwnd_, L"BUTTON", L"Run as Administrator", BS_PUSHBUTTON, IDC_BTN_ADMIN);

    searchEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_,
        reinterpret_cast<HMENU>(IDC_SEARCH), GetModuleHandleW(nullptr), nullptr);
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif
    SendMessageW(searchEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search filename..."));

    typeFilter_ = CreateChild(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_FILTER_TYPE);
    confFilter_ = CreateChild(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_FILTER_CONF);
    sizeFilter_ = CreateChild(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_FILTER_SIZE);
    dateFilter_ = CreateChild(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_FILTER_DATE);
    sortCombo_  = CreateChild(hwnd_, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_SORT);

    AddCombo(typeFilter_, L"All types");
    AddCombo(typeFilter_, L"Image");
    AddCombo(typeFilter_, L"Video");
    AddCombo(typeFilter_, L"Audio");
    AddCombo(typeFilter_, L"Document");
    AddCombo(typeFilter_, L"Archive");
    AddCombo(typeFilter_, L"Database");
    AddCombo(typeFilter_, L"Text");
    SendMessageW(typeFilter_, CB_SETCURSEL, 0, 0);

    AddCombo(confFilter_, L"All confidence");
    AddCombo(confFilter_, L"Excellent");
    AddCombo(confFilter_, L"Good");
    AddCombo(confFilter_, L"Partial");
    AddCombo(confFilter_, L"Corrupted");
    SendMessageW(confFilter_, CB_SETCURSEL, 0, 0);

    AddCombo(sizeFilter_, L"All sizes");
    AddCombo(sizeFilter_, L"< 1 MB");
    AddCombo(sizeFilter_, L"1 – 10 MB");
    AddCombo(sizeFilter_, L"10 – 100 MB");
    AddCombo(sizeFilter_, L"> 100 MB");
    SendMessageW(sizeFilter_, CB_SETCURSEL, 0, 0);

    AddCombo(dateFilter_, L"All dates");
    AddCombo(dateFilter_, L"Last 7 days");
    AddCombo(dateFilter_, L"Last 30 days");
    AddCombo(dateFilter_, L"Last year");
    SendMessageW(dateFilter_, CB_SETCURSEL, 0, 0);

    AddCombo(sortCombo_, L"Sort: Name");
    AddCombo(sortCombo_, L"Sort: Size");
    AddCombo(sortCombo_, L"Sort: Date");
    AddCombo(sortCombo_, L"Sort: Type");
    SendMessageW(sortCombo_, CB_SETCURSEL, 0, 0);

    resultList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(IDC_RESULTS), GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(resultList_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    SetWindowTheme(resultList_, L"", L"");
    ListView_SetBkColor(resultList_, theme::Panel);
    ListView_SetTextBkColor(resultList_, theme::Panel);
    ListView_SetTextColor(resultList_, theme::Text);

    const wchar_t* cols[] = {L"", L"Name", L"Ext", L"Original path", L"Size", L"Type", L"Date/Time", L"Status", L"Confidence", L"Found by"};
    int widths[] = {28, 180, 56, 220, 90, 110, 140, 90, 90, 90};
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (int i = 0; i < 10; ++i) {
        col.pszText = const_cast<wchar_t*>(cols[i]);
        col.cx = widths[i];
        col.iSubItem = i;
        ListView_InsertColumn(resultList_, i, &col);
    }

    btnSelectAll_ = CreateChild(hwnd_, L"BUTTON", L"Select All", BS_PUSHBUTTON, IDC_BTN_SELECTALL);
    btnRecover_   = CreateChild(hwnd_, L"BUTTON", L"Recover Selected", BS_PUSHBUTTON, IDC_BTN_RECOVER);
    btnSettings_  = CreateChild(hwnd_, L"BUTTON", L"Settings", BS_PUSHBUTTON, IDC_BTN_SETTINGS);
    btnHistory_   = CreateChild(hwnd_, L"BUTTON", L"History", BS_PUSHBUTTON, IDC_BTN_HISTORY);
    btnAbout_     = CreateChild(hwnd_, L"BUTTON", L"About", BS_PUSHBUTTON, IDC_BTN_ABOUT);

    previewHost_ = CreateChild(hwnd_, L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY, IDC_PREVIEW);
    infoEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(IDC_INFO), GetModuleHandleW(nullptr), nullptr);

    progressBar_ = CreateChild(hwnd_, PROGRESS_CLASSW, L"", PBS_SMOOTH, IDC_PROGRESS);
    SendMessageW(progressBar_, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
    statusLabel_ = CreateChild(hwnd_, L"STATIC", L"Ready", SS_LEFT | SS_ENDELLIPSIS, IDC_STATUS);

    HWND kids[] = {driveList_, modeQuick_, modeDeep_, modeRaw_, btnStart_, btnPause_, btnResume_,
                   btnCancel_, btnRefresh_, btnAdmin_, searchEdit_, typeFilter_, confFilter_,
                   sizeFilter_, dateFilter_, sortCombo_, resultList_, btnSelectAll_, btnRecover_,
                   btnSettings_, btnHistory_, btnAbout_, infoEdit_, statusLabel_};
    for (HWND h : kids) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    RefreshDrives();
    SetButtonsForState();
    if (!IsAdmin()) {
        statusText_ = L"Running without Administrator rights. Recycle Bin scan works; deleted MFT/FAT/RAW scans need elevation.";
    }
    UpdateStatusBar();
}

void MainWindow::OnSize(int width, int height) {
    const int header = 58;
    const int footer = 70;
    const int left = 250;
    const int right = 280;
    const int pad = 12;
    const int y0 = header + pad;
    const int workH = height - header - footer - pad;

    MoveWindow(btnSettings_, width - 284, 12, 80, 28, TRUE);
    MoveWindow(btnHistory_, width - 196, 12, 80, 28, TRUE);
    MoveWindow(btnAbout_, width - 108, 12, 80, 28, TRUE);

    int ly = y0 + 28;
    MoveWindow(driveList_, pad, ly, left - 8, 170, TRUE);
    ly += 178;
    MoveWindow(btnRefresh_, pad, ly, left - 8, 26, TRUE);
    ly += 34;
    MoveWindow(modeQuick_, pad, ly, left - 8, 22, TRUE); ly += 22;
    MoveWindow(modeDeep_, pad, ly, left - 8, 22, TRUE); ly += 22;
    MoveWindow(modeRaw_, pad, ly, left - 8, 22, TRUE); ly += 28;
    MoveWindow(btnStart_, pad, ly, (left - 16) / 2, 28, TRUE);
    MoveWindow(btnPause_, pad + (left - 16) / 2 + 4, ly, (left - 16) / 2, 28, TRUE);
    ly += 32;
    MoveWindow(btnResume_, pad, ly, (left - 16) / 2, 28, TRUE);
    MoveWindow(btnCancel_, pad + (left - 16) / 2 + 4, ly, (left - 16) / 2, 28, TRUE);
    ly += 36;
    MoveWindow(btnAdmin_, pad, ly, left - 8, 26, TRUE);
    EnableWindow(btnAdmin_, !IsAdmin());

    int cx = pad + left + 8;
    int cw = width - cx - right - pad;
    int fy = y0;
    int fw = (cw - 24) / 5;
    MoveWindow(searchEdit_, cx, fy, fw + 20, 24, TRUE);
    MoveWindow(typeFilter_, cx + fw + 28, fy, fw, 24, TRUE);
    MoveWindow(confFilter_, cx + 2 * fw + 36, fy, fw, 24, TRUE);
    MoveWindow(sizeFilter_, cx + 3 * fw + 44, fy, fw - 10, 24, TRUE);
    MoveWindow(dateFilter_, cx + 4 * fw + 42, fy, fw - 20, 24, TRUE);
    MoveWindow(sortCombo_, cx + cw - 130, fy, 130, 24, TRUE);

    int listY = fy + 32;
    int listH = workH - 72;
    MoveWindow(resultList_, cx, listY, cw, listH, TRUE);
    MoveWindow(btnSelectAll_, cx, listY + listH + 8, 100, 26, TRUE);
    MoveWindow(btnRecover_, cx + 108, listY + listH + 8, 140, 26, TRUE);

    int rx = width - right;
    MoveWindow(previewHost_, rx, y0, right - pad, 200, TRUE);
    MoveWindow(infoEdit_, rx, y0 + 208, right - pad, workH - 208, TRUE);

    MoveWindow(progressBar_, pad, height - 52, width - 2 * pad, 16, TRUE);
    MoveWindow(statusLabel_, pad, height - 32, width - 2 * pad, 20, TRUE);
}

void MainWindow::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    theme::FillSolid(hdc, rc, theme::Bg);

    RECT header{0, 0, rc.right, 58};
    theme::FillSolid(hdc, header, theme::Header);
    RECT title{16, 8, 640, 50};
    theme::DrawTextAlign(hdc, title, L"PC Data Recovery", theme::Text, fontTitle_);
    RECT sub{260, 18, 820, 46};
    theme::DrawTextAlign(hdc, sub, L"Recover deleted and lost files from PC storage", theme::TextDim, font_);

    RECT leftLabel{12, 68, 250, 90};
    theme::DrawTextAlign(hdc, leftLabel, L"DRIVES", theme::Accent, fontBold_);

    RECT foot{0, rc.bottom - 62, rc.right, rc.bottom};
    theme::FillSolid(hdc, foot, theme::Header);

    EndPaint(hwnd_, &ps);
}

void MainWindow::OnCommand(int id) {
    switch (id) {
    case IDC_DRIVE_LIST:
        UpdateDriveSelection();
        break;
    case IDC_MODE_QUICK: scanMode_ = ScanMode::Quick; break;
    case IDC_MODE_DEEP:  scanMode_ = ScanMode::Deep; break;
    case IDC_MODE_RAW:   scanMode_ = ScanMode::Raw; break;
    case IDC_BTN_START:  StartScan(); break;
    case IDC_BTN_PAUSE:  PauseScan(); break;
    case IDC_BTN_RESUME: ResumeScan(); break;
    case IDC_BTN_CANCEL: CancelScan(); break;
    case IDC_BTN_REFRESH: RefreshDrives(); break;
    case IDC_BTN_SETTINGS: ShowSettingsDialog(hwnd_, config_); break;
    case IDC_BTN_HISTORY: ShowHistoryDialog(hwnd_); break;
    case IDC_BTN_ABOUT: ShowAboutDialog(hwnd_); break;
    case IDC_BTN_SELECTALL: {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        for (size_t idx : visible_) {
            if (idx < results_.size()) {
                results_[idx].checked = true;
            }
        }
        ListView_RedrawItems(resultList_, 0, static_cast<int>(visible_.size()));
        break;
    }
    case IDC_BTN_RECOVER: RecoverSelected(false); break;
    case IDC_BTN_ADMIN: RequestAdmin(); break;
    }
}

LRESULT MainWindow::OnNotify(LPARAM lParam) {
    auto* nmh = reinterpret_cast<NMHDR*>(lParam);
    if (nmh->hwndFrom == resultList_) {
        if (nmh->code == NM_CUSTOMDRAW) {
            auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                cd->clrText = theme::Text;
                cd->clrTextBk = (cd->nmcd.dwItemSpec % 2) ? theme::PanelAlt : theme::Panel;
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
        if (nmh->code == LVN_GETDISPINFO) {
            OnGetDispInfo(lParam);
        } else if (nmh->code == LVN_ITEMCHANGED || nmh->code == NM_CLICK) {
            if (nmh->code == NM_CLICK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (item->iItem >= 0 && item->iSubItem == 0) {
                    std::lock_guard<std::mutex> lock(resultsMutex_);
                    if (item->iItem < static_cast<int>(visible_.size())) {
                        size_t idx = visible_[static_cast<size_t>(item->iItem)];
                        results_[idx].checked = !results_[idx].checked;
                        ListView_RedrawItems(resultList_, item->iItem, item->iItem);
                    }
                }
            }
            UpdatePreview();
        } else if (nmh->code == LVN_COLUMNCLICK) {
            auto* lv = reinterpret_cast<NMLISTVIEW*>(lParam);
            if (lv->iSubItem == 1) SendMessageW(sortCombo_, CB_SETCURSEL, 0, 0);
            if (lv->iSubItem == 4) SendMessageW(sortCombo_, CB_SETCURSEL, 1, 0);
            if (lv->iSubItem == 6) SendMessageW(sortCombo_, CB_SETCURSEL, 2, 0);
            if (lv->iSubItem == 5) SendMessageW(sortCombo_, CB_SETCURSEL, 3, 0);
            ApplyFilters();
        }
    }
    return 0;
}

void MainWindow::OnDrawItem(LPARAM lParam) {
    auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
    if (!di || di->CtlID != IDC_PREVIEW) {
        return;
    }
    theme::FillSolid(di->hDC, di->rcItem, theme::Panel);
    HPEN pen = CreatePen(PS_SOLID, 1, theme::Border);
    HGDIOBJ oldPen = SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
    HGDIOBJ oldP = SelectObject(di->hDC, pen);
    Rectangle(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom);
    SelectObject(di->hDC, oldP);
    SelectObject(di->hDC, oldPen);
    DeleteObject(pen);

    if (previewData_.bitmap) {
        BITMAP bm{};
        GetObjectW(previewData_.bitmap, sizeof(bm), &bm);
        int boxW = di->rcItem.right - di->rcItem.left - 8;
        int boxH = di->rcItem.bottom - di->rcItem.top - 8;
        double sx = boxW / static_cast<double>(bm.bmWidth);
        double sy = boxH / static_cast<double>(bm.bmHeight);
        double s = (std::min)(sx, sy);
        int dw = static_cast<int>(bm.bmWidth * s);
        int dh = static_cast<int>(bm.bmHeight * s);
        int dx = di->rcItem.left + (boxW - dw) / 2 + 4;
        int dy = di->rcItem.top + (boxH - dh) / 2 + 4;
        HDC mem = CreateCompatibleDC(di->hDC);
        HGDIOBJ old = SelectObject(mem, previewData_.bitmap);
        SetStretchBltMode(di->hDC, HALFTONE);
        StretchBlt(di->hDC, dx, dy, dw, dh, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        theme::DrawTextAlign(di->hDC, di->rcItem, L"No preview", theme::TextDim, font_,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void MainWindow::OnGetDispInfo(LPARAM lParam) {
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(lParam);
    if (!(di->item.mask & LVIF_TEXT) && !(di->item.mask & LVIF_IMAGE)) {
        return;
    }
    std::lock_guard<std::mutex> lock(resultsMutex_);
    if (di->item.iItem < 0 || di->item.iItem >= static_cast<int>(visible_.size())) {
        return;
    }
    const RecoveredFile& f = results_[visible_[static_cast<size_t>(di->item.iItem)]];
    static thread_local std::wstring cache;
    switch (di->item.iSubItem) {
    case 0: cache = f.checked ? L"[x]" : L"[ ]"; break;
    case 1: cache = f.name; break;
    case 2: cache = f.extension; break;
    case 3: cache = f.originalPath.empty() ? L"— (carved / unknown)" : f.originalPath; break;
    case 4: cache = FormatBytes(f.size); break;
    case 5: cache = f.typeName; break;
    case 6: cache = f.hasTimestamps ? FormatFileTime(f.modified) : L"—"; break;
    case 7: cache = ToString(f.status); break;
    case 8: cache = ToString(f.confidence); break;
    case 9: cache = ToString(f.method); break;
    default: cache.clear(); break;
    }
    if (di->item.mask & LVIF_TEXT) {
        wcsncpy_s(di->item.pszText, di->item.cchTextMax, cache.c_str(), _TRUNCATE);
    }
}

void MainWindow::OnTimer() {
    UpdateStatusBar();
}

void MainWindow::OnDestroy() {
    engine_.Cancel();
}

void MainWindow::RefreshDrives() {
    SendMessageW(driveList_, LB_RESETCONTENT, 0, 0);
    drives_ = DriveEnumerator::Enumerate();
    for (const auto& d : drives_) {
        std::wstring line = DriveSummary(d);
        line += L"  ·  ";
        line += ToString(d.kind);
        SendMessageW(driveList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (!drives_.empty()) {
        SendMessageW(driveList_, LB_SETCURSEL, 0, 0);
        selectedDrive_ = 0;
        UpdateDriveSelection();
    }
}

void MainWindow::UpdateDriveSelection() {
    selectedDrive_ = SelectedDriveIndex();
    if (selectedDrive_ < 0 || selectedDrive_ >= static_cast<int>(drives_.size())) {
        return;
    }
    const auto& d = drives_[static_cast<size_t>(selectedDrive_)];
    std::wstring info = L"Selected drive\r\n";
    info += L"Letter: " + d.letter + L"\r\n";
    info += L"Name: " + (d.label.empty() ? L"Local Disk" : d.label) + L"\r\n";
    info += L"File system: " + d.fileSystemName + L"\r\n";
    info += L"Capacity: " + FormatBytes(d.totalBytes) + L"\r\n";
    info += L"Used: " + FormatBytes(d.usedBytes) + L"\r\n";
    info += L"Free: " + FormatBytes(d.freeBytes) + L"\r\n";
    info += L"Kind: " + std::wstring(ToString(d.kind)) + L"\r\n";
    if (d.physical.diskNumber != 0xFFFFFFFF) {
        info += L"Physical disk: #" + std::to_wstring(d.physical.diskNumber) + L"\r\n";
    }
    if (!d.physical.model.empty()) info += L"Model: " + d.physical.model + L"\r\n";
    if (!d.physical.busType.empty()) info += L"Bus: " + d.physical.busType + L"\r\n";
    if (!d.physical.mediaType.empty()) info += L"Media: " + d.physical.mediaType + L"\r\n";
    if (!d.physical.serial.empty()) info += L"Serial: " + d.physical.serial + L"\r\n";
    SetWindowTextW(infoEdit_, info.c_str());
}

int MainWindow::SelectedDriveIndex() const {
    return static_cast<int>(SendMessageW(driveList_, LB_GETCURSEL, 0, 0));
}

void MainWindow::StartScan() {
    int idx = SelectedDriveIndex();
    if (idx < 0 || idx >= static_cast<int>(drives_.size())) {
        MessageBoxW(hwnd_, L"Select a drive first.", L"Scan", MB_ICONINFORMATION);
        return;
    }
    if (engine_.IsBusy()) {
        return;
    }
    if (config_.warnBeforeScan && !ShowSafetyWarning(hwnd_, true)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        results_.clear();
        visible_.clear();
    }
    ListView_SetItemCountEx(resultList_, 0, LVSICF_NOINVALIDATEALL);
    lastProgress_ = {};
    scanStartTick_ = GetTickCount64();
    statusText_ = L"Starting scan...";
    UpdateStatusBar();

    HWND ui = hwnd_;
    engine_.Start(drives_[static_cast<size_t>(idx)], scanMode_, config_, ui,
        [ui](const RecoveredFile& f) {
            auto* copy = new RecoveredFile(f);
            if (!PostMessageW(ui, WM_SCAN_FILE, 0, reinterpret_cast<LPARAM>(copy))) {
                delete copy;
            }
        },
        [ui](const ScanProgress& p) {
            auto* copy = new ScanProgress(p);
            if (!PostMessageW(ui, WM_SCAN_PROGRESS, 0, reinterpret_cast<LPARAM>(copy))) {
                delete copy;
            }
        });
    SetTimer(hwnd_, 1, 400, nullptr);
    SetButtonsForState();
}

void MainWindow::PauseScan() { engine_.Pause(); SetButtonsForState(); }
void MainWindow::ResumeScan() { engine_.Resume(); SetButtonsForState(); }
void MainWindow::CancelScan() { engine_.Cancel(); SetButtonsForState(); }

void MainWindow::SetButtonsForState() {
    bool busy = engine_.IsBusy();
    bool paused = engine_.State() == ScanState::Paused;
    EnableWindow(btnStart_, !busy);
    EnableWindow(btnPause_, busy && !paused);
    EnableWindow(btnResume_, paused);
    EnableWindow(btnCancel_, busy);
    EnableWindow(driveList_, !busy);
    EnableWindow(modeQuick_, !busy);
    EnableWindow(modeDeep_, !busy);
    EnableWindow(modeRaw_, !busy);
}

void MainWindow::ApplyFilters() {
    wchar_t search[256]{};
    GetWindowTextW(searchEdit_, search, 256);
    int typeSel = static_cast<int>(SendMessageW(typeFilter_, CB_GETCURSEL, 0, 0));
    int confSel = static_cast<int>(SendMessageW(confFilter_, CB_GETCURSEL, 0, 0));
    int sizeSel = static_cast<int>(SendMessageW(sizeFilter_, CB_GETCURSEL, 0, 0));
    int dateSel = static_cast<int>(SendMessageW(dateFilter_, CB_GETCURSEL, 0, 0));
    int sortSel = static_cast<int>(SendMessageW(sortCombo_, CB_GETCURSEL, 0, 0));

    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER now64;
    now64.LowPart = now.dwLowDateTime;
    now64.HighPart = now.dwHighDateTime;

    int shown = 0;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        visible_.clear();
        visible_.reserve(results_.size());
        for (size_t i = 0; i < results_.size(); ++i) {
            const auto& f = results_[i];
            if (search[0] && !ContainsIgnoreCase(f.name, search) && !ContainsIgnoreCase(f.originalPath, search)) {
                continue;
            }
            if (typeSel > 0) {
                FileCategory want[] = {FileCategory::Other, FileCategory::Image, FileCategory::Video,
                                       FileCategory::Audio, FileCategory::Document, FileCategory::Archive,
                                       FileCategory::Database, FileCategory::Text};
                if (f.category != want[typeSel]) continue;
            }
            if (confSel > 0) {
                Confidence want[] = {Confidence::Excellent, Confidence::Excellent, Confidence::Good,
                                     Confidence::Partial, Confidence::Corrupted};
                if (f.confidence != want[confSel]) continue;
            }
            if (sizeSel == 1 && f.size >= 1024ull * 1024ull) continue;
            if (sizeSel == 2 && (f.size < 1024ull * 1024ull || f.size > 10ull * 1024ull * 1024ull)) continue;
            if (sizeSel == 3 && (f.size < 10ull * 1024ull * 1024ull || f.size > 100ull * 1024ull * 1024ull)) continue;
            if (sizeSel == 4 && f.size <= 100ull * 1024ull * 1024ull) continue;
            if (dateSel > 0 && f.hasTimestamps) {
                ULARGE_INTEGER ft;
                ft.LowPart = f.modified.dwLowDateTime;
                ft.HighPart = f.modified.dwHighDateTime;
                uint64_t age = now64.QuadPart > ft.QuadPart ? now64.QuadPart - ft.QuadPart : 0;
                const uint64_t day = 864000000000ULL;
                if (dateSel == 1 && age > 7 * day) continue;
                if (dateSel == 2 && age > 30 * day) continue;
                if (dateSel == 3 && age > 365 * day) continue;
            } else if (dateSel > 0 && !f.hasTimestamps) {
                continue;
            }
            visible_.push_back(i);
        }

        std::sort(visible_.begin(), visible_.end(), [&](size_t a, size_t b) {
            const auto& x = results_[a];
            const auto& y = results_[b];
            if (sortSel == 1) return x.size > y.size;
            if (sortSel == 2) {
                ULARGE_INTEGER xa{};
                xa.LowPart = x.modified.dwLowDateTime;
                xa.HighPart = x.modified.dwHighDateTime;
                ULARGE_INTEGER ya{};
                ya.LowPart = y.modified.dwLowDateTime;
                ya.HighPart = y.modified.dwHighDateTime;
                return xa.QuadPart > ya.QuadPart;
            }
            if (sortSel == 3) return x.typeName < y.typeName;
            return _wcsicmp(x.name.c_str(), y.name.c_str()) < 0;
        });

        lastProgress_.filesFound = results_.size();
        shown = static_cast<int>(visible_.size());
    }

    ListView_SetItemCountEx(resultList_, shown, 0);
    InvalidateRect(resultList_, nullptr, FALSE);
}

void MainWindow::UpdatePreview() {
    RecoveredFile file;
    DriveInfo drive;
    {
        int sel = ListView_GetNextItem(resultList_, -1, LVNI_SELECTED);
        std::lock_guard<std::mutex> lock(resultsMutex_);
        if (sel < 0 || sel >= static_cast<int>(visible_.size()) || selectedDrive_ < 0) {
            return;
        }
        file = results_[visible_[static_cast<size_t>(sel)]];
        drive = drives_[static_cast<size_t>(selectedDrive_)];
    }

    PreviewData data;
    preview_.Build(file, drive, config_, data);
    previewData_.Reset();
    previewData_ = data;
    data.bitmap = nullptr;

    std::wstring text = previewData_.detail + L"\r\n\r\n";
    if (!previewData_.text.empty()) {
        text += previewData_.text;
    } else if (!previewData_.unsupportedReason.empty()) {
        text += previewData_.unsupportedReason;
    }
    if (previewData_.width && previewData_.height) {
        // already in detail
    }
    SetWindowTextW(infoEdit_, text.c_str());
    InvalidateRect(previewHost_, nullptr, TRUE);
}

void MainWindow::UpdateStatusBar() {
    size_t found = 0;
    size_t shown = 0;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        found = results_.size();
        shown = visible_.size();
    }
    std::wstring s = statusText_;
    if (engine_.IsBusy() || lastProgress_.percent > 0 || found > 0) {
        s += L"   |   ";
        s += FormatPercent(lastProgress_.percent);
        s += L"   |   Files: ";
        s += std::to_wstring(found);
        s += L" shown ";
        s += std::to_wstring(shown);
        if (lastProgress_.bytesPerSecond > 0) {
            s += L"   |   ";
            s += FormatSpeed(lastProgress_.bytesPerSecond);
        }
        uint64_t elapsed = lastProgress_.elapsedMs;
        if (!elapsed && scanStartTick_) elapsed = GetTickCount64() - scanStartTick_;
        s += L"   |   Elapsed ";
        s += FormatDuration(elapsed);
        if (lastProgress_.etaMs) {
            s += L"   |   ETA ";
            s += FormatDuration(lastProgress_.etaMs);
        }
        SendMessageW(progressBar_, PBM_SETPOS, static_cast<WPARAM>(lastProgress_.percent * 10.0), 0);
    }
    SetWindowTextW(statusLabel_, s.c_str());
}

void MainWindow::RequestAdmin() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    HINSTANCE r = ShellExecuteW(hwnd_, L"runas", exe, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) > 32) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

std::vector<size_t> MainWindow::CheckedIndices() const {
    std::vector<size_t> out;
    for (size_t idx : visible_) {
        if (idx < results_.size() && results_[idx].checked) {
            out.push_back(idx);
        }
    }
    return out;
}

void MainWindow::RecoverSelected(bool all) {
    if (selectedDrive_ < 0) return;
    std::vector<RecoveredFile> files;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        if (all) {
            for (size_t idx : visible_) files.push_back(results_[idx]);
        } else {
            for (size_t idx : CheckedIndices()) files.push_back(results_[idx]);
        }
    }
    if (files.empty()) {
        MessageBoxW(hwnd_, L"Select one or more files using the [ ] checkbox column, or click Select All first.",
                    L"Recover", MB_ICONINFORMATION);
        return;
    }
    if (!ShowSafetyWarning(hwnd_, false)) {
        return;
    }

    const auto& src = drives_[static_cast<size_t>(selectedDrive_)];
    std::wstring dest = BrowseForFolder(hwnd_, L"Choose recovery destination (another drive is safer)",
                                        config_.defaultRecoveryFolder);
    if (dest.empty()) {
        return;
    }
    if (RecoveryEngine::IsSameVolume(dest, src.letter)) {
        int risk = MessageBoxW(hwnd_,
            L"The destination is on the SAME drive as the source.\r\n\r\n"
            L"Writing recovered files onto this disk can overwrite other deleted data.\r\n"
            L"Another physical drive is safer.\r\n\r\n"
            L"Continue recovery to this folder anyway?",
            L"Same-drive recovery warning",
            MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        if (risk != IDYES) {
            return;
        }
    }
    if (!ShowRecoverConfirm(hwnd_, files.size(), dest, src.letter)) {
        return;
    }

    EnableWindow(btnRecover_, FALSE);
    HWND ui = hwnd_;
    AppConfig cfg = config_;
    DriveInfo source = src;
    ScanMode mode = scanMode_;
    std::thread([ui, files, dest, cfg, source, mode]() {
        RecoveryEngine engine;
        uint64_t t0 = GetTickCount64();
        auto results = engine.Recover(source, files, dest, cfg.maxFileSize,
            [ui](const RecoveryProgress& p) {
                auto* copy = new RecoveryProgress(p);
                if (!PostMessageW(ui, WM_RECOVER_PROG, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
            });
        uint64_t dur = GetTickCount64() - t0;
        std::wstring report = RecoveryEngine::WriteReport(dest, source, results, dur);
        uint64_t ok = 0, fail = 0;
        for (const auto& r : results) {
            if (r.success) ++ok; else ++fail;
        }
        HistoryRecord rec;
        rec.timestamp = FormatNow();
        rec.sourceDrive = source.letter;
        rec.destination = dest;
        rec.scanMode = ToString(mode);
        rec.filesRecovered = ok;
        rec.filesFailed = fail;
        rec.durationMs = dur;
        HistoryStore::Add(rec);

        auto* msg = new std::wstring(
            L"Recovery finished.\r\nSucceeded: " + std::to_wstring(ok) +
            L"\r\nFailed: " + std::to_wstring(fail) +
            (report.empty() ? L"" : L"\r\nReport: " + report));
        if (!PostMessageW(ui, WM_RECOVER_DONE, 0, reinterpret_cast<LPARAM>(msg))) delete msg;
    }).detach();

    // Update statuses on UI copies - will refresh after done via user re-select
    std::lock_guard<std::mutex> lock(resultsMutex_);
    for (auto& f : results_) {
        for (const auto& picked : files) {
            if (f.id == picked.id) f.status = RecoveryStatus::Pending;
        }
    }
}

} // namespace pdr
