TRANSLATIONS = ../bin/i18n/zodiac_ru.ts \
               ../bin/i18n/zodiac_en.ts

SOURCES += src/main.cpp \
    $$PWD/src/afileinfo.cpp \
       src/mainwindow.cpp \
    src/help.cpp \
    src/slidewidget.cpp \
    src/thememanager.cpp \
    src/crashhandler.cpp \
    src/helptopics.cpp

HEADERS  += src/mainwindow.h \
    $$PWD/src/afileinfo.h \
    src/help.h \
    src/slidewidget.h \
    src/thememanager.h \
    src/crashhandler.h \
    src/helptopics.h

# Developer-only screenshot mode (docs/doc-capture.md): qmake CONFIG+=doc_capture
doc_capture {
    DEFINES += ZODIAC_DOC_CAPTURE
    SOURCES += src/doccapture.cpp src/doccapturescript.cpp src/widgetpicker.cpp
    HEADERS += src/doccapture.h src/doccapturescript.h src/widgetpicker.h
}

## win icon, etc
win32: RC_FILE = app.rc

INCLUDEPATH += ../astroprocessor/include/
