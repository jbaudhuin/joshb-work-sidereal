**Zodiac** is an Astrological software for personal use.

**Current Version**: 0.9.8.2 (June 12, 2026)

**Download the latest version**: [**Windows**](https://github.com/jbaudhuin/joshb-work-sidereal/releases/download/v0.9.8.2/Zodiac-0.9.8.2-installer.exe)

This sidereal branch was to experiment with certain feature ideas in sidereal and harmonic practice.

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
* **Paranatellonta event finding (v0.9.8)**: Detect and list paran events (planet-to-planet angular co-incidences) over a search range, with a dedicated paran chart UI for visual inspection.
* **Declination strip view (v0.9.8)**: A declination strip visualization alongside the chart wheel, showing planetary declinations side-by-side for parallel/contraparallel inspection.
* **Bi-wheel prime vertical display (v0.9.8)**: Corrected bi-wheel prime vertical display so that charts are properly coordinated. This mode is better for visualizing the angular coincidences of the paranatellontas.

* **Font Installation Fix (v0.9.4.2)**: Fixed installer font installation that was failing to install required fonts
* **Speculum Display Synchronization (v0.9.4.1)**: Display mode changes in Text view now propagate to Speculum dock widget
  - **Display Modes**: Local Time, Sidereal Time, and Right Ascension modes now synchronized
  - **Radix Button**: Shows time in selected display mode format
  - **Theme Support**: Fixes to speculum dock widget and Text slide widget
  - **Cell Highlighting**: Visible gold (clicked) and blue (matched) cell backgrounds in both themes
* **Theme System (v0.9.4)**: Comprehensive Dark/Light/Printable theme support
  - **Dark/Light/Printable themes**: Complete UI theming with persistence
  - **Theme-aware colors**: HTML output, chart rendering, and table displays
  - **ThemeManager singleton**: Centralized theme management with signals
* **Event toolbar (v0.9.3)**: Added per-tab toolbar event control
  - **Toolbar**: Now it's easier to make the event selection specific to a particular tab.
  - **Miscellaneous**: cleaned up session management: count tabs not charts.
* **Session Management (v0.9.2)**: Complete workflow management system
  - **Auto-save/restore**: All open charts, tabs, and settings automatically preserved between sessions
  - **Named sessions**: Promote timestamped sessions to named sessions via **Save Session As** toolbar button
  - **Window titles**: Named sessions display in title bar (e.g., "Zodiac - My Research Session")
  - **Session database**: Browse, manage, and launch sessions from the database panel
  - **Multi-window support**: 
    - **Open in New Window**: Launch any session in a separate instance
    - **Load in Current**: Import charts from saved sessions into current workspace
    - Sessions can be double-clicked to open (Windows `.zos` file association)
  - **Smart session handling**:
    - Single-instance mode by default with automatic window raising on relaunch
    - Timestamped sessions (session-1234567890.zos) for automatic saves
    - Named sessions (MySession.zos) for organized workflows
    - Del key support for cleaning up unwanted sessions
  - **User directory storage**: Sessions stored in Documents/zodiac-charts for easy backup and cloud sync
  - **Command-line options**: `--new` for fresh start, `--load-session <file>` for specific session
  - **Preserved settings**: settings.ini and sessions.ini in user directory; API keys in install directory

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


Build Instructions:
===================

Requirements
------------

* **Qt 6.10.x** (recommended) - Qt 5.15.x should be supported
* **Boost C++ Libraries** - Install at same directory level as project root (used for chart and event search algorithms)
* **CMake 3.15+** or Qt Creator with qmake support
* **C++17 compiler** - MSVC 2019+, GCC, or Clang/LLVM

Building
--------

**Recommended: CMake Build**
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

**Alternative: Qt Creator (qmake)**

Open **zodiac/zodiac_bundle.pro** in Qt Creator, select Release configuration, and build (Ctrl+B). This produces a single executable with integrated libraries.

**Note**: Only use `zodiac_bundle.pro` - other build methods are deprecated.

Deployment
----------

Use the NSIS installer script in `nsis/` directory to create a Windows installer:
```powershell
cd nsis
.\build-installer.ps1
```

The installer packages all required Qt DLLs, resources, fonts, and ephemeris data.

Google Maps API Key
-------------------

The installer prompts for an optional Google Maps API key during installation. To enable location search, obtain a free API key from [Google Cloud Console](https://console.cloud.google.com/) and either:
- Enter it during installation, or
- Manually create `APIKey.ini` in the installation directory:
  ```ini
  [%General]
  Key=YOUR_API_KEY_HERE
  ```


External links:
===============

For more information, visit

- [SourceForge page](https://sourceforge.net/projects/zodiac-app/)
- [Personal blog](http://www.syslog.pro/tag/zodiac)
