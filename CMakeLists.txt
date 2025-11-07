cmake_minimum_required(VERSION 3.16)

# Project configuration - machine independent
project(zodiac VERSION 1.0 LANGUAGES C CXX)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Find Qt installation - this will work with any Qt version and kit
find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS 
    Concurrent 
    Gui 
    Network 
    Quick 
    Widgets
)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Core5Compat)

qt_standard_project_setup()

# Define source files based on .pri files structure
set(SWE_SOURCES
    swe/swedate.c
    swe/swehouse.c
    swe/swejpl.c
    swe/swemmoon.c
    swe/swemplan.c
    swe/sweph.c
    swe/swephlib.c
    swe/swecl.c
    swe/swehel.c
)

set(SWE_HEADERS
    swe/swedate.h
    swe/swedll.h
    swe/swemptab.h
    swe/swehouse.h
    swe/swejpl.h
    swe/swenut2000a.h
    swe/sweodef.h
    swe/swepcalc.h
    swe/swephexp.h
    swe/sweph.h
    swe/swephlib.h
)

set(ASTROPROCESSOR_SOURCES
    astroprocessor/src/appsettings.cpp
    astroprocessor/src/astro-gui.cpp
    astroprocessor/src/astro-output.cpp
    astroprocessor/src/astro-data.cpp
    astroprocessor/src/astro-calc.cpp
    astroprocessor/src/csvreader.cpp
)

set(ASTROPROCESSOR_HEADERS
    astroprocessor/include/Astroprocessor/Zodiac
    astroprocessor/src/appsettings.h
    astroprocessor/src/astro-gui.h
    astroprocessor/src/astro-output.h
    astroprocessor/src/astro-data.h
    astroprocessor/src/astro-calc.h
    astroprocessor/include/Astroprocessor/Output
    astroprocessor/include/Astroprocessor/Gui
    astroprocessor/include/Astroprocessor/Data
    astroprocessor/include/Astroprocessor/Calc
    astroprocessor/src/csvreader.h
)

set(CHART_SOURCES
    chart/src/chart.cpp
)

set(CHART_HEADERS
    chart/src/chart.h
)

set(FILEEDITOR_SOURCES
    fileeditor/src/fileeditor.cpp
    fileeditor/src/geosearch.cpp
)

set(FILEEDITOR_HEADERS
    fileeditor/src/fileeditor.h
    fileeditor/src/geosearch.h
)

set(PLAIN_SOURCES
    plain/src/plain.cpp
)

set(PLAIN_HEADERS
    plain/src/plain.h
)

set(PLANETS_SOURCES
    planets/src/planets.cpp
)

set(PLANETS_HEADERS
    planets/src/planets.h
)

set(DETAILS_SOURCES
    details/src/details.cpp
    details/src/expandwidget.cpp
    details/src/harmonics.cpp
    details/src/transits.cpp
    details/src/speculum.cpp
)

set(DETAILS_HEADERS
    details/src/details.h
    details/src/expandwidget.h
    details/src/harmonics.h
    details/src/transits.h
    details/src/speculum.h
)

set(ZODIAC_SOURCES
    zodiac/src/main.cpp
    zodiac/src/afileinfo.cpp
    zodiac/src/mainwindow.cpp
    zodiac/src/help.cpp
    zodiac/src/slidewidget.cpp
)

set(ZODIAC_HEADERS
    zodiac/src/afileinfo.h
    zodiac/src/mainwindow.h
    zodiac/src/help.h
    zodiac/src/slidewidget.h
)

# Create the executable with all sources
qt_add_executable(zodiac WIN32 MACOSX_BUNDLE
    ${SWE_SOURCES} ${SWE_HEADERS}
    ${ASTROPROCESSOR_SOURCES} ${ASTROPROCESSOR_HEADERS}
    ${CHART_SOURCES} ${CHART_HEADERS}
    ${FILEEDITOR_SOURCES} ${FILEEDITOR_HEADERS}
    ${PLAIN_SOURCES} ${PLAIN_HEADERS}
    ${PLANETS_SOURCES} ${PLANETS_HEADERS}
    ${DETAILS_SOURCES} ${DETAILS_HEADERS}
    ${ZODIAC_SOURCES} ${ZODIAC_HEADERS}
)

# Set up include directories
target_include_directories(zodiac PRIVATE
    swe
    astroprocessor/include
    astroprocessor/src
    fileeditor/src
    zodiac/src
    # Try to find boost - make it optional for now
    ${CMAKE_CURRENT_SOURCE_DIR}/../boost_1_74_0
)

# Compiler definitions based on .pro files
target_compile_definitions(zodiac PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

# Link Qt libraries
target_link_libraries(zodiac PRIVATE
    Qt::Concurrent
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Quick
    Qt::Widgets
)

# Link Core5Compat if Qt6
if(QT_VERSION_MAJOR GREATER 5)
    target_link_libraries(zodiac PRIVATE
        Qt::Core5Compat
    )
endif()

# Unix-specific libraries
if(UNIX)
    target_link_libraries(zodiac PRIVATE
        dl
    )
endif()

# Build type specific definitions
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(zodiac PRIVATE
        NDEBUG
        QT_NO_DEBUG_OUTPUT
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(zodiac PRIVATE
        _ZOD_DEBUG
    )
endif()

# Debug information - cross-platform approach
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # Use DWARF debug format for Clang/GCC (works with LLDB and GDB)
        target_compile_options(zodiac PRIVATE -g -O0)
        target_link_options(zodiac PRIVATE -g)
    elseif(MSVC)
        # Use PDB format for MSVC
        target_compile_options(zodiac PRIVATE /Zi /Od)
        target_link_options(zodiac PRIVATE /DEBUG)
    endif()
    
    # Disable optimizations to improve debugging experience
    set_target_properties(zodiac PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
    )
endif()

# Set output directory to match .pro behavior
set_target_properties(zodiac PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin
    # Set working directory for debugging
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin
)

# Windows RC file - temporarily disabled due to windres issues
# if(WIN32)
#     if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/zodiac/app.rc)
#         target_sources(zodiac PRIVATE zodiac/app.rc)
#     endif()
# endif()

install(TARGETS zodiac
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install debug symbols in Debug builds
if(CMAKE_BUILD_TYPE STREQUAL Debug)
    if(WIN32)
        # Install .pdb files (MSVC/MinGW/Clang on Windows)
        install(
            DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING PATTERN "*.pdb"
        )
    elseif(APPLE)
        # Install .dSYM bundles (macOS)
        install(
            DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/zodiac.app.dSYM
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            OPTIONAL
        )
    elseif(UNIX)
        # Install .debug files (Linux, if split debug info is used)
        install(
            DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILES_MATCHING PATTERN "*.debug"
        )
    endif()
endif()

# Qt deployment script (for Qt 6.10+)
if(QT_VERSION_MAJOR EQUAL 6 AND QT_VERSION_MINOR GREATER_EQUAL 10)
    qt_generate_deploy_app_script(
        TARGET zodiac
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
else()
    qt_generate_deploy_app_script(
        TARGET zodiac
        FILENAME_VARIABLE deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
endif()
install(SCRIPT ${deploy_script})

# Install all subdirectories and files from bin into the install bin directory
install(DIRECTORY ${CMAKE_SOURCE_DIR}/bin/
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILES_MATCHING PATTERN "*"
    PATTERN "zodiac.exe" EXCLUDE # already installed as target
    PATTERN "zodiac.pdb" EXCLUDE # already installed above
    PATTERN "*.ini" EXCLUDE
)