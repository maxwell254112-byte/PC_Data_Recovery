#include "ui/Theme.h"
#include <map>
#include <mutex>

namespace pdr::theme {
namespace {
std::mutex brushMutex;
std::map<COLORREF, HBRUSH> brushes;
}

HFONT CreateUiFont(int pointSize, bool bold) {
    HDC hdc = GetDC(nullptr);
    int log = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    return CreateFontW(log, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void FillSolid(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

void DrawRoundRect(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border, int radius) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

void DrawTextAlign(HDC hdc, const RECT& rc, const wchar_t* text, COLORREF color, HFONT font, UINT format) {
    HGDIOBJ old = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    RECT copy = rc;
    DrawTextW(hdc, text, -1, &copy, format);
    SelectObject(hdc, old);
}

HBRUSH Brush(COLORREF color) {
    std::lock_guard<std::mutex> lock(brushMutex);
    auto it = brushes.find(color);
    if (it != brushes.end()) {
        return it->second;
    }
    HBRUSH br = CreateSolidBrush(color);
    brushes[color] = br;
    return br;
}

} // namespace pdr::theme
