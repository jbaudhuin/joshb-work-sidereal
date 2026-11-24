TRANSLATIONS = ../bin/i18n/details_ru.ts \
               ../bin/i18n/details_en.ts

SOURCES += src/details.cpp \
    src/expandwidget.cpp \
    src/harmonics.cpp \
    src/transits.cpp \
    src/speculum.cpp \
    src/planettable.cpp

HEADERS += src/details.h \
       src/expandwidget.h \
       src/harmonics.h \
       src/transits.h \
       src/speculum.h \
       src/planettable.h

INCLUDEPATH += ../astroprocessor/include/ \
        ../fileeditor/src/
