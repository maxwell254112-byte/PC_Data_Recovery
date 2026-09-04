#pragma once

#include "utils/Types.h"
#include "scanner/ScanTypes.h"

namespace pdr {

class VolumeReader;

class RawCarver {
public:
    bool Carve(VolumeReader& reader, const DriveInfo& drive, uint64_t maxFileSize,
               IScanControl& control, const FileFoundCallback& onFile,
               const ProgressCallback& onProgress);
};

} // namespace pdr
