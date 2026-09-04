#pragma once

#include "utils/Types.h"
#include <functional>
#include <string>
#include <vector>

namespace pdr {

class RecoveryEngine {
public:
    using ProgressFn = std::function<void(const RecoveryProgress&)>;

    static bool IsSameVolume(const std::wstring& destination, const std::wstring& sourceLetter);
    static std::wstring WriteReport(const std::wstring& destination,
                                    const DriveInfo& source,
                                    const std::vector<RecoveryItemResult>& results,
                                    uint64_t durationMs);

    std::vector<RecoveryItemResult> Recover(const DriveInfo& source,
                                            const std::vector<RecoveredFile>& files,
                                            const std::wstring& destination,
                                            uint64_t maxFileSize,
                                            const ProgressFn& onProgress);
};

} // namespace pdr
