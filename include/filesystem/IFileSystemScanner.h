#pragma once

#include "utils/Types.h"
#include "scanner/ScanTypes.h"
#include <string>

namespace pdr {

class VolumeReader;

class IFileSystemScanner {
public:
    virtual ~IFileSystemScanner() = default;
    virtual FileSystemKind Kind() const = 0;
    virtual bool Detect(VolumeReader& reader, DriveInfo& drive) = 0;
    virtual bool ScanDeleted(VolumeReader& reader, const DriveInfo& drive, ScanMode mode,
                             IScanControl& control, const FileFoundCallback& onFile,
                             const ProgressCallback& onProgress) = 0;
};

} // namespace pdr
