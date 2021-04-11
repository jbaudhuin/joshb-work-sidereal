**Zodiac** is an Astrological software for personal use.

This sidereal branch was to experiment with certain feature ideas in sidereal and harmonic practice.

**Download the latest version**: [**Windows**](http://sourceforge.net/projects/zodiac-app/files/Zodiac-0.7.1-installer.exe/download) | [**Linux64**](http://sourceforge.net/projects/zodiac-app/files/zodiac-0.7.1.tar.gz/download)

Features:
=========

* Contains different views of horoscope: classical chart, planets view and plain text;
* Provides detail information about each element of horoscope;
* Uses 9 house systems and over 40 aspects for horoscope calculation;
* Allows to view tropical and sidereal zodiacs;
* Compact, dock-based user interface;
* Includes Swiss Ephemeris � library;
* English & Russian language support.

The original version includes a 'sidereal' ayanamsha, but it's a non-equal sign, intended to cohere to the actual constellations. Some people like this! Some people even want to include a 13th sign (Ophiucus) because the Ecliptic runs through it, so why not. There is a lot of evidence that the Babylonians not only used a sidereal zodiac, but that they also used equal-portioned, 30-degree signs. I added two sidereal ayanamshas, the Fagan-Bradley (which uses Aldebaran as a fiducial star, "locking" it at 15 Taurus; it is used by Western Sidereal practitioners), and the Lahiri (which uses Spica as fiducial at 0 Libra; it is commonly used in Jyotish [aka Vedic]).

Sidereal version additions/updates:
* Corrected to use apparent rather than true aspect. Most astro software uses apparent, so now it's consistent with other commercially-available tools.
* Added 30-degree ayanamshas, Fagan-Bradley and Lahiri.
* Chart-wheel shows fixed stars that are within 1.5 degrees direct distance (i.e., in 2 dimensions).
* Added "harmonic" control to display the current chart-wheel using that harmonic. Since it's a display option, you don't have to create a new harmonic chart, you just switch the display to that harmonic.
* Added speculum and parans + primary directions. [These are calculated in a somewhat idiosyncratic way, so more work to be done here.] The innovations here are (1) excludes star-to-star parans, i.e., just shows relevant planet-to-star parans; (2) shows a "effective" date for the paran based on RA-degree-for-year primary directions; (3) groups parans by orb.
   As I said, the calculations are somewhat idiosyncratic in that I simply took the angular transit time for the bodies and used
this as a kind of primary. This is different from taking the right ascension or oblique ascension and then rotating the primum mobile as though the angular placements were fixed. It is even arguable whether this is possibly a better way to do it, but it is not the way it is typically done. I *tried* to do this the right way and failed! (i.e., it didn't seem to match up what other programs, such as Janus, were doing.)
* Added Harmonics tab which displays a tree of harmonic aspects and midpoints. Three different sorts: by harmonic, by planetary combination, and by orb. It's super easy to update the harmonic of the chart-wheel display so that aspects can easily be seen. The harmonics list also includes "overtones". Thus, I can see the H4 patterns (squares) in an H9 chart, or, conversely, the H9 patterns (noviles) in an H4 chart. Midpoints are represented in a somewhat idiosyncratic way because the Almagest font doesn't include = and /.
* There are various options for tuning and filtering the scope to help limit the noise. Basically, it made sense to have a tighter orb-span requirement for fewer planets, looser for more planets. So there's a minimum quorum and a maximum quorum and an orb for each. I like 8 degrees for minimum 3 planets, and 16 degrees for maximum 5 planets. That effectively makes 4 planets need something like 12 degrees. Anything above 5 planets still has to have that 16-degree span. I ignore two-planet harmonics because that generates an insane amount of noise.
* Added return charts. These use current harmonic setting, so you could enter H4 and then select a return and it would generate the nearest quarterly return, which might be the actual return. The subsequent work on "find chart" does this a little better.
* Added "find chart" feature to allow arbitrary harmonic aspect, ingress or planetary return search over a time range. This includes transit-to-transit and transit-to-natal aspects, and precise aspects as well as aspect patterns (described by harmonic). This works reasonably and surprisingly well, but sometimes it misses 3+ planet combos that it should catch. This functionality will soon be migrated to the following:
* Added Events tab which (currently) displays transits-to-transits and transits-to-natal and stations. It also includes an summary of aspects in orb for certain events, like stations and returns. [This will be enhanced further to show progressions, aspect patterns, and incorporate primary directions, and will eventually allow the user to in-weave a list of actual events to allow rectification based on transits or directions. Sooner rather than later, the arbitrary aspect/pattern search of "Find Chart" will be incorporated here.]
* The event search is pretty speedy, and on an optimized build it takes about a second to bring up a year's worth of transits
to-transits, transits-to-natal, returns-to-natal, and stations. For all harmonics 1 through 32 it takes about 4 seconds.
* Added equatorial and prime vertical aspects and display. The prime vertical display is not quite correct in the chart-wheel, but the aspects are displayed.
* Added dynamic harmonic aspect display up to H32. That is, you can show all aspect lines from H1 to H32 on the chart-wheel. It is easy to add or subtract one or more of these harmonics as desired: just click on the appropriate button on the status bar. Ptolemaic aspects would be: 1 2 3 6 8.
* Chart-wheel now shows aspect intensity by thickening the aspect line.
* Improved glyph layout in chart-wheel. Still not perfect, but I think it's better.

Mainly, the events listing is where I'm focusing most of my efforts lately, to make it quick, easy and powerful.

Subprojects:
------------

``Zodiac` project consists of following parts:

* ``zodiac`` - front-end application with tab interface and user-defined files management;
* ``astroprocessor`` - library with classes for astrological calculations and settings management;
* ``swe`` - Swiss Ephemeris Library. Provides positions of planets, houses etc;
* ``chart`` - library for making natal chart;
* ``plain`` - library for making simple text view of horoscope;
* ``planets`` - library for making planets viewer;
* ``details`` - library for displaying planet properties;
* ``fileeditor`` - library for editing horoscope data.


Content of subdirs:
------------

* ***bin/*** - executable, libraries and other application files.
* ***bin/i18n/*** - localization files for all projects;
* ***bin/images/*** - various astrological images used in application
* ***bin/style/*** - CSS and icons for application
* ***bin/text/*** - interpretations of astrological items
* ***bin/user/*** - collection of user files (File->Open & File->Save)
* ***bin/astroprocessor/***,
* ***bin/chart/***,
* ***bin/fileeditor/***,
* ***bin/plain/***,
* ***bin/planets/***,
* ***bin/swe/*** - files used by Swiss Ephemeris library;
* ***chart/***, ***fileeditor/***, ***plain/***, ***planets/***, ***details/***, ***astroprocessor/***, ***swe/***, ***zodiac/*** - subprojects
* ***nsis/*** - files for Nullsoft Scriptable Install System


Build instructions:
===================

Requirement: Qt > 4.8 (works in Qt 4.8.2 and in Qt 5.3 as well). [See sidereal version notes below.]

Method 1: automatic bundle build
---------------------------------

This method will produce a single executable file with integrated project libraries.

Open **zodiac/zodiac_bundle.pro** in Qt Creator, select a build configuration (Debug or Release) then build (Ctrl+B).

Method 2: automatic separate build (Windows only)
-----------------------------------

Open **all.pro** in Qt Creator, and build it as usual. It will produce a main executable (zodiac) with a few dynamic libraries.

Method 3: manual build
-----------------------

This is a variation of 'method 2' that supposes manual building of all subprojects.
Open projects in QtCreator and build them manually in the following order:

    1. swe/swe.pro
    2. astroprocessor/astroprocessor.pro
    3. chart/chart.pro
    fileeditor/fileeditor.pro
    plain/plain.pro
    planets/planets.pro
	planets/details.pro
    4. zodiac/zodiac.pro

[Sidereal version build notes:
I've been building with 5.15.x. I've used MSVS19 and g++. I have made updates to support Qt 6, however I encountered strange redraw effects, and I haven't investigated what could be wrong. I am also requiring the boost library as well, installed in a directory at the same level as the root directory of this project, because it has some of the algorithms used in the chart and event searches.
N.B. **Only use zodiac_bundle.**]

Additional note: the original google and yandex geo lookup used http: protocol, however, these are not actually supported any longer. You have to use https. I joined the google developer program and created an API key, and perhaps you should to! Not sure about the yandex (russian) service -- I didn't bother with that. 


Run on Windows:
===============

To run application on Windows after build, put following Qt libraries into app directory:

	imageformats/qgif.dll
	imageformats/qico.dll
	imageformats/qjpeg.dll
	platforms/qwindows.dll

In Qt 5.2, they are located in ***C:\Qt\5.2.1\mingw48_32\plugins***.


External links:
===============

For more information, visit

- [SourceForge page](https://sourceforge.net/projects/zodiac-app/)
- [Personal blog](http://www.syslog.pro/tag/zodiac)
