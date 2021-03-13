TRANSLATIONS = ../bin/i18n/details_ru.ts \
               ../bin/i18n/details_en.ts

SOURCES += src/details.cpp \
    src/expandwidget.cpp \
    src/harmonics.cpp \
    src/transits.cpp

HEADERS += src/details.h \
       src/expandwidget.h \
       src/harmonics.h \
       src/transits.h

INCLUDEPATH += ../astroprocessor/include/ \
        ../fileeditor/src/
