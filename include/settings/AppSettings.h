#pragma once

#include "utils/Types.h"
#include <string>

namespace pdr {

class AppSettings {
public:
    static AppConfig Load();
    static bool Save(const AppConfig& config);
    static AppConfig Defaults();
    static std::wstring ConfigPath();
};

} // namespace pdr
