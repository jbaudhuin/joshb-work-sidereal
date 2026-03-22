# Copilot Instructions — Zodiac (Sidereal Astrology Desktop App)

## Project Overview

Zodiac is a Qt-based C++ desktop application for astrological charting and analysis. It provides classical chart views, a planets viewer, details/speculum panels, and plain-text horoscope output. Astronomical positions are computed via the bundled Swiss Ephemeris (swe) C library. Both tropical and sidereal zodiacs are supported.

## Repository Layout

| Directory | Purpose |
|---|---|
| `zodiac/src/` | Main application — entry point, main window, tab UI, theming, slide widget |
| `astroprocessor/src/` | Core astrological engine — calculations, data model, GUI helpers, output formatting, CSV config reader |
| `astroprocessor/include/Astroprocessor/` | Public header forwarding (Calc, Data, Gui, Output, Zodiac) |
| `swe/` | Swiss Ephemeris C library (position of planets, houses, eclipses, etc.) |
| `chart/src/` | Natal/transit chart drawing widget |
| `plain/src/` | Plain-text horoscope view |
| `planets/src/` | Planets viewer widget |
| `details/src/` | Details panel — expand widgets, harmonics, transits, speculum |
| `fileeditor/src/` | Horoscope file editor and geo-search |
| `themes/` | QSS stylesheets (dark, light, printable) and HTML-export CSS |
| `bin/` | Runtime resources — fonts, images, i18n translations, CSV data files, ephemeris data |
| `nsis/` | Windows NSIS installer scripts and assets |

## Build System

### Primary: CMake (recommended)

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

- CMake 3.16+, C++17, Ninja generator preferred.
- Qt 6.10.x recommended; Qt 5.15.x also supported.
- Boost headers expected at `../boost_1_74_0` (sibling of project root).
- `compile_commands.json` is generated in `build/` for clangd.
- Output binary goes to `bin/`.

### Alternative: qmake

Open `zodiac/zodiac_bundle.pro` in Qt Creator and build. The `.pro`/`.pri` files mirror the CMake structure.

### Verifying the toolchain

Run `check-setup.ps1` to confirm CMake, Ninja, Clang, clang-format, and Qt are available.

## Language & Compiler Standards

- **C++17** (`CMAKE_CXX_STANDARD 17`).
- **C11** for the Swiss Ephemeris files.
- Target compilers: MSVC 2019+, GCC, Clang/LLVM (project actively uses LLVM MinGW on Windows).
- `_CRT_SECURE_NO_WARNINGS` is defined globally.

## Code Style

- Formatting is enforced by `.clang-format` (BasedOnStyle: Microsoft) — Do **not** run clang-format before committing, especially since there are problems with how it formats long strings. The user will do so manually and with care.
- clangd is the primary IntelliSense provider (configured in `.clangd` and `.vscode/settings.json`).
- Header guards use `#ifndef / #define / #endif` (not `#pragma once`).
- Qt naming conventions: classes use PascalCase, member functions use camelCase.
- Signals & slots use the Qt5+ `connect(sender, &Sender::signal, receiver, &Receiver::slot)` style where possible.
- Alignment of consecutive assignments and declarations is enabled in clang-format — keep variables aligned in blocks.

## Qt Usage Conventions

- All UI is built with **Qt Widgets** (QMainWindow, QTabBar, QDockWidget, etc.).
- Qt Quick is linked but used sparingly — the primary UI layer is Widgets.
- `Qt::Concurrent` is used for background calculations.
- `Qt::Network` is used for geo-search lookups.
- Translations use Qt `.ts`/`.qm` files in `bin/i18n/`.
- The app supports Qt 5 and Qt 6; version-dependent code uses `QT_VERSION` checks (see the `VAR_TYPE` macros in `astro-data.h`).

## Key Architectural Patterns

1. **AstroFileHandler** — Base class for all panels that display horoscope data. Subclasses implement `filesUpdated()` and optionally `viewSettingsUpdated()`.
2. **AppSettings / AppSettingsEditor** — Unified settings system for persisting per-widget configuration.
3. **SlideWidget** — Animated container for swapping panels in the tab interface.
4. **ThemeManager** — Manages QSS theme loading (dark/light/printable) at runtime.
5. **CSV-driven configuration** — Planet definitions, aspect sets, signs, house systems, and zodiac modes are loaded from CSV files in `bin/astroprocessor/` via `csvreader.cpp`.

## Swiss Ephemeris (swe/) Notes

- Pure C library — do **not** add C++ constructs to `.c` files.
- Headers use `swephexp.h` as the public API; internal headers (`sweph.h`, `swephlib.h`) should not be included outside `swe/`.
- Ephemeris data files live in `bin/swe/`.

## Testing

No automated test suite exists yet. The `swetest.c` file is a Swiss Ephemeris test utility, not a project test harness.

## Installer / Deployment

- Windows installer built via NSIS: `cd nsis && .\build-installer.ps1`.
- The installer bundles Qt DLLs, runtime resources, fonts, and ephemeris data.
- See `INSTALLER_GUIDE.md` for details.

## Common Tasks

| Task | Command |
|---|---|
| Configure | `cmake -B build -G Ninja` |
| Build | `cmake --build build` |
| Clean rebuild | Remove `build/` then re-configure |
| Format code | `clang-format -i <file>` (or save in VS Code with clangd formatter) |
| Check toolchain | `pwsh check-setup.ps1` |
| Build installer | `cd nsis && pwsh build-installer.ps1` |

## Creating a New Release

When cutting a new version (e.g. 0.9.5 → 0.9.6), update **all** of the following files:

### Required version-bump files

| # | File | What to change |
|---|---|---|
| 1 | `zodiac/src/main.cpp` | `a.setApplicationVersion("v0.9.6 (build YYYY-MM-DD)");` |
| 2 | `nsis/zodiac.nsi` | `!define VERSION '0.9.6'` (line 2) |
| 3 | `nsis/zodiac-ru.nsi` | `!define VERSION '0.9.6'` (line 2) |
| 4 | `CHANGELOG.md` | Add a new `## [0.9.6] - YYYY-MM-DD` section at the top (below the header) |
| 5 | `README.md` | Update "Current Version" line and download link URL |
| 6 | `nsis/README_FOR_USERS.txt` | Update `Version X.Y.Z` on line 2 |

### Checklist

1. **Update version strings** in all six files listed above.
2. **Write the CHANGELOG entry** summarising Added / Improved / Fixed / Changed items since the previous release.
3. **Build and smoke-test**: `cmake --build build` then run `bin/zodiac.exe`.
4. **Build the installer** (optional): `cd nsis && pwsh build-installer.ps1` — it reads the version from `zodiac.nsi` automatically.
5. **Commit** with message `Release vX.Y.Z` (or similar).
6. **Tag**: `git tag vX.Y.Z && git push origin vX.Y.Z`.
7. **Create a GitHub Release** attaching the installer `.exe`.

### Notes

- `CMakeLists.txt` has `project(zodiac VERSION 1.0 ...)` — this is the CMake project version and is **not** the application release version; do not change it for a release.
- `nsis/build-installer.ps1` extracts the version from `zodiac.nsi` at build time, so no separate update is needed there.
- The `.pro` / `.pri` files (qmake) do not carry a version number.

## Guidelines for AI Assistance

- When modifying astrological calculations, always work in `astroprocessor/src/astro-calc.cpp` and keep the public API in `astro-data.h` stable.
- Preserve Qt 5 / Qt 6 compatibility unless explicitly told otherwise.
- Never modify the Swiss Ephemeris C source files unless fixing a build issue — treat them as a vendored dependency.
- Keep CSV data files (`bin/astroprocessor/*.csv`) format-stable; the column layout is parsed positionally by `csvreader.cpp`.
- Runtime resources referenced by relative path assume the working directory is `bin/`.
- Use the existing `.clang-format` style — do not introduce conflicting formatting.
