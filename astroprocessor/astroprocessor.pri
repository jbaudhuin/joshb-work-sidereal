SOURCES += \
    src/appsettings.cpp \
    src/astro-gui.cpp \
    src/astro-output.cpp \
    src/astro-data.cpp \
    src/astro-calc.cpp \
    src/csvreader.cpp

HEADERS +=\
    $$PWD/include/Astroprocessor/Zodiac \
    src/appsettings.h \
    src/astro-gui.h \
    src/astro-output.h \
    src/astro-data.h \
    src/astro-calc.h \
    include/Astroprocessor/Output \
    include/Astroprocessor/Gui \
    include/Astroprocessor/Data \
    include/Astroprocessor/Calc \
    include/Astroprocessor/Zodiac \
    src/csvreader.h

INCLUDEPATH += ../swe ../../boost_1_74_0

TRANSLATIONS = ../bin/i18n/astroprocessor_ru.ts \
               ../bin/i18n/astroprocessor_en.ts
