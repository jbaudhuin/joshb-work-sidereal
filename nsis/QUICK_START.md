# Quick Start - Building Installer

## Automatic Method (Recommended)

Simply run the build script:

```powershell
cd nsis
.\build-installer.ps1
```

The script will:
1. Deploy Qt dependencies to bin/
2. Verify all required files are present
3. Compile the NSIS installer
4. Report the location of the completed installer

## Custom Qt Location

If Qt is not at the default location:

```powershell
.\build-installer.ps1 -QtPath "C:\Path\To\Your\Qt\6.x.x\mingw_64"
```

## Manual Method

If you prefer to do it step-by-step:

### 1. Deploy Qt Dependencies

```powershell
cd "C:\Qt\6.10.0\mingw_64\bin"
.\windeployqt.exe "C:\Users\jbaud\OneDrive\Documents\GitHub\joshb-work-sidereal\bin\zodiac.exe" --release
```

### 2. Compile Installer

```powershell
cd "C:\Users\jbaud\OneDrive\Documents\GitHub\joshb-work-sidereal\nsis"
& "C:\Program Files (x86)\NSIS\makensis.exe" zodiac.nsi
```

### 3. Find Your Installer

The installer will be created in the `nsis/` directory:
- `Zodiac-0.8.1-installer.exe`

## Troubleshooting

### "windeployqt not found"
- Install Qt from https://www.qt.io/
- Specify the correct path with `-QtPath`

### "NSIS not found"
- Install NSIS from https://nsis.sourceforge.io/
- Use default installation location

### "Missing directories in bin/"
- Ensure you have built the project completely
- Copy required data directories from the project

For complete documentation, see INSTALLER_GUIDE.md
