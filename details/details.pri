TRANSLATIONS = ../bin/i18n/details_ru.ts \
               ../bin/i18n/details_en.ts

SOURCES += src/details.cpp \
    src/expandwidget.cpp \
    src/harmonics.cpp \
    src/transits.cpp \
    src/speculum.cpp

HEADERS += src/details.h \
       src/expandwidget.h \
       src/harmonics.h \
       src/transits.h \
       src/speculum.h

INCLUDEPATH += ../astroprocessor/include/ \
        ../fileeditor/src/
