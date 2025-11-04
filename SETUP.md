# Development Environment Setup

This project uses CMake for cross-platform building and requires Qt6 for the GUI.

## Prerequisites

### Required Tools
- **CMake** (3.16+)
- **Ninja** (build system)
- **Clang** (C++ compiler)
- **Qt6** (GUI framework)

### Platform-Specific Setup

#### Windows
1. **Qt Installation**: 
   - Install Qt 6.10+ with LLVM MinGW kit from [Qt website](https://www.qt.io/download)
   - Or use vcpkg: `vcpkg install qt6-base qt6-widgets qt6-webenginewidgets`

2. **Build Tools**:
   - Qt installer includes CMake, Ninja, and Clang
   - Or install manually: Visual Studio Build Tools + LLVM

3. **Environment**:
   - Ensure `cmake`, `ninja`, and `clang` are in PATH
   - Qt should be auto-detected by CMake

#### Linux/WSL
1. **System Dependencies**:
   ```bash
   # Ubuntu/Debian
   sudo apt update
   sudo apt install cmake ninja-build clang qt6-base-dev qt6-webengine-dev
   
   # Fedora
   sudo dnf install cmake ninja-build clang qt6-qtbase-devel qt6-qtwebengine-devel
   ```

2. **Swiss Ephemeris**:
   ```bash
   # Install development libraries if available
   sudo apt install libswe-dev  # Ubuntu (if available)
   ```

#### macOS
1. **Homebrew**:
   ```bash
   brew install cmake ninja llvm qt6
   ```

2. **Xcode Command Line Tools**:
   ```bash
   xcode-select --install
   ```

## Building

### VSCode (Recommended)
1. Open project in VSCode
2. Install recommended extensions (CMake Tools, clangd)
3. Use `Ctrl+Shift+P` → "CMake: Configure" 
4. Use `Ctrl+Shift+P` → "CMake: Build"
5. Debug with `F5`

### Command Line
```bash
# Configure
cmake -S zodiac -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --parallel

# Run (Windows)
cd bin && ./zodiac.exe

# Run (Linux/macOS)
cd bin && ./zodiac
```

## Troubleshooting

### Qt Not Found
- **Windows**: Ensure Qt installation path is correct
- **Linux**: Install qt6-base-dev package
- **All**: Set `CMAKE_PREFIX_PATH` to Qt installation directory

### Missing Swiss Ephemeris
The project includes Swiss Ephemeris source code, so no external installation needed.

### Runtime Issues
- **Windows**: Qt DLLs must be in PATH or bundled
- **Linux**: Install Qt6 runtime packages
- **All**: Use `windeployqt` (Windows) or equivalent for deployment