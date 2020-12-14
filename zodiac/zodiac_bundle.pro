QT += widgets network quick concurrent
DESTDIR = $$_PRO_FILE_PWD_/../bin
TARGET = zodiac
TEMPLATE = app
CONFIG += debug_and_release

unix:LIBS += -ldl

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
