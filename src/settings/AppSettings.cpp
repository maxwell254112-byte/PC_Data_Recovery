#include "settings/AppSettings.h"
#include "utils/PathUtils.h"
#include "utils/StringUtils.h"
#include "carving/SignatureDb.h"

#include <windows.h>
#include <sstream>

namespace pdr {
namespace {

std::wstring ReadIni(const std::wstring& path, const wchar_t* key, const std::wstring& fallback) {
    wchar_t buf[1024]{};
    GetPrivateProfileStringW(L"PCDataRecovery", key, fallback.c_str(), buf, 1024, path.c_str());
    return buf;
}

void WriteIni(const std::wstring& path, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(L"PCDataRecovery", key, value.c_str(), path.c_str());
}

} // namespace

std::wstring AppSettings::ConfigPath() {
    EnsureDirectory(GetConfigDirectory());
    return JoinPath(GetConfigDirectory(), L"settings.ini");
}

AppConfig AppSettings::Defaults() {
    AppConfig c;
    c.defaultRecoveryFolder = JoinPath(GetExeDirectory(), L"Recovered");
    c.theme = L"dark";
    c.previewImages = true;
    c.previewPdf = true;
    c.previewText = true;
    c.maxFileSize = 4ull * 1024ull * 1024ull * 1024ull;
    c.carveChunkKb = 1024;
    c.scanRecycleBin = true;
    c.warnBeforeScan = true;
    for (const auto& s : SignatureDb::Instance().All()) {
        c.enabledExtensions.push_back(s.extension);
    }
    return c;
}

AppConfig AppSettings::Load() {
    AppConfig c = Defaults();
    std::wstring path = ConfigPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Save(c);
        return c;
    }
    c.defaultRecoveryFolder = ReadIni(path, L"DefaultRecoveryFolder", c.defaultRecoveryFolder);
    c.theme = ReadIni(path, L"Theme", L"dark");
    c.previewImages = ReadIni(path, L"PreviewImages", L"1") != L"0";
    c.previewPdf = ReadIni(path, L"PreviewPdf", L"1") != L"0";
    c.previewText = ReadIni(path, L"PreviewText", L"1") != L"0";
    c.scanRecycleBin = ReadIni(path, L"ScanRecycleBin", L"1") != L"0";
    c.warnBeforeScan = ReadIni(path, L"WarnBeforeScan", L"1") != L"0";
    try {
        c.maxFileSize = std::stoull(ReadIni(path, L"MaxFileSize", std::to_wstring(c.maxFileSize)));
    } catch (...) {}
    try {
        c.carveChunkKb = static_cast<uint32_t>(std::stoul(ReadIni(path, L"CarveChunkKb", L"1024")));
    } catch (...) {}
    std::wstring exts = ReadIni(path, L"EnabledExtensions", L"");
    if (!exts.empty()) {
        c.enabledExtensions.clear();
        std::wstringstream ss(exts);
        std::wstring item;
        while (std::getline(ss, item, L',')) {
            item = Trim(item);
            if (!item.empty()) c.enabledExtensions.push_back(ToLower(item));
        }
    }
    return c;
}

bool AppSettings::Save(const AppConfig& config) {
    std::wstring path = ConfigPath();
    WriteIni(path, L"DefaultRecoveryFolder", config.defaultRecoveryFolder);
    WriteIni(path, L"Theme", config.theme);
    WriteIni(path, L"PreviewImages", config.previewImages ? L"1" : L"0");
    WriteIni(path, L"PreviewPdf", config.previewPdf ? L"1" : L"0");
    WriteIni(path, L"PreviewText", config.previewText ? L"1" : L"0");
    WriteIni(path, L"ScanRecycleBin", config.scanRecycleBin ? L"1" : L"0");
    WriteIni(path, L"WarnBeforeScan", config.warnBeforeScan ? L"1" : L"0");
    WriteIni(path, L"MaxFileSize", std::to_wstring(config.maxFileSize));
    WriteIni(path, L"CarveChunkKb", std::to_wstring(config.carveChunkKb));
    std::wstring exts;
    for (size_t i = 0; i < config.enabledExtensions.size(); ++i) {
        if (i) exts += L",";
        exts += config.enabledExtensions[i];
    }
    WriteIni(path, L"EnabledExtensions", exts);
    return true;
}

} // namespace pdr
