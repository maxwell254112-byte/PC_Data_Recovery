#include "database/HistoryStore.h"
#include "utils/PathUtils.h"
#include "utils/StringUtils.h"
#include "utils/Logger.h"

#include <fstream>
#include <sstream>

namespace pdr {
namespace {

constexpr wchar_t kMagic[] = L"PCDRHIST1";

std::wstring Escape(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'\\' || c == L'|') {
            o.push_back(L'\\');
        }
        o.push_back(c);
    }
    return o;
}

std::vector<std::wstring> SplitRecord(const std::wstring& line) {
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == L'\\' && i + 1 < line.size()) {
            cur.push_back(line[++i]);
            continue;
        }
        if (line[i] == L'|') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(line[i]);
        }
    }
    parts.push_back(cur);
    return parts;
}

} // namespace

std::wstring HistoryStore::DatabasePath() {
    EnsureDirectory(GetDataDirectory());
    return JoinPath(GetDataDirectory(), L"history.db");
}

bool HistoryStore::Initialize() {
    std::wstring path = DatabasePath();
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    FILE* fp = nullptr;
    _wfopen_s(&fp, path.c_str(), L"wb");
    if (!fp) {
        return false;
    }
    std::string header = WideToUtf8(std::wstring(kMagic) + L"\n");
    fwrite(header.data(), 1, header.size(), fp);
    fclose(fp);
    return true;
}

bool HistoryStore::Add(const HistoryRecord& record) {
    Initialize();
    FILE* fp = nullptr;
    _wfopen_s(&fp, DatabasePath().c_str(), L"ab");
    if (!fp) {
        return false;
    }
    std::wstring line =
        Escape(record.timestamp) + L"|" +
        Escape(record.sourceDrive) + L"|" +
        Escape(record.destination) + L"|" +
        Escape(record.scanMode) + L"|" +
        std::to_wstring(record.filesRecovered) + L"|" +
        std::to_wstring(record.filesFailed) + L"|" +
        std::to_wstring(record.durationMs) + L"\n";
    std::string u = WideToUtf8(line);
    fwrite(u.data(), 1, u.size(), fp);
    fclose(fp);
    return true;
}

std::vector<HistoryRecord> HistoryStore::All() {
    std::vector<HistoryRecord> out;
    FILE* fp = nullptr;
    _wfopen_s(&fp, DatabasePath().c_str(), L"rb");
    if (!fp) {
        return out;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(fp);
        return out;
    }
    std::string raw(static_cast<size_t>(sz), '\0');
    fread(raw.data(), 1, raw.size(), fp);
    fclose(fp);

    std::wstring text = Utf8ToWide(raw);
    std::wstringstream ss(text);
    std::wstring line;
    int64_t id = 1;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line == kMagic) continue;
        auto parts = SplitRecord(line);
        if (parts.size() < 7) continue;
        HistoryRecord r;
        r.id = id++;
        r.timestamp = parts[0];
        r.sourceDrive = parts[1];
        r.destination = parts[2];
        r.scanMode = parts[3];
        try { r.filesRecovered = std::stoull(parts[4]); } catch (...) {}
        try { r.filesFailed = std::stoull(parts[5]); } catch (...) {}
        try { r.durationMs = std::stoull(parts[6]); } catch (...) {}
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace pdr
