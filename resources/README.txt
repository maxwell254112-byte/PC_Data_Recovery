PC Data Recovery
================

Portable Windows application for recovering deleted or lost files from
PC storage devices (internal/external HDD and SSD, USB flash drives,
and memory cards visible to Windows).

How to run
----------
Copy this entire folder to another Windows PC and double-click:

    PCDataRecovery.exe

No Python, XAMPP, Visual Studio, or other development tools are required.

Recommended
-----------
1. Run as Administrator for deleted-file (NTFS/FAT32/exFAT) and RAW scans.
2. Stop using the source drive as soon as data is lost.
3. Recover files to a different drive. Recovery onto the source volume is blocked.

Scan modes
----------
- Quick Scan : Recycle Bin + deleted filesystem records
- Deep Scan  : Thorough filesystem scan + signature carving
- RAW Recovery : File-signature carving without relying on filesystem metadata

Folders
-------
config\   portable settings
data\     local recovery history
logs\     application log

This program opens source volumes in read-only mode and never writes to them.
