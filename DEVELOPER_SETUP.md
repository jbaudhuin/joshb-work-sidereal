# Cross-Platform Developer Setup Instructions

## The Real Solution: Environment Setup

To make this project truly cross-platform, each developer needs to set up their environment properly. Here's how:

### Windows Developers

1. **Install Qt with Tools**:
   - Download Qt installer from qt.io
   - Install Qt 6.10+ with "llvm-mingw_64" kit
   - This includes CMake, Ninja, and Clang

2. **Add to System PATH** (required for cross-platform tasks):
   ```cmd
   # Add these to your system PATH environment variable:
   C:\Qt\Tools\CMake_64\bin
   C:\Qt\Tools\Ninja  
   C:\Qt\Tools\llvm-mingw1706_64\bin
   C:\Qt\6.10.0\llvm-mingw_64\bin
   ```

3. **Alternative: Use Qt Creator's environment**:
   - Open a "Qt Creator command prompt" from Start Menu
   - Launch `code .` from that prompt to inherit Qt paths

### Linux/WSL Developers

1. **Install system packages**:
   ```bash
   # Ubuntu/Debian
   sudo apt install cmake ninja-build clang qt6-base-dev qt6-webengine-dev
   
   # Fedora
   sudo dnf install cmake ninja-build clang qt6-qtbase-devel qt6-qtwebengine-devel
   ```

2. **Verify tools are in PATH**:
   ```bash
   which cmake ninja clang clang-format
   ```

### macOS Developers

1. **Install via Homebrew**:
   ```bash
   brew install cmake ninja llvm qt6
   echo 'export PATH="/opt/homebrew/opt/llvm/bin:$PATH"' >> ~/.zshrc
   ```

## Why This Approach Works

1. **VSCode CMake Tools extension** automatically detects CMake when it's in PATH
2. **CMake find_package(Qt6)** automatically finds Qt when properly installed  
3. **clangd extension** uses system clang-format when available
4. **Tasks use generic commands** instead of absolute paths

## Verification

After setup, verify everything works:

```bash
# These should all work:
cmake --version
ninja --version  
clang --version
clang-format --version

# Qt should be findable:
cmake -DCMAKE_PREFIX_PATH=$(qmake -query QT_INSTALL_PREFIX) ..
```

## Fallback for Current Session

If you can't modify system PATH, use these VSCode user settings:

**On Windows (adjust paths to your Qt installation):**
```json
{
    "cmake.cmakePath": "C:/Qt/Tools/CMake_64/bin/cmake.exe",
    "cmake.configureEnvironment": {
        "PATH": "C:/Qt/Tools/Ninja;C:/Qt/6.10.0/llvm-mingw_64/bin;${env:PATH}"
    }
}
```

**On Linux/macOS:**
```json
{
    "cmake.cmakePath": "/usr/bin/cmake"
}
```