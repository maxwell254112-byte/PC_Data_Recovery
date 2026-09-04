cmake_minimum_required(VERSION 3.16)

project(PCDataRecovery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(PCDataRecovery WIN32
    src/main.cpp
    src/utils/Logger.cpp
    src/utils/StringUtils.cpp
    src/utils/FormatUtils.cpp
    src/utils/PathUtils.cpp
    src/drive/DriveEnumerator.cpp
    src/drive/VolumeReader.cpp
    src/filesystem/NtfsScanner.cpp
    src/filesystem/Fat32Scanner.cpp
    src/filesystem/ExFatScanner.cpp
    src/filesystem/RecycleBinScanner.cpp
    src/carving/SignatureDb.cpp
    src/carving/RawCarver.cpp
    src/scanner/ScanEngine.cpp
    src/recovery/RecoveryEngine.cpp
    src/preview/PreviewEngine.cpp
    src/database/HistoryStore.cpp
    src/settings/AppSettings.cpp
    src/ui/Theme.cpp
    src/ui/Dialogs.cpp
    src/ui/MainWindow.cpp
    resources/app.rc
)

target_include_directories(PCDataRecovery PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(PCDataRecovery PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /nologo /I\"${CMAKE_SOURCE_DIR}/resources\"")

if(MSVC)
    target_compile_options(PCDataRecovery PRIVATE
        /utf-8
        /W4
        /permissive-
        /Zc:__cplusplus
        /EHsc
    )
    target_link_options(PCDataRecovery PRIVATE
        /MANIFEST:NO
        /SUBSYSTEM:WINDOWS
    )
endif()

target_link_libraries(PCDataRecovery PRIVATE
    comctl32
    uxtheme
    gdiplus
    windowscodecs
    shlwapi
    ole32
    oleaut32
    uuid
    shell32
    comdlg32
    advapi32
    setupapi
    wintrust
)

set_target_properties(PCDataRecovery PROPERTIES
    OUTPUT_NAME "PCDataRecovery"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    VS_DPI_AWARE "PerMonitor"
)

add_custom_command(TARGET PCDataRecovery POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PCDataRecovery>/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PCDataRecovery>/data"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PCDataRecovery>/logs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/README.txt"
            "$<TARGET_FILE_DIR:PCDataRecovery>/README.txt"
)

set(PORTABLE_DIR "${CMAKE_SOURCE_DIR}/dist/PCDataRecovery")
add_custom_target(portable
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PORTABLE_DIR}/config"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PORTABLE_DIR}/data"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PORTABLE_DIR}/logs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:PCDataRecovery>"
            "${PORTABLE_DIR}/PCDataRecovery.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/README.txt"
            "${PORTABLE_DIR}/README.txt"
    DEPENDS PCDataRecovery
    COMMENT "Creating portable distribution folder"
)
