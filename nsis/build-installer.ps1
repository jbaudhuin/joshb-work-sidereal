# Zodiac Sidereal - Installer Build Script
# This script automates the process of deploying Qt dependencies and building the installer

param(
    [string]$QtPath = "",  # Empty by default - will use PATH
    [string]$NSISPath = "C:\Program Files (x86)\NSIS\makensis.exe",
    [switch]$SkipDeploy,
    [switch]$SkipBuild,
    [switch]$TestMode
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$BinDir = Join-Path $ProjectRoot "bin"
$NSISDir = Join-Path $ProjectRoot "nsis"
$InstallerScript = Join-Path $NSISDir "zodiac.nsi"

# Extract version from NSIS script
$Version = "unknown"
if (Test-Path $InstallerScript) {
    $nsisContent = Get-Content $InstallerScript -Raw
    if ($nsisContent -match "!define VERSION '([^']+)'") {
        $Version = $matches[1]
    }
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Zodiac Sidereal - Installer Build Tool" -ForegroundColor Cyan
Write-Host "Version: $Version" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Function to check if path exists
function Test-PathExists {
    param([string]$Path, [string]$Description)
    
    if (-not (Test-Path $Path)) {
        Write-Host "ERROR: $Description not found at: $Path" -ForegroundColor Red
        return $false
    }
    Write-Host "✓ Found $Description" -ForegroundColor Green
    return $true
}

# Check prerequisites
Write-Host "Checking prerequisites..." -ForegroundColor Yellow
Write-Host ""

$allGood = $true

if (-not (Test-PathExists $BinDir "Binary directory")) { $allGood = $false }

$ZodiacExe = Join-Path $BinDir "zodiac.exe"
if (-not (Test-PathExists $ZodiacExe "zodiac.exe")) { $allGood = $false }

if (-not $SkipDeploy) {
    $WinDeployQt = ""
    try {
        $WinDeployQt = (Get-Command windeployqt.exe -ErrorAction Stop).Source
    }
    catch {
        if ($QtPath) {
            $WinDeployQt = Join-Path $QtPath "bin\windeployqt.exe"
        }
    }
    
    if (-not $WinDeployQt -or -not (Test-Path $WinDeployQt)) {
        Write-Host "ERROR: windeployqt not found" -ForegroundColor Red
        Write-Host "  Try: Ensure Qt's bin directory is in your PATH, or specify -QtPath parameter" -ForegroundColor Yellow
        $allGood = false
    } else {
        Write-Host "✓ Found windeployqt: $WinDeployQt" -ForegroundColor Green
    }
}

if (-not (Test-PathExists $NSISPath "NSIS compiler")) {
    Write-Host "  Try: Install NSIS from https://nsis.sourceforge.io/" -ForegroundColor Yellow
    $allGood = $false
}

if (-not (Test-PathExists $InstallerScript "NSIS installer script")) { $allGood = $false }

if (-not $allGood) {
    Write-Host ""
    Write-Host "Cannot proceed due to missing prerequisites." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "All prerequisites found!" -ForegroundColor Green
Write-Host ""

# Step 1: Deploy Qt dependencies
if (-not $SkipDeploy) {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Step 1: Deploying Qt Dependencies" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    # Check if Qt DLLs already exist
    $qtDlls = Get-ChildItem -Path $BinDir -Filter "Qt*.dll" -ErrorAction SilentlyContinue
    if ($qtDlls) {
        # Check Qt DLL version
        $qtCoreDll = Join-Path $BinDir "Qt6Core.dll"
        if (Test-Path $qtCoreDll) {
            $dllVersion = (Get-Item $qtCoreDll).VersionInfo.FileVersion
            Write-Host "Found Qt DLLs in bin/ (version: $dllVersion)" -ForegroundColor Cyan
            
            # Try to detect the Qt version from PATH
            try {
                $windeployqtPath = (Get-Command windeployqt.exe -ErrorAction Stop).Source
                $qtBinDir = Split-Path $windeployqtPath -Parent
                $qtDir = Split-Path $qtBinDir -Parent
                
                # Extract version from path (e.g., C:\Qt\6.10.1\...)
                if ($qtDir -match '\\(\d+\.\d+\.\d+)\\') {
                    $expectedVersion = $matches[1]
                    
                    if (-not $dllVersion.StartsWith($expectedVersion)) {
                        Write-Host "⚠ WARNING: Qt version mismatch detected!" -ForegroundColor Yellow
                        Write-Host "  Deployed DLLs: $dllVersion" -ForegroundColor Yellow
                        Write-Host "  Expected version: $expectedVersion (from PATH)" -ForegroundColor Yellow
                        Write-Host ""
                        $response = Read-Host "Clean and re-deploy all Qt dependencies? (y/n)"
                        if ($response -eq 'y') {
                            Write-Host "Cleaning old Qt deployment..." -ForegroundColor Yellow
                            # Remove Qt DLLs
                            Remove-Item (Join-Path $BinDir "Qt*.dll") -Force -ErrorAction SilentlyContinue
                            # Remove Qt plugin directories
                            $pluginDirs = @("platforms", "styles", "imageformats", "iconengines", "generic", "networkinformation", "tls", "platforminputcontexts", "qmltooling", "vectorimageformats")
                            foreach ($dir in $pluginDirs) {
                                $dirPath = Join-Path $BinDir $dir
                                if (Test-Path $dirPath) {
                                    Remove-Item $dirPath -Recurse -Force -ErrorAction SilentlyContinue
                                }
                            }
                            # Remove QML directories
                            $qmlPath = Join-Path $BinDir "qml"
                            if (Test-Path $qmlPath) {
                                Remove-Item $qmlPath -Recurse -Force -ErrorAction SilentlyContinue
                            }
                            $qtDlls = $null
                            Write-Host "✓ Cleaned old Qt deployment" -ForegroundColor Green
                        }
                    } else {
                        Write-Host "✓ Qt DLL version matches PATH version" -ForegroundColor Green
                        Write-Host "  Skipping windeployqt" -ForegroundColor Gray
                    }
                }
            }
            catch {
                Write-Host "  Could not verify version match" -ForegroundColor Gray
                Write-Host "  Skipping windeployqt" -ForegroundColor Gray
            }
        }
    }
    
    if (-not $qtDlls) {
        Write-Host "Deploying Qt dependencies via windeployqt..." -ForegroundColor Yellow
        
        # Try to find windeployqt
        $WinDeployQt = ""
        
        # First, try to use windeployqt from PATH
        try {
            $WinDeployQt = (Get-Command windeployqt.exe -ErrorAction Stop).Source
            Write-Host "Using windeployqt from PATH: $WinDeployQt" -ForegroundColor Green
        }
        catch {
            # If not in PATH, try the QtPath parameter
            if ($QtPath -and (Test-Path $QtPath)) {
                $WinDeployQt = Join-Path $QtPath "bin\windeployqt.exe"
                if (-not (Test-Path $WinDeployQt)) {
                    Write-Host "⚠ WARNING: windeployqt not found" -ForegroundColor Yellow
                    $WinDeployQt = ""
                }
            }
        }
        
        if ($WinDeployQt) {
            try {
                & $WinDeployQt $ZodiacExe --release --no-translations
                Write-Host "✓ Qt dependencies deployed successfully" -ForegroundColor Green
                
                # Check if main Qt DLLs were copied (windeployqt sometimes misses them with QML apps)
                $qtCoreDll = Join-Path $BinDir "Qt6Core.dll"
                if (-not (Test-Path $qtCoreDll)) {
                    Write-Host "  Note: Main Qt DLLs not found, copying required DLLs..." -ForegroundColor Yellow
                    
                    # Get the Qt bin directory from windeployqt path
                    $qtBinDir = Split-Path $WinDeployQt -Parent
                    
                    # Copy only the essential Qt DLLs that the app needs
                    $requiredQtDlls = @(
                        "Qt6Concurrent.dll",
                        "Qt6Core.dll",
                        "Qt6Core5Compat.dll",
                        "Qt6Gui.dll",
                        "Qt6Network.dll",
                        "Qt6OpenGL.dll",
                        "Qt6Qml.dll",
                        "Qt6QmlMeta.dll",
                        "Qt6QmlModels.dll",
                        "Qt6QmlWorkerScript.dll",
                        "Qt6Quick.dll",
                        "Qt6Widgets.dll"
                    )
                    
                    foreach ($dll in $requiredQtDlls) {
                        $sourceDll = Join-Path $qtBinDir $dll
                        $destDll = Join-Path $BinDir $dll
                        if (Test-Path $sourceDll) {
                            Copy-Item $sourceDll $destDll -Force
                        } else {
                            Write-Host "    Warning: $dll not found in Qt installation" -ForegroundColor Yellow
                        }
                    }
                    
                    Write-Host "  ✓ Required Qt DLLs copied" -ForegroundColor Green
                    
                    # Copy OpenSSL DLLs (required for HTTPS/TLS support)
                    $openSslPath = "C:\Qt\Tools\OpenSSLv3\Win_x64\bin"
                    if (Test-Path $openSslPath) {
                        Write-Host "  Copying OpenSSL DLLs for TLS support..." -ForegroundColor Yellow
                        $openSslDlls = @(
                            "libssl-3-x64.dll",
                            "libcrypto-3-x64.dll"
                        )
                        foreach ($dll in $openSslDlls) {
                            $sourceDll = Join-Path $openSslPath $dll
                            $destDll = Join-Path $BinDir $dll
                            if (Test-Path $sourceDll) {
                                Copy-Item $sourceDll $destDll -Force
                            } else {
                                Write-Host "    Warning: $dll not found in OpenSSL directory" -ForegroundColor Yellow
                            }
                        }
                        Write-Host "  ✓ OpenSSL DLLs copied" -ForegroundColor Green
                    } else {
                        Write-Host "  ⚠ WARNING: OpenSSL not found at $openSslPath" -ForegroundColor Yellow
                        Write-Host "    HTTPS/TLS features (like Google Maps API) will not work!" -ForegroundColor Yellow
                    }
                    
                    # Copy Qt plugins (required for application to function)
                    $qtDir = Split-Path $qtBinDir -Parent
                    $qtPluginsPath = Join-Path $qtDir "plugins"
                    if (Test-Path $qtPluginsPath) {
                        Write-Host "  Copying Qt plugins..." -ForegroundColor Yellow
                        $pluginDirs = @("platforms", "styles", "imageformats", "iconengines", "generic", "tls")
                        foreach ($pluginDir in $pluginDirs) {
                            $sourcePluginDir = Join-Path $qtPluginsPath $pluginDir
                            $destPluginDir = Join-Path $BinDir $pluginDir
                            if (Test-Path $sourcePluginDir) {
                                Copy-Item $sourcePluginDir $destPluginDir -Recurse -Force
                            } else {
                                Write-Host "    Warning: Plugin directory $pluginDir not found" -ForegroundColor Yellow
                            }
                        }
                        Write-Host "  ✓ Qt plugins copied" -ForegroundColor Green
                    } else {
                        Write-Host "  ⚠ WARNING: Qt plugins directory not found at $qtPluginsPath" -ForegroundColor Yellow
                    }
                }
            }
            catch {
                Write-Host "⚠ WARNING: Failed to deploy Qt dependencies" -ForegroundColor Yellow
                Write-Host $_.Exception.Message -ForegroundColor Yellow
                Write-Host ""
                Write-Host "Continuing with installer build, but it may not work on other systems." -ForegroundColor Yellow
            }
        } else {
            Write-Host "⚠ WARNING: Could not find windeployqt" -ForegroundColor Yellow
            Write-Host "  Make sure Qt's bin directory is in your PATH" -ForegroundColor Yellow
            Write-Host ""
            Write-Host "Continuing with installer build, but it may not work on other systems." -ForegroundColor Yellow
        }
    }
    
    Write-Host ""
} else {
    Write-Host "Skipping Qt deployment (--SkipDeploy specified)" -ForegroundColor Yellow
    Write-Host ""
}

# Step 2: Verify required files
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Step 2: Verifying Required Files" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$RequiredDirs = @(
    "astroprocessor",
    "chart",
    "details",
    "fileeditor",
    "fonts",
    "i18n",
    "images",
    "plain",
    "planets",
    "platforms",
    "style",
    "swe",
    "text",
    "sampleCharts"
)

$missingDirs = @()
foreach ($dir in $RequiredDirs) {
    $fullPath = Join-Path $BinDir $dir
    if (Test-Path $fullPath) {
        Write-Host "✓ $dir" -ForegroundColor Green
    } else {
        Write-Host "✗ $dir (MISSING!)" -ForegroundColor Red
        $missingDirs += $dir
    }
}

if ($missingDirs.Count -gt 0) {
    Write-Host ""
    Write-Host "ERROR: Some required directories are missing from bin/" -ForegroundColor Red
    Write-Host "Missing: $($missingDirs -join ', ')" -ForegroundColor Red
    Write-Host "Please ensure you have a complete build before creating installer." -ForegroundColor Yellow
    exit 1
}

Write-Host ""
Write-Host "All required directories present!" -ForegroundColor Green
Write-Host ""

# Step 3: Check for files that should NOT be in installer
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Step 3: Checking for Personal Data" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$SettingsIni = Join-Path $BinDir "settings.ini"
if (Test-Path $SettingsIni) {
    Write-Host "⚠ WARNING: settings.ini found in bin/" -ForegroundColor Yellow
    Write-Host "  This file contains personal preferences and will NOT be included in installer." -ForegroundColor Yellow
    Write-Host "  (This is correct behavior - just informing you)" -ForegroundColor Gray
} else {
    Write-Host "✓ No settings.ini found (good - it will be created on first run)" -ForegroundColor Green
}

Write-Host ""

# Step 4: Build installer
if (-not $SkipBuild) {
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Step 4: Building Installer" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""
    
    Write-Host "Compiling NSIS installer script..." -ForegroundColor Yellow
    Write-Host "Script: $InstallerScript" -ForegroundColor Gray
    Write-Host ""
    
    try {
        # Change to NSIS directory so relative paths work
        Push-Location $NSISDir
        
        $output = & $NSISPath $InstallerScript 2>&1
        $output | ForEach-Object { Write-Host $_ }
        
        Pop-Location
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host ""
            Write-Host "✓ Installer built successfully!" -ForegroundColor Green
            
            # Find the installer file
            $installerPattern = "Zodiac-$Version-installer.exe"
            $installer = Get-ChildItem -Path $NSISDir -Filter $installerPattern | Select-Object -First 1
            
            if ($installer) {
                Write-Host ""
                Write-Host "Installer created:" -ForegroundColor Cyan
                Write-Host "  File: $($installer.Name)" -ForegroundColor White
                Write-Host "  Version: $Version" -ForegroundColor White
                Write-Host "  Location: $($installer.DirectoryName)" -ForegroundColor Gray
                Write-Host "  Size: $([math]::Round($installer.Length / 1MB, 2)) MB" -ForegroundColor Gray
            }

            # Archive the debug symbols (PDB) alongside the installer. The in-app
            # crash handler writes minidumps that can only be symbolized with the
            # exact PDB from that build (matched by GUID), so a copy is kept for
            # every shipped installer. Stored in a per-version subfolder keeping
            # the original "zodiac.pdb" name, so a debugger auto-resolves it when
            # symbols\<version> is on the symbol path (and versions never clobber).
            $PdbSource = Join-Path $BinDir "zodiac.pdb"
            if (Test-Path $PdbSource) {
                $PdbDestDir = Join-Path (Join-Path $NSISDir "symbols") $Version
                New-Item -ItemType Directory -Force -Path $PdbDestDir | Out-Null
                $PdbDest = Join-Path $PdbDestDir "zodiac.pdb"
                Copy-Item $PdbSource $PdbDest -Force
                Write-Host ""
                Write-Host "Debug symbols archived:" -ForegroundColor Cyan
                Write-Host "  File: $PdbDest" -ForegroundColor White
                Write-Host "  Size: $([math]::Round((Get-Item $PdbDest).Length / 1MB, 2)) MB" -ForegroundColor Gray
                Write-Host "  (Add symbols\$Version to your debugger's symbol path to load it.)" -ForegroundColor Gray
            } else {
                Write-Host ""
                Write-Host "WARNING: zodiac.pdb not found in bin/" -ForegroundColor Yellow
                Write-Host "  Crash reports from this build will not be symbolizable." -ForegroundColor Yellow
                Write-Host "  Build Release with the PDB-enabled flags (see CMakeLists.txt) to produce it." -ForegroundColor Yellow
            }
        } else {
            Write-Host ""
            Write-Host "ERROR: Installer compilation failed" -ForegroundColor Red
            exit 1
        }
    }
    catch {
        Pop-Location
        Write-Host "ERROR: Failed to build installer" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "Skipping installer build (--SkipBuild specified)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Process Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

if (-not $SkipBuild) {
    Write-Host "Next steps:" -ForegroundColor Yellow
    Write-Host "1. Test the installer on a clean machine or VM" -ForegroundColor White
    Write-Host "2. Verify all features work after installation" -ForegroundColor White
    Write-Host "3. Test the uninstaller" -ForegroundColor White
    Write-Host "4. Distribute to your friends!" -ForegroundColor White
    Write-Host ""
    Write-Host "For detailed testing procedures, see INSTALLER_GUIDE.md" -ForegroundColor Gray
}
