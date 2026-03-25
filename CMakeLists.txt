cmake_minimum_required(VERSION 3.20)
project(ANNEWizard VERSION 0.1 LANGUAGES CXX)
message(STATUS "MINGW: ${MINGW}")
option(STATIC_BUILD "Build a fully static executable" OFF)

if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00 
        WINVER=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    
    if(MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS,6.2")
    endif()
endif()


# Set Windows 10 compatibility flags before anything else
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
    
    # Optional: Set minimum Windows version for MSVC
    if(MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS,6.2")
    endif()
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(QT6_DIR "$ENV{QT6_DIR}" CACHE PATH "Qt6 directory")

if(QT6_DIR AND NOT "${QT6_DIR}" STREQUAL "")
    list(APPEND CMAKE_PREFIX_PATH "${QT6_DIR}")
    message(STATUS "Using custom Qt6 at: ${QT6_DIR}")
else()
    message(STATUS "Using system Qt6")
endif()

# SIMPLE FIX: Just set target properties, no global RPATH settings
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Qml Concurrent DBus)
# find_package(QuaZip-Qt6 REQUIRED)
# find_package(ZLIB REQUIRED)

# ... rest unchanged ...
set(PROJECT_SOURCES
    main.cpp
    wizard.cpp
    pages/intropage.cpp
    pages/licensepage.cpp
    pages/javapage.cpp
    pages/mariadbpage.cpp
    pages/dbsetuppage.cpp
    pages/downloadpage.cpp
    pages/netconfigpage.cpp
    pages/configpage.cpp
    pages/minerconfigpage.cpp
    pages/finalpage.cpp
    utils/osinfo.cpp
    utils/utils.cpp
    utils/dbmanager.cpp
    utils/filehandler.cpp
    utils/netutils.cpp
    utils/systemutils.cpp
    utils/antorutils.cpp
    utils/jsutils.cpp
    utils/javautils.cpp
    utils/mariautils.cpp
    resources.qrc
)

set(PROJECT_HEADERS
    wizard.h
    pages/intropage.h
    pages/licensepage.h
    pages/javapage.h
    pages/mariadbpage.h
    pages/dbsetuppage.h
    pages/downloadpage.h
    pages/netconfigpage.h
    pages/configpage.h
    pages/minerconfigpage.h
    pages/finalpage.h
    utils/concurrent.h
    utils/osinfo.h
    utils/utils.h
    utils/dbmanager.h
    utils/filehandler.h
    utils/netutils.h
    utils/systemutils.h
    utils/antorutils.h
    utils/jsutils.h
    utils/javautils.h
    utils/mariautils.h
)

qt_add_executable(ANNEWizard
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
)

target_link_libraries(ANNEWizard PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Network Qt6::Qml Qt6::Concurrent Qt6::DBus   
)
# if(WIN32 AND MINGW)
#     target_link_options(ANNEWizard PRIVATE
#         -static
#         -static-libgcc
#         -static-libstdc++
#         -mwindows
#         -Wl,--major-subsystem-version,6
#         -Wl,--minor-subsystem-version,2
#     )
# endif()
# ${ZLIB_LIBRARIES}
#QuaZip::QuaZip

# include_directories(${ZLIB_INCLUDE_DIRS})

# CRITICAL: Set RPATH directly on target, not globally
if(QT6_DIR AND NOT "${QT6_DIR}" STREQUAL "")
    # Use full absolute path without $ORIGIN for development
    set_target_properties(ANNEWizard PROPERTIES
        BUILD_RPATH "${QT6_DIR}/lib"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

set_target_properties(ANNEWizard PROPERTIES
    OUTPUT_NAME "annewizard"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Windows – use the .rc file with Windows 10 flags
if(WIN32)
    target_sources(ANNEWizard PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/annewizard.rc")
    # Set compile flags for the RC file too
    if(MSVC)
        set_source_files_properties(
            "${CMAKE_CURRENT_SOURCE_DIR}/annewizard.rc"
            PROPERTIES COMPILE_FLAGS "/DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00"
        )
    endif()
endif()

if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE annewizard.icns)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/assets/annewizard.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(ANNEWizard PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/annewizard.icns")
endif()

qt_finalize_executable(ANNEWizard)

if(WIN32 AND STATIC_BUILD)
    target_link_options(ANNEWizard PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
        -mwindows
        -Wl,--major-subsystem-version,6
        -Wl,--minor-subsystem-version,2
    )
    set_target_properties(ANNEWizard PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    # For shared builds, still need GUI subsystem
    set_target_properties(ANNEWizard PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MINGW)
        target_link_options(ANNEWizard PRIVATE -mwindows)
    endif()
endif()