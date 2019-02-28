QT += widgets

TEMPLATE = lib
DESTDIR = $$_PRO_FILE_PWD_/../bin
include(details.pri)

# library dependencies
LIBS += -L$$_PRO_FILE_PWD_/../bin
LIBS += -lastroprocessor

HEADERS += \
    src/harmonics.h

SOURCES += \
    src/harmonics.cpp































