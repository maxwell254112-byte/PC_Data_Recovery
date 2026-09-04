#pragma once

#include "utils/Types.h"
#include <string>
#include <vector>

namespace pdr {

class HistoryStore {
public:
    static bool Initialize();
    static bool Add(const HistoryRecord& record);
    static std::vector<HistoryRecord> All();
    static std::wstring DatabasePath();
};

} // namespace pdr
