#pragma once

#include <windows.h>

namespace pdr::theme {

constexpr COLORREF Bg           = RGB(18, 22, 28);
constexpr COLORREF Panel        = RGB(28, 34, 42);
constexpr COLORREF PanelAlt     = RGB(36, 44, 54);
constexpr COLORREF Border       = RGB(52, 62, 74);
constexpr COLORREF Accent       = RGB(0, 180, 166);
constexpr COLORREF AccentHot    = RGB(20, 210, 194);
constexpr COLORREF Text         = RGB(230, 234, 238);
constexpr COLORREF TextDim      = RGB(140, 150, 160);
constexpr COLORREF Danger       = RGB(232, 93, 93);
constexpr COLORREF Warn         = RGB(232, 176, 72);
constexpr COLORREF Success      = RGB(80, 200, 120);
constexpr COLORREF Excellent    = RGB(80, 200, 120);
constexpr COLORREF Good         = RGB(0, 180, 166);
constexpr COLORREF Partial      = RGB(232, 176, 72);
constexpr COLORREF Corrupted    = RGB(232, 93, 93);
constexpr COLORREF Header       = RGB(14, 17, 22);
constexpr COLORREF InputBg      = RGB(22, 27, 34);

HFONT CreateUiFont(int pointSize, bool bold = false);
void FillSolid(HDC hdc, const RECT& rc, COLORREF color);
void DrawRoundRect(HDC hdc, const RECT& rc, COLORREF fill, COLORREF border, int radius = 6);
void DrawTextAlign(HDC hdc, const RECT& rc, const wchar_t* text, COLORREF color, HFONT font,
                   UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
HBRUSH Brush(COLORREF color);

} // namespace pdr::theme
