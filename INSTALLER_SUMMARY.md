# Zodiac Sidereal - Installer Creation Summary

## Quick Answer: Yes, You're Ready!

Your project already has installer infrastructure from the previous version. I've updated and enhanced it for version 0.8.1.

## What I Created For You

### 1. Documentation Files
- **`INSTALLER_GUIDE.md`** - Complete guide to creating and distributing installers
- **`GPL_COMPLIANCE.md`** - Legal checklist for GPL license compliance
- **`nsis/QUICK_START.md`** - Quick reference for building installer
- **`nsis/README_FOR_USERS.txt`** - User-facing documentation (included in installer)

### 2. Updated Installer Script
- **`nsis/zodiac.nsi`** - Updated to v0.9.1
  - Added README_FOR_USERS.txt to installation
  - Added explicit comment about settings.ini exclusion
  - Added license.txt to installation directory

### 3. Automation Script
- **`nsis/build-installer.ps1`** - PowerShell script to automate the entire process
  - Deploys Qt dependencies
  - Verifies all required files
  - Compiles NSIS installer
  - Provides helpful feedback

## How to Build Your Installer (Simple Version)

### Prerequisites (One-time Setup)
1. Install NSIS: https://nsis.sourceforge.io/Download
2. Ensure Qt is installed (you already have this)

### Build the Installer (Every Release)

```powershell
# Navigate to nsis directory
cd C:\Users\jbaud\OneDrive\Documents\GitHub\joshb-work-sidereal\nsis

# Run the automated build script
.\build-installer.ps1
```

That's it! The script will create `Zodiac-0.9.1-installer.exe` in the nsis/ directory.

### If Qt is not at default location:

```powershell
.\build-installer.ps1 -QtPath "C:\Path\To\Your\Qt\6.x.x\mingw_64"
```

## What About settings.ini?

✅ **GOOD NEWS:** The installer already excludes settings.ini!

Your `bin/settings.ini` contains:
- Personal file paths
- Your recent chart files
- Window positions
- Personal preferences

**This is intentionally NOT included in the installer.**

When users run the application for the first time:
- A new settings.ini will be created automatically
- With default settings
- In their own user directory

## GPL License Compliance

✅ **GOOD NEWS:** You're already compliant!

Because:
1. Your code is on public GitHub (source available)
2. License files are included in installer
3. README tells users where to get source
4. No restrictions added

See `GPL_COMPLIANCE.md` for complete details.

## File Structure After Installation

When a user installs your application, they get:

```
C:\Program Files\Zodiac\
├── zodiac.exe                    # Main application
├── README_FOR_USERS.txt          # Documentation with GitHub link
├── license.txt                   # GPL v3 license
├── Qt6*.dll                      # Qt libraries
├── astroprocessor/               # Aspect definitions, etc.
├── chart/                        # Chart rendering resources
├── details/                      # Detail view resources
├── fileeditor/                   # File editor resources
├── fonts/                        # Astrological fonts
├── i18n/                         # Translations
├── images/                       # Graphics and icons
├── plain/                        # Plain text view resources
├── planets/                      # Planet view resources
├── platforms/                    # Qt platform plugins
├── style/                        # CSS stylesheets
├── swe/                          # Swiss Ephemeris data & LICENSE
├── text/                         # Interpretation texts
└── user/                         # Sample celebrity charts

# settings.ini will be created here on first run:
C:\Users\[username]\AppData\Local\Zodiac\
└── settings.ini                  # User-specific settings
```

## Testing Your Installer

**Critical:** Test on a machine that doesn't have Qt installed!

### Test Checklist:
- [ ] Install to default location (C:\Program Files\Zodiac\)
- [ ] Install to custom location (D:\MyApps\Zodiac\)
- [ ] Application launches without errors
- [ ] Can create new chart
- [ ] Can load sample charts
- [ ] Fonts display correctly (astrological glyphs)
- [ ] All views work (Chart, Planets, Plain, Details, etc.)
- [ ] Start menu shortcuts work
- [ ] Desktop shortcut works
- [ ] Uninstaller removes all files
- [ ] Uninstaller removes shortcuts

### Testing Environments:
- **Ideal:** Windows 10/11 Virtual Machine without Qt
- **Good:** Friend's computer
- **Minimum:** Your computer after uninstalling existing version

## Distribution Options

### Option 1: GitHub Releases (Recommended)
1. Create release tag: v0.8.1
2. Upload installer to release
3. Write release notes
4. Share link with friends

**Benefits:**
- Professional presentation
- Automatic source code inclusion
- Version history tracking
- Easy updates

### Option 2: Direct File Sharing
1. Email installer to friends
2. Upload to cloud storage (Dropbox, Google Drive, OneDrive)
3. Share download link

**Benefits:**
- Simple and immediate
- No GitHub account needed for recipients

### Option 3: Personal Website
1. Host installer on your website
2. Create simple download page
3. Include link to GitHub for source

**Benefits:**
- Professional appearance
- Full control over presentation

## Common Issues and Solutions

### "Application won't start - missing DLL"
**Cause:** Qt dependencies not deployed
**Solution:** Run `windeployqt` or use the build-installer.ps1 script

### "Fonts don't display - boxes instead of symbols"
**Cause:** Font installation failed
**Solution:** Check that fonts/ directory exists in nsis/ and contains Almagest.ttf, etc.

### "Installer compilation fails"
**Cause:** NSIS can't find files
**Solution:** 
- Check that all File commands in zodiac.nsi point to existing files
- Verify bin/ directory has all required content
- Run from nsis/ directory

### "Settings reset every time I run"
**Cause:** Application can't write to installation directory
**Solution:** This is normal! Settings should be saved to AppData, not Program Files

## Version Management

Current version: **0.9.1**

To update for next release:
1. Update version in `zodiac/src/main.cpp`:
   ```cpp
   a.setApplicationVersion("v0.10.0 (build YYYY-MM-DD)");
   ```

2. Update version in `nsis/zodiac.nsi`:
   ```nsis
   !define VERSION '0.10.0'
   ```

3. Update version in `nsis/README_FOR_USERS.txt`:
   - Header: `Version 0.10.0`
   - Footer version info section

4. Rebuild application
5. Run build-installer.ps1
6. Test new installer

## Size Expectations

Typical installer sizes:
- **With Qt 6:** ~80-150 MB (includes all Qt libraries)
- **Compressed (NSIS LZMA):** ~40-70 MB

This is normal for Qt applications. Users expect this size for desktop software.

## Security Note

**Important:** When you distribute the installer:
- Some antivirus software may flag it as "unknown" or "unsigned"
- This is normal for unsigned executables
- Not a security risk - just lack of code signing certificate

**Optional:** Get a code signing certificate (~$200/year) to:
- Remove antivirus warnings
- Show verified publisher name
- Build trust with users

**Not required** for sharing with friends, but nice for wider distribution.

## Next Steps

1. **Install NSIS** (if not already installed)
2. **Run build-installer.ps1** to create installer
3. **Test installer** on another machine or VM
4. **Share with friends!**

For any issues:
- Check INSTALLER_GUIDE.md for detailed procedures
- Check GPL_COMPLIANCE.md for legal questions
- Check nsis/QUICK_START.md for quick reference

## Boost Libraries - No Problem!

You're using Boost Math (header-only) for numerical algorithms. **This doesn't complicate distribution at all:**

- ✅ **Boost License is permissive** - Compatible with GPL, no restrictions
- ✅ **Header-only** - Compiled into your executable, no DLLs to distribute
- ✅ **No attribution required** - Though already mentioned in README_FOR_USERS.txt
- ✅ **Nothing to install** - Already built into zodiac.exe

The Boost Math functions you use (`boost::math::tools::minima` and `roots`) are compiled directly into your executable during build. Users don't need Boost installed, and there are no extra files to distribute.

## Questions?

Common concerns addressed:

**Q: Will my personal settings be in the installer?**
A: No, settings.ini is excluded.

**Q: Can friends share it with other friends?**
A: Yes! GPL explicitly allows redistribution.

**Q: Do I need to do anything special for GPL?**
A: No, you're already compliant. Just keep GitHub public.

**Q: What if I make changes to the code?**
A: Commit to GitHub, rebuild, run build-installer.ps1 again.

**Q: How often should I create new releases?**
A: Whenever you have significant changes or bug fixes. Or never! It's your software.

## Resources

- NSIS Documentation: https://nsis.sourceforge.io/Docs/
- Qt Deployment Guide: https://doc.qt.io/qt-6/windows-deployment.html
- GPL v3 Text: https://www.gnu.org/licenses/gpl-3.0.html
- This Project: https://github.com/jbaudhuin/joshb-work-sidereal

---

**You're all set!** The infrastructure is in place. Just run the build script and share the installer. Good luck with your distribution!
