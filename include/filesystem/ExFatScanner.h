#pragma once

#include "filesystem/IFileSystemScanner.h"

namespace pdr {

class ExFatScanner : public IFileSystemScanner {
public:
    FileSystemKind Kind() const override { return FileSystemKind::ExFat; }
    bool Detect(VolumeReader& reader, DriveInfo& drive) override;
    bool ScanDeleted(VolumeReader& reader, const DriveInfo& drive, ScanMode mode,
                     IScanControl& control, const FileFoundCallback& onFile,
                     const ProgressCallback& onProgress) override;
};

} // namespace pdr
