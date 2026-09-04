#pragma once

#include "utils/Types.h"
#include <windows.h>
#include <string>
#include <vector>

namespace pdr {

bool ShowSafetyWarning(HWND parent, bool forScan);
bool ShowRecoverConfirm(HWND parent, size_t count, const std::wstring& destination, const std::wstring& sourceLetter);
void ShowSettingsDialog(HWND parent, AppConfig& config);
void ShowHistoryDialog(HWND parent);
void ShowAboutDialog(HWND parent);
std::wstring BrowseForFolder(HWND parent, const std::wstring& title, const std::wstring& start);

} // namespace pdr
