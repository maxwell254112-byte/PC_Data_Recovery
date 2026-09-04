#pragma once

#include "utils/Types.h"
#include <atomic>
#include <functional>
#include <mutex>

namespace pdr {

class IScanControl {
public:
    virtual ~IScanControl() = default;
    virtual bool ShouldStop() const = 0;
    virtual void WaitIfPaused() const = 0;
};

using FileFoundCallback = std::function<void(const RecoveredFile&)>;
using ProgressCallback = std::function<void(const ScanProgress&)>;

} // namespace pdr
