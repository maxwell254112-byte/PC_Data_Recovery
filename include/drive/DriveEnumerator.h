#pragma once

#include "utils/Types.h"
#include <vector>

namespace pdr {

class DriveEnumerator {
public:
    static std::vector<DriveInfo> Enumerate();

private:
    static void FillVolumeDetails(DriveInfo& drive);
    static void FillPhysicalDetails(DriveInfo& drive);
    static DriveKind Classify(const DriveInfo& drive, DWORD bus, bool rotational);
    static FileSystemKind ParseFileSystem(const std::wstring& name);
};

} // namespace pdr
