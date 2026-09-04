#pragma once

#include "utils/Types.h"
#include "scanner/ScanTypes.h"

namespace pdr {

class RecycleBinScanner {
public:
    static void Scan(const DriveInfo& drive, IScanControl& control,
                     const FileFoundCallback& onFile);
};

} // namespace pdr
