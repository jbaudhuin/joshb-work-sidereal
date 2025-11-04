#!/usr/bin/env pwsh
# Cross-platform setup verification script

Write-Host "=== Development Environment Check ===" -ForegroundColor Green

# Check CMake
try {
    $cmakeVersion = & cmake --version 2>$null | Select-Object -First 1
    Write-Host "✓ CMake: $cmakeVersion" -ForegroundColor Green
}
catch {
    Write-Host "✗ CMake not found in PATH" -ForegroundColor Red
    Write-Host "  Install CMake or add to PATH" -ForegroundColor Yellow
}

# Check Ninja
try {
    $ninjaVersion = & ninja --version 2>$null
    Write-Host "✓ Ninja: $ninjaVersion" -ForegroundColor Green
}
catch {
    Write-Host "✗ Ninja not found in PATH" -ForegroundColor Red
    Write-Host "  Install Ninja or add to PATH" -ForegroundColor Yellow
}

# Check Clang
try {
    $clangVersion = & clang --version 2>$null | Select-Object -First 1
    Write-Host "✓ Clang: $clangVersion" -ForegroundColor Green
}
catch {
    Write-Host "✗ Clang not found in PATH" -ForegroundColor Red
    Write-Host "  Install Clang or add to PATH" -ForegroundColor Yellow
}

# Check clang-format
try {
    $formatVersion = & clang-format --version 2>$null | Select-Object -First 1
    Write-Host "✓ clang-format: $formatVersion" -ForegroundColor Green
}
catch {
    Write-Host "✗ clang-format not found in PATH" -ForegroundColor Red
    Write-Host "  Install clang-format or add to PATH" -ForegroundColor Yellow
}

# Check Qt
Write-Host "`n=== Qt Detection ===" -ForegroundColor Green
try {
    # Try to find qmake
    $qmakeVersion = & qmake --version 2>$null | Select-Object -First 1
    if ($qmakeVersion) {
        Write-Host "✓ Qt: $qmakeVersion" -ForegroundColor Green
        $qtPath = & qmake -query QT_INSTALL_PREFIX 2>$null
        Write-Host "  Qt Installation: $qtPath" -ForegroundColor Cyan
    }
    else {
        Write-Host "✗ qmake not found - Qt may not be in PATH" -ForegroundColor Yellow
    }
}
catch {
    Write-Host "✗ Qt not found in PATH" -ForegroundColor Red
    Write-Host "  CMake will try to find Qt automatically" -ForegroundColor Yellow
}

# Test CMake Qt detection
Write-Host "`n=== Testing CMake Qt Detection ===" -ForegroundColor Green
$tempDir = Join-Path $env:TEMP "qt-test-$(Get-Random)"
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

$testCMake = @"
cmake_minimum_required(VERSION 3.16)
project(QtTest)
find_package(Qt6 QUIET COMPONENTS Core)
if(Qt6_FOUND)
    message(STATUS "Qt6 found at: `${Qt6_DIR}")
else()
    message(STATUS "Qt6 not found")
endif()
"@

Set-Content -Path "$tempDir/CMakeLists.txt" -Value $testCMake

try {
    Push-Location $tempDir
    $cmakeResult = & cmake . 2>&1
    if ($cmakeResult -match "Qt6 found at:") {
        Write-Host "✓ CMake can find Qt6 automatically" -ForegroundColor Green
        $qtDir = ($cmakeResult | Select-String "Qt6 found at:" | ForEach-Object { $_.ToString().Split(":")[1].Trim() })
        Write-Host "  Found at: $qtDir" -ForegroundColor Cyan
    }
    else {
        Write-Host "✗ CMake cannot find Qt6 automatically" -ForegroundColor Red
        Write-Host "  You may need to set CMAKE_PREFIX_PATH" -ForegroundColor Yellow
    }
}
catch {
    Write-Host "✗ CMake test failed: $_" -ForegroundColor Red
}
finally {
    Pop-Location
    Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "`n=== Recommendations ===" -ForegroundColor Green
Write-Host "For best cross-platform experience:" -ForegroundColor Cyan
Write-Host "1. Add all tools to system PATH" -ForegroundColor White
Write-Host "2. Restart VSCode after PATH changes" -ForegroundColor White
Write-Host "3. Use CMake Tools extension for building" -ForegroundColor White
Write-Host "4. See DEVELOPER_SETUP.md for detailed instructions" -ForegroundColor White