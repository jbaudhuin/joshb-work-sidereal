# Changelog - Zodiac Sidereal

All notable changes to the sidereal branch of this project will be documented in this file.

## [0.9.4] - 

- **Improved shadow period transit search**: Faster search for transit of stations to demarcate the shadow period entrance and exit using a more straightforward approach than adding the stations to the general transit search list.

## [0.9.3] - 2025-12-29

### Added
- **Event Filter Toolbar**: Comprehensive per-tab event filtering in Transits UI
  - Toggle event types: T=T (Transiting to Transiting), T=N (Transiting to Natal), P=P (Progressed to Progressed)
  - Dropdown controls for sub-modes including ingress and Paranatellonta events
  - EventOptions now passed to OmnibusFinder for session-specific filtering
  - Settings synchronized across tabs and saved in session files

### Fixed
- **Session Display**: Session listings in AstroDatabase now correctly show tab count instead of chart count
  - Sessions display "N tabs" instead of previously showing total chart count
  - Renamed internal `chartCount` to `tabCount` for clarity

### Improved
- **House Support**: Added house glyph support to event displays

### Changed
- Version bumped to 0.9.3

## [0.9.2] - 2025-12-15

### Added
- **Session Management Features**:
  - **Save Session As**: Promote current timestamped session to a named session via toolbar button
    - Prompts for user-defined session name
    - Automatically switches current session to the new named file
    - Future auto-saves update the named session
  - **Session Context Menu** (right-click on session in database):
    - **Open in New Window**: Launch session in separate application instance
    - **Load in Current**: Import charts from selected session(s) into current tabs
    - **Rename Session**: Convert timestamped sessions to named sessions or rename existing
    - **Delete Sessions**: Remove unwanted session files (with protection for current session)
  - **Command-Line Options**:
    - `--new` / `-new` / `/new`: Start fresh session without restoring previous state
    - `--load-session <file>`: Launch with specific session file
    - Both options allow multiple concurrent instances
- **Single Instance Management**:
  - Default behavior: single instance with automatic window raising
  - Double-clicking zodiac icon brings existing window to foreground
  - Multiple instances supported when using `--new` or `--load-session` flags
- **Debug Logging** (debug builds only):
  - Per-instance log files (zodiac-<PID>.log)
  - Detailed session restore and command-line argument tracking

### Fixed
- Process handle inheritance issue on Windows causing freeze when launching child instances
  - Redirected stdout/stderr to null device for detached processes (debug builds)
  - Prevents I/O deadlock from orphaned parent process handles
- Session file naming: User-named sessions now use clean filenames (e.g., `MySession.zos`) instead of `session-MySession.zos`

### Changed
- Session files now use `.zos` extension (Zodiac Session) instead of generic `.ini`
- Toolbar icons: "Save Session As" uses `file.png` for visual distinction from chart save
- Version bumped to 0.9.2

## [0.9.1] - 2025-12-03

### Added
- **Events Dock Widget**: Comprehensive transit and aspect pattern finding system
  - Transit-to-transit and transit-to-natal aspects
  - Progression tracking (secondary progressions to natal and to progressions)
  - Aspect pattern detection with configurable quorum and orb settings
  - Station tracking for all planets
  - Return charts integration
  - Ingress tracking
  - Heliacal events display
  - Configurable time spans and filtering options
  - Aspect ratio and harmonic dividend display options
- **Secondary Chart Display**: Easy overlay of secondary charts with key aspect highlighting
  - Quick visualization of synastry and comparison charts
  - Aspect emphasis between charts
  - Interactive chart switching
- **Interactive Speculum**: Enhanced speculum interface for paran identification
  - Easy identification of planet-star parans
  - Multiple display modes (declination, right ascension, prime vertical)
  - Integrated filtering by orb
  - Quick navigation and analysis
- **Parans Display Integration**: 
  - Parans combined with Primary Directions timing
  - PSSR (Primary Solar/Sidereal Return) timing calculations
  - Effective dates for paran aspects
  - Grouped display by orb strength
- **Chart Database Management**: Full file organization system
  - Create subfolders within chart database
  - Move charts between folders
  - Copy charts to different locations
  - Drag-and-drop support
  - Organized directory tree view
  - Quick access to recently used charts
- **Enhanced Session Management**: Automatic restoration of work state
  - Saves all open charts and tabs between sessions
  - Restores chart view states and positions
  - Preserves transit date ranges and settings for each chart
  - Maintains window layout and dock panel configurations
  - Seamless workflow continuity across application restarts
- Comprehensive installer creation system with NSIS scripts
- Complete GPL compliance documentation
- Automated build script (build-installer.ps1) for easy installer creation
- User-facing README for distribution

### Changed
- Updated version number from 0.8.1 to 0.9.1
- Completely rewrote "About" dialog to properly credit:
  - Original author: Artem Vasilev (2012-2014)
  - Sidereal branch enhancements: Josh Baudhuin (2016-2025)
- Enhanced About dialog to highlight key sidereal features
- Improved About dialog credits section with proper library attributions

### Fixed
- CSS style rendering issues in various UI components
- Corrupted string concatenations in About dialog
- Chart display stability improvements

## [0.8.1] - 2019-02-08

### Added
- Sidereal zodiac support with Fagan-Bradley and Lahiri ayanamshas
- Harmonic charts (H1-H32) with dynamic aspect display
- Fixed star conjunctions displayed in chart wheel
- Parans and primary directions calculations
- Comprehensive events tab with transit tracking
- Speculum display with multiple modes
- Harmonic aspect patterns and midpoints
- Return charts with harmonic support
- Find chart feature for aspect patterns and transits
- Equatorial and prime vertical aspects
- Transit-to-transit and transit-to-natal tracking
- Stations display and analysis
- Aspect intensity visualization in chart wheel

### Changed
- Updated to use apparent aspects rather than true aspects
- Improved glyph layout in chart wheel
- Enhanced aspect filtering and orb controls
- Optimized event search performance

### Fixed
- Chart wheel redraw issues
- Aspect calculation accuracy

## Earlier Versions

### Original Zodiac (by Artem Vasilev)
- Initial tropical zodiac implementation
- Multiple house systems
- Basic chart display (wheel, planets, plain text)
- Detail information for planets and aspects
- Swiss Ephemeris integration
- Multi-language support (English, Russian)
- Cross-platform support (Windows, Linux)

---

## Version Numbering

This project uses semantic versioning: MAJOR.MINOR.PATCH

- **MAJOR**: Significant architectural changes or feature overhauls
- **MINOR**: New features, enhancements, significant improvements
- **PATCH**: Bug fixes, minor improvements, documentation updates

## Links

- **Sidereal Branch**: https://github.com/jbaudhuin/joshb-work-sidereal
- **Original Project**: https://github.com/atten/zodiac
- **Original SourceForge**: https://sourceforge.net/projects/zodiac-app/
