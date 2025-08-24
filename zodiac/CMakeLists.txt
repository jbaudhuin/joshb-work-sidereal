cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_COMPILER C:/Qt/Tools/llvm-mingw1706_64/bin/clang.exe)
set(CMAKE_CXX_COMPILER C:/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe)
project(zodiac VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent Gui Network Quick Widgets)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Core5Compat)

qt_standard_project_setup()

qt_add_executable(zodiac WIN32 MACOSX_BUNDLE
    ../astroprocessor/include/Astroprocessor/Zodiac
    ../astroprocessor/include/Astroprocessor/Calc
    ../astroprocessor/include/Astroprocessor/Data
    ../astroprocessor/include/Astroprocessor/Gui
    ../astroprocessor/include/Astroprocessor/Output
    ../astroprocessor/include/Astroprocessor/Zodiac
    ../zodiac/src/afileinfo.cpp ../zodiac/src/afileinfo.h
    ../astroprocessor/src/appsettings.cpp ../astroprocessor/src/appsettings.h
    ../astroprocessor/src/astro-calc.cpp ../astroprocessor/src/astro-calc.h
    ../astroprocessor/src/astro-data.cpp ../astroprocessor/src/astro-data.h
    ../astroprocessor/src/astro-gui.cpp ../astroprocessor/src/astro-gui.h
    ../astroprocessor/src/astro-output.cpp ../astroprocessor/src/astro-output.h
    ../chart/src/chart.cpp ../chart/src/chart.h
    ../astroprocessor/src/csvreader.cpp ../astroprocessor/src/csvreader.h
    ../details/src/details.cpp ../details/src/details.h
    ../details/src/expandwidget.cpp ../details/src/expandwidget.h
    ../details/src/harmonics.cpp ../details/src/harmonics.h
    ../details/src/transits.cpp ../details/src/transits.h
    ../fileeditor/src/fileeditor.cpp ../fileeditor/src/fileeditor.h
    ../fileeditor/src/geosearch.cpp ../fileeditor/src/geosearch.h
    ../zodiac/src/help.cpp ../zodiac/src/help.h
    ../zodiac/src/main.cpp
    ../zodiac/src/mainwindow.cpp ../zodiac/src/mainwindow.h
    ../plain/src/plain.cpp ../plain/src/plain.h
    ../planets/src/planets.cpp ../planets/src/planets.h
    ../zodiac/src/slidewidget.cpp ../zodiac/src/slidewidget.h
    ../swe/swecl.c
    ../swe/swedate.c ../swe/swedate.h
    ../swe/swedll.h
    ../swe/swehel.c
    ../swe/swehouse.c ../swe/swehouse.h
    ../swe/swejpl.c ../swe/swejpl.h
    ../swe/swemmoon.c
    ../swe/swemplan.c
    ../swe/swemptab.h
    ../swe/swenut2000a.h
    ../swe/sweodef.h
    ../swe/swepcalc.h
    ../swe/sweph.c ../swe/sweph.h
    ../swe/swephexp.h
    ../swe/swephlib.c ../swe/swephlib.h
)
target_include_directories(zodiac PRIVATE
    ../../boost_1_74_0
    ../astroprocessor/include
    ../fileeditor/src src
    ../swe
)

target_compile_definitions(zodiac PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(zodiac PRIVATE
    Qt::Concurrent
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Quick
    Qt::Widgets
)

if((QT_VERSION_MAJOR GREATER 5))
    target_link_libraries(zodiac PRIVATE
        Qt::Core5Compat
    )
endif()

if(UNIX)
    target_link_libraries(zodiac PRIVATE
        dl
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL Release)
    target_compile_definitions(zodiac PRIVATE
        NDEBUG
        QT_NO_DEBUG_OUTPUT
    )
endif()

if(CMAKE_BUILD_TYPE STREQUAL Debug)
    target_compile_definitions(zodiac PRIVATE
        _ZOD_DEBUG
    )
endif()

if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32))
    target_compile_options(zodiac PRIVATE -gcodeview)
    target_link_options(zodiac PRIVATE -gcodeview)
endif()

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

qt_generate_deploy_app_script(
    TARGET zodiac
    FILENAME_VARIABLE deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# Install all subdirectories and files from bin into the install bin directory
install(DIRECTORY ${CMAKE_SOURCE_DIR}/../bin/
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILES_MATCHING PATTERN "*"
)
