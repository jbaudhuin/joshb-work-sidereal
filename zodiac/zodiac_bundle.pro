QT += widgets network quick concurrent
greaterThan(QT_MAJOR_VERSION,5) {
    QT += core5compat
}

DESTDIR = $$_PRO_FILE_PWD_/../bin
TARGET = zodiac
TEMPLATE = app
CONFIG += debug_and_release

unix:LIBS += -ldl

# Crash handler needs dbghelp (MiniDumpWriteDump / StackWalk64) on Windows.
win32:LIBS += -ldbghelp

# Emit a PDB for the optimized release build so post-mortem crash dumps can be
# symbolized. /Zi keeps full optimization; /OPT:REF,/OPT:ICF strip the debug
# bloat the linker would otherwise add. Keep zodiac.pdb archived per release.
win32-msvc* {
    QMAKE_CXXFLAGS_RELEASE += /Zi
    QMAKE_LFLAGS_RELEASE   += /DEBUG /OPT:REF /OPT:ICF
}

VPATH += ../swe ../astroprocessor ../chart ../fileeditor ../plain ../planets ../details

include(../swe/swe.pri)
include(../astroprocessor/astroprocessor.pri)
include(../chart/chart.pri)
include(../fileeditor/fileeditor.pri)
include(../plain/plain.pri)
include(../planets/planets.pri)
include(../details/details.pri)
include(zodiac.pri)

CONFIG(release, debug|release): DEFINES += QT_NO_DEBUG_OUTPUT
#CONFIG(release, debug|release): DEFINES += _CRT_SECURE_NO_WARNINGS
DEFINES += _CRT_SECURE_NO_WARNINGS

CONFIG(release, debug|release): DEFINES += NDEBUG
CONFIG(debug, debug|release): DEFINES += _ZOD_DEBUG

HEADERS +=

SOURCES +=
