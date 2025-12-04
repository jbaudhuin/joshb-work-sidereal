# Installer Creation Guide for Zodiac Sidereal

## Prerequisites

### 1. Install NSIS (Nullsoft Scriptable Install System)
- Download from: https://nsis.sourceforge.io/Download
- Install to default location (usually `C:\Program Files (x86)\NSIS`)
- The installer script is already configured in `nsis/zodiac.nsi`

### 2. Prepare Your Build
Before creating an installer, you need a complete build with all dependencies:

```powershell
# Build the application using CMake
cmake --build build --config Release
```

## Important Files and Considerations

### License Compliance (GPL v3)

Your application uses:
- **GPL v3** for your code (LICENSE file)
- **Swiss Ephemeris** under GPL v2+ (swe/LICENSE)

**GPL Requirements for Distribution:**
1. ✅ Include license text (already done in `nsis/license.txt`)
2. ✅ Make source code available to recipients
3. ✅ Display license during installation (already in NSIS script)
4. ⚠️ **IMPORTANT:** You must provide source code or a written offer to provide it

**Best practice:** Include a README in the installer that points users to:
- Your GitHub repository: https://github.com/jbaudhuin/joshb-work-sidereal
- Or include source code on a separate medium/download

### Files to EXCLUDE from Installer

The following files in `bin/` are for development/testing only:

- ❌ `settings.ini` - Contains personal preferences and file paths
- ❌ `.user` files
- ❌ Any personal chart files you don't want to distribute
- ❌ Build artifacts (`.obj`, `.o`, etc.)

### Files to INCLUDE in Installer

Required runtime files:
- ✅ `zodiac.exe` - Main executable
- ✅ Qt DLLs (Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, etc.)
- ✅ `platforms/qwindows.dll` - Qt platform plugin
- ✅ `imageformats/*.dll` - Image format plugins
- ✅ All subdirectories: `astroprocessor/`, `chart/`, `details/`, `fileeditor/`, `fonts/`, `i18n/`, `images/`, `plain/`, `planets/`, `style/`, `swe/`, `text/`
- ✅ Sample user files (celebrity charts in `user/` - these are fine)
- ⚠️ License files (GPL text)

## Step-by-Step: Creating the Installer

### Step 1: Deploy Qt Dependencies

Qt applications need their DLLs deployed. Use Qt's `windeployqt` tool:

```powershell
# Navigate to your Qt installation's bin directory
# Example path (adjust for your Qt version):
cd "C:\Qt\6.10.0\mingw_64\bin"

# Run windeployqt on your executable
.\windeployqt.exe "C:\Users\jbaud\OneDrive\Documents\GitHub\joshb-work-sidereal\bin\zodiac.exe" --release
```

This will automatically copy all required Qt DLLs and plugins to your `bin/` directory.

### Step 2: Update Version Number

Edit the NSIS script to match current version:

File: `nsis/zodiac.nsi`
```nsis
!define VERSION '0.8.1'
```

### Step 3: Review and Customize NSIS Script

The existing script (`nsis/zodiac.nsi`) includes:
- Application files installation
- Font installation (Almagest, DejaVu fonts)
- Start menu shortcuts
- Desktop shortcut
- Uninstaller
- GPL license display

**Optional customizations:**
- Exclude `settings.ini` (already excluded in current script)
- Add more sample chart files
- Include a README or changelog

### Step 4: Compile the Installer

```powershell
# Method 1: Right-click on zodiac.nsi in Windows Explorer
# Select "Compile NSIS Script"

# Method 2: Command line
cd "C:\Users\jbaud\OneDrive\Documents\GitHub\joshb-work-sidereal\nsis"
& "C:\Program Files (x86)\NSIS\makensis.exe" zodiac.nsi
```

This will create: `Zodiac-0.8.1-installer.exe` in the `nsis/` directory.

### Step 5: Test the Installer

**Critical Testing Steps:**
1. Test on a clean Windows VM or test machine WITHOUT Qt installed
2. Install to a non-default location
3. Verify all features work:
   - Application launches
   - Can create new charts
   - Can load sample charts
   - Fonts display correctly
   - All UI elements render properly
4. Test uninstaller
5. Check that shortcuts work

## Common Issues and Solutions

### Issue: Missing DLLs
**Symptom:** Application won't start, shows "missing DLL" error
**Solution:** Run `windeployqt` again, or manually copy missing DLLs

### Issue: Fonts not displaying correctly
**Symptom:** Astrological glyphs show as squares or wrong characters
**Solution:** Verify font installation section in NSIS script, ensure fonts are in `nsis/fonts/`

### Issue: Application starts but features missing
**Symptom:** Charts won't render, data files not found
**Solution:** Check that all subdirectories (swe/, astroprocessor/, etc.) are included in installer

### Issue: Settings not persisting
**Symptom:** User settings reset on each launch
**Solution:** Ensure application has write permissions to user's AppData folder (should be automatic)

## Distribution Best Practices

### For Friends (Non-commercial)

1. **Provide the installer** - Easy, one-click installation
2. **Include a README** with:
   - What the software does
   - Basic usage instructions
   - License information (it's GPL - they can share it)
   - Link to source code (GitHub)
   - How to contact you for support

3. **Optional:** Create a simple website or GitHub releases page

### GPL Compliance Checklist

- [ ] Installer displays GPL license during installation
- [ ] Include LICENSE file in installation directory
- [ ] Provide link to source code (GitHub URL)
- [ ] Include Swiss Ephemeris license (swe/LICENSE)
- [ ] Include attribution for Swiss Ephemeris library
- [ ] Make clear that there is NO WARRANTY (GPL requirement)

### Source Code Availability

Since this is GPL software, you MUST make source available. Options:

1. **Best:** Point users to GitHub repository (already public)
2. Include a text file in installation with GitHub URL
3. Offer to provide source on request (written offer for 3 years)

## Advanced: Automatic Updates

If you want to distribute updates to friends:

1. Host installer on GitHub Releases
2. Include version number in filename: `Zodiac-0.8.1-installer.exe`
3. Maintain a CHANGELOG
4. Consider adding an "About" dialog that shows current version and checks for updates

## Example README.txt for Users

```
ZODIAC SIDEREAL - Astrological Software
Version 0.8.1

ABOUT
=====
Zodiac is an astrological charting application with support for sidereal 
zodiac calculations, harmonics, transits, and more.

LICENSE
=======
This software is licensed under GNU General Public License v3.
You are free to use, copy, modify, and distribute this software.
See LICENSE.txt for full terms.

This program includes Swiss Ephemeris, © Astrodienst AG, used under GPL v2+.

NO WARRANTY
===========
This software comes with ABSOLUTELY NO WARRANTY. See license for details.

SOURCE CODE
===========
Source code is available at:
https://github.com/jbaudhuin/joshb-work-sidereal

SUPPORT
=======
For questions or issues, please contact: [your email]
Or file an issue on GitHub: https://github.com/jbaudhuin/joshb-work-sidereal/issues
```

## Building for Different Configurations

### Debug vs Release
Always distribute **Release** builds:
- Smaller file size
- Better performance
- No debug symbols

### 32-bit vs 64-bit
- Determine your Qt installation type (mingw_64, mingw_32, msvc2019_64, etc.)
- The installer should match your build architecture
- For widest compatibility, consider building both and creating separate installers

## File Size Optimization

Typical installer size: 50-150 MB (includes Qt libraries)

To reduce size:
- Remove unnecessary Qt modules (if not using multimedia, network, etc.)
- Compress with NSIS LZMA compression (already enabled)
- Remove debug symbols (use Release build)
- Don't include unnecessary plugins

## Next Steps After Creating Installer

1. Test thoroughly on multiple machines
2. Create a GitHub Release with:
   - Version tag (v0.8.1)
   - Release notes
   - Installer attachment
   - Source code (automatic)
3. Share link with friends
4. Consider creating a simple website or documentation
5. Set up issue tracking for bug reports
