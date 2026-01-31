---
name: hise-setup
description: Setup this computer to work with HISE.
---

# HISE Development Environment Setup Agent

## Overview
Automates complete development environment setup for HISE (Hart Instrument Software Environment) on Windows, macOS, or Linux.

## Supported Platforms
- Windows 7+ (64-bit recommended)
- macOS 10.7+ (10.13+ for development)
- Linux (Ubuntu 16.04 LTS tested)

---

## Core Capabilities

### 1. Platform Detection
- Automatically detects operating system
- Verifies OS version compatibility
- Adjusts setup workflow based on platform
- Provides platform-specific guidance

### 2. Test Mode Operation
- **Test Mode:** Validates environment without making changes
  - **Executes:** All non-modifying commands (read-only operations)
    - `git --version` (check if Git is installed)
    - `xcode-select -p` (check Xcode path)
    - `which gcc` / `gcc --version` (check compiler)
    - `ls` / `test -f` / `test -d` (check file/directory existence)
    - Platform detection and OS version checks
    - Disk space checks
  - **Skips:** All modifying commands (shows what would be executed)
    - Installations (`apt-get install`, `brew install`, etc.)
    - Git operations that change state (`git clone`, `git checkout`, etc.)
    - File modifications (unzip, write, delete)
    - Build commands (MSBuild, xcodebuild, make)
    - PATH modifications
  - Output format for skipped commands: `[TEST MODE] Would execute: {command}`
  - Used for debugging setup workflow and validating prerequisites
- **Normal Mode:** Executes full installation

### 3. Git Installation & Repository Setup
- Checks for Git installation
- Installs Git if missing (platform-specific method)
- Clones HISE repository from **develop branch** (default)
- Initializes and updates JUCE submodule
- Uses **juce6 branch only** (stable version)

### 4. IDE & Compiler Installation

**Windows:**
- Detects Visual Studio installation (**VS2026 preferred, default**)
- Installs VS2026 Community with "Desktop development with C++" workload
- Verifies MSBuild availability

**macOS:**
- Detects Xcode installation
- Installs Xcode with Command Line Tools
- Handles Gatekeeper permission issues

**Linux:**
- Installs dependencies: build-essential, LLVM/Clang, and GUI libraries
- Ensures GCC/G++ ≤11 (aborts if newer version detected)
- Optionally installs mold linker for faster builds

### 5. SDK & Tool Installation

**Required SDKs:**
- Extracts and configures ASIO SDK 2.3 (Windows: low-latency audio)
- Extracts and configures VST3 SDK (all platforms)

**Optional (User Prompted):**
- **Intel IPP oneAPI (Windows only):** Performance optimization
  - User prompted before installation
  - Provides option to build without IPP
  - Configures Projucer setting accordingly
- **Faust DSP programming language**
  - User prompted before installation
  - macOS: architecture-specific (x64/arm64)
  - Windows: installs to C:\Program Files\Faust\

### 6. HISE Compilation
- Compiles HISE from `projects/standalone/HISE Standalone.jucer`
- Uses Projucer (from JUCE submodule) to generate IDE project files
- Compiles HISE:
  - Windows: Visual Studio 2026 (Release)
  - macOS: Xcode (Release) using **xcbeautify** for output formatting
  - Linux: Makefile (Release)
- **Aborts on non-trivial build failures**

### 7. Environment Setup
- Adds compiled HISE binary to PATH environment variable
- **macOS only:** Displays code signing certificate setup instructions
- Verifies HISE is callable from command line

### 8. Verification
- Runs `HISE --help` to display available CLI commands
- Compiles test project from `extras/demo_project/`
- Confirms system is ready for HISE development

---

## Verified Download URLs

### Windows
- **Visual Studio 2026 Community:** https://visualstudio.microsoft.com/downloads/
  - Download: "Visual Studio Community 2026" (Web Installer)
  - Workload: "Desktop development with C++"
- **Intel IPP oneAPI 2021.11.0.533:** https://registrationcenter-download.intel.com/akdlm/IRC_NAS/b4adec02-353b-4144-aa21-f2087040f316/w_ipp_oneapi_p_2021.11.0.533.exe
- **ASIO SDK 2.3:** https://www.steinberg.net/de/company/developer.html

### macOS
- **Xcode:** https://developer.apple.com/xcode/
  - Available from Mac App Store

### Linux
- Dependencies installed via `apt-get` from Ubuntu repositories

---

## Required Tools & Paths

### Windows Build Tools
1. **MSBuild** (from Visual Studio 2026)
   - Path: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe`

2. **Projucer** (from JUCE submodule)
   - Path: `{hisePath}/JUCE/Projucer/Projucer.exe`

3. **Visual Studio Solution**
   - Path: `{buildPath}/Builds/VisualStudio2026/{project}.sln`

### Linux Build Tools
1. **Projucer** (from JUCE submodule)
   - Path: `{hisePath}/JUCE/Projucer/Projucer`

2. **Make** (via build-essential)
   - Path: `{buildPath}/Builds/LinuxMakefile/`

3. **GCC/Clang** toolchain

### macOS Build Tools
1. **Projucer** (from JUCE submodule)
   - Path: `{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer`

2. **xcodebuild** (from Xcode)
   - Path: `{buildPath}/Builds/MacOSX/{project}.xcodeproj`

3. **xcbeautify** (output beautifier)
   - Path: `{hisePath}/tools/Projucer/xcbeautify`

---

## HISE CLI Commands (verified from Main.cpp)

**Available Commands:**
- `export` - builds project using default settings
- `export_ci` - builds project using customized behaviour for automated builds
- `full_exp` - exports project as Full Instrument Expansion (HISE Player Library)
- `compress_samples` - collects all HLAC files into a hr1 archive
- `set_project_folder` - changes current project folder
- `set_hise_folder` - sets HISE source code folder location
- `get_project_folder` - returns current project folder
- `set_version` - sets project version number
- `clean` - cleans Binaries folder
- `create-win-installer` - creates Inno Setup installer script
- `create-docs` - creates HISE documentation files
- `load` - loads given file and returns status code
- `compile_script` - compiles script file and returns Console output as JSON
- `compile_networks` - compiles DSP networks in given project folder
- `create_builder_cache` - creates cache file for Builder AI agent
- `run_unit_tests` - runs unit tests (requires CI build)

**Usage:**
```bash
HISE COMMAND [FILE] [OPTIONS]
```

**Arguments:**
- `-h:{TEXT}` - sets HISE path
- `-ipp` - enables Intel Performance Primitives
- `-l` - compile for legacy CPU models
- `-t:{TEXT}` - sets project type ('standalone' | 'instrument' | 'effect' | 'midi')
- `-p:{TEXT}` - sets plugin type ('VST' | 'AU' | 'VST_AU' | 'AAX' | 'ALL' | 'VST2' | 'VST3' | 'VST23AU')
- `--help` - displays this help message

---

## Automated Workflow

### Step 0: Installation Location Selection

**Default Installation Paths:**
- **Windows:** `C:\HISE`
- **macOS:** `~/HISE`
- **Linux:** `~/HISE`

**Workflow:**
1. Detect current working directory
2. Compare with platform-specific default path
3. If different from default, prompt user:

```
Current directory: {current_path}
Default HISE installation path: {default_path}

Where would you like to install HISE?
1. Install here ({current_path})
2. Install in default location ({default_path})
3. Specify custom path
```

**Test Mode:**
```
[TEST MODE] Step 0: Installation Location Selection
[TEST MODE] Current directory: {current_path}
[TEST MODE] Default installation path: {default_path}
[TEST MODE] Would prompt user for installation location choice
[TEST MODE] Selected installation path: {selected_path}
```

**Normal Mode:**
- If current directory equals default path: proceed without prompting
- If current directory differs: display prompt and wait for user selection
- Store selected path as `{hisePath}` for all subsequent steps
- **High-level log:** "Determining HISE installation location..."

---

### Step 1: Test Mode Detection
```
Prompt: "Run in test mode? (y/n)"

If test mode:
  - Detect OS platform (Windows/macOS/Linux)
  - Verify OS version compatibility
  - Skip all installations
  - Display exact commands for each subsequent step
  - Output format: [TEST MODE] Would execute: {command}
  - No system modifications

If normal mode:
  - Proceed with full installation
  - Execute all steps as documented below
```

- **Test mode example output:**
  ```
  [TEST MODE] Platform detected: Windows
  [TEST MODE] OS version: Windows 11 - COMPATIBLE
  [TEST MODE] Would execute: git clone https://github.com/christophhart/HISE.git
  ```

### Step 2: System Check & Platform Detection
- Verify OS compatibility
- Check disk space requirements (~2-5 GB)
- Detect existing installations

**Test Mode:**
```
[TEST MODE] Platform detected: {Windows/macOS/Linux}
[TEST MODE] OS version: {version} - {COMPATIBLE/NOT COMPATIBLE}
[TEST MODE] Disk space check: {X GB available} - SUFFICIENT
```

**Normal Mode:**
- **High-level log:** "Detecting platform and checking system requirements..."

### Step 3: Git Setup
- Verify Git availability
- Install if needed (automatic)
- Clone repository:

**Test Mode:**
```
[TEST MODE] Step 3: Git Setup
[TEST MODE] Executing: git --version
[TEST MODE] Result: git version X.X.X (or "not installed")
[TEST MODE] If not installed, would execute: {platform-specific git install command}
[TEST MODE] Would execute: git clone https://github.com/christophhart/HISE.git
[TEST MODE] Would execute: cd HISE
[TEST MODE] Would execute: git checkout develop
[TEST MODE] Would execute: git submodule update --init
[TEST MODE] Would execute: cd JUCE && git checkout juce6 && cd ..
```

**Normal Mode:**
```bash
git clone https://github.com/christophhart/HISE.git
cd HISE
git checkout develop
git submodule update --init
cd JUCE && git checkout juce6 && cd ..
```

- **High-level log:** "Installing Git and cloning HISE repository from develop branch..."

### Step 4: User Prompts (Before Installation)

**Windows only:**
- **Prompt:** "Install Intel IPP oneAPI for performance optimization? (Recommended) [Y/n]"
  - Yes: Download and install from verified URL
  - No: Configure Projucer to build without IPP

**All platforms:**
- **Prompt:** "Install Faust DSP programming language? [Y/n]"
  - Yes: Install based on platform/architecture, use `ReleaseWithFaust` build configuration
  - No: Skip Faust installation, use `Release` build configuration

### Step 5: IDE & Compiler Setup

**Windows:**
- Download/install Visual Studio 2026 Community (automatic)
- Select "Desktop development with C++" workload (automatic)
- Verify MSBuild availability at `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe`

**Test Mode (Windows):**
```
[TEST MODE] Step 5: IDE & Compiler Setup
[TEST MODE] Executing: where msbuild (checking for Visual Studio)
[TEST MODE] Executing: test -f "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe"
[TEST MODE] Result: Visual Studio 2026 {installed/not installed}
[TEST MODE] If not installed, would execute: Start installer from https://visualstudio.microsoft.com/downloads/
[TEST MODE] Would select: "Desktop development with C++" workload
```

**macOS:**
- Install Xcode from App Store (automatic)
- Install Command Line Tools (automatic)
- Accept Xcode license (automatic)

**Test Mode (macOS):**
```
[TEST MODE] Step 5: IDE & Compiler Setup
[TEST MODE] Executing: xcode-select -p (checking Xcode path)
[TEST MODE] Executing: which xcodebuild
[TEST MODE] Executing: xcodebuild -version
[TEST MODE] Result: Xcode {version} {installed/not installed}
[TEST MODE] If not installed, would execute: xcode-select --install
[TEST MODE] Would execute: Accept Xcode license automatically
```

**Linux:**
- Install dependencies (automatic)

**Test Mode (Linux):**
```
[TEST MODE] Step 5: IDE & Compiler Setup
[TEST MODE] Executing: which gcc
[TEST MODE] Executing: gcc --version
[TEST MODE] Executing: which clang
[TEST MODE] Executing: clang --version
[TEST MODE] Result: GCC version {X.X} {≤11 OK / >11 ABORT}
[TEST MODE] Would execute: sudo apt-get -y install build-essential make llvm clang libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev mesa-common-dev libasound2-dev freeglut3-dev libxcomposite-dev libcurl4-gnutls-dev libgtk-3-dev libjack-jackd2-dev libwebkit2gtk-4.0-dev libpthread-stubs0-dev ladspa-sdk
```

**Normal Mode:**
- **High-level log:** "Installing IDE and compiler tools..."

### Step 6: SDK Installation
- Extract tools/SDK/sdk.zip to tools/SDK/ (automatic)
- Verify structure:
  - tools/SDK/ASIOSDK2.3/
  - tools/SDK/VST3 SDK/

**Test Mode:**
```
[TEST MODE] Step 6: SDK Installation
[TEST MODE] Executing: test -f tools/SDK/sdk.zip (checking SDK archive exists)
[TEST MODE] Result: SDK archive {found/not found}
[TEST MODE] Executing: test -d tools/SDK/ASIOSDK2.3 (checking if already extracted)
[TEST MODE] Executing: test -d "tools/SDK/VST3 SDK" (checking if already extracted)
[TEST MODE] Result: SDKs {already extracted/need extraction}
[TEST MODE] Would execute: unzip tools/SDK/sdk.zip -d tools/SDK/
```

**Normal Mode:**
- **High-level log:** "Extracting and configuring required SDKs..."

### Step 7: JUCE Submodule Verification
- Verify JUCE submodule is on **juce6 branch** (configured in Step 2)
- Validate JUCE structure is complete

**Test Mode:**
```
[TEST MODE] Step 7: JUCE Submodule Verification
[TEST MODE] Executing: test -d JUCE (checking JUCE directory exists)
[TEST MODE] Executing: git -C JUCE branch --show-current (checking current branch)
[TEST MODE] Executing: test -f JUCE/modules/juce_core/system/juce_StandardHeader.h
[TEST MODE] Result: JUCE submodule {valid on juce6/needs initialization}
```

**Normal Mode:**
- **High-level log:** "Verifying JUCE submodule configuration..."

### Step 8: Compile HISE Standalone Application

**Build Configuration Selection:**
- If Faust was installed (Step 4): Use `ReleaseWithFaust` configuration
- If Faust was not installed: Use `Release` configuration

**Windows:**
- Launch Projucer: `{hisePath}/JUCE/Projucer/Projucer.exe`
- Load project: `projects/standalone/HISE Standalone.jucer`
- Save project to generate IDE files
- Build using MSBuild

**Test Mode (Windows):**
```
[TEST MODE] Step 8: Compile HISE Standalone Application
[TEST MODE] Build configuration: {Release|"Release with Faust"} (based on Faust installation)
[TEST MODE] Executing: test -f "{hisePath}\JUCE\Projucer\Projucer.exe" (checking Projucer exists)
[TEST MODE] Would execute: "{hisePath}\JUCE\Projucer\Projucer.exe" --resave "projects\standalone\HISE Standalone.jucer"
[TEST MODE] Would execute: "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" "projects\standalone\Builds\VisualStudio2026\HISE.sln" /p:Configuration={Release|"Release with Faust"} /verbosity:minimal
[TEST MODE] Output path: projects\standalone\Builds\VisualStudio2026\x64\{Release|Release with Faust}\App\HISE.exe
```

**Normal Mode (Windows - without Faust):**
```batch
cd projects\standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration=Release /verbosity:minimal
```

**Normal Mode (Windows - with Faust):**
```batch
cd projects\standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration="Release with Faust" /verbosity:minimal
```

**macOS:**
- Launch Projucer: `{hisePath}/JUCE/Projucer/Projucer.app`
- Load project: `projects/standalone/HISE Standalone.jucer`
- Save project to generate IDE files
- Build using xcodebuild

**Test Mode (macOS):**
```
[TEST MODE] Step 8: Compile HISE Standalone Application
[TEST MODE] Build configuration: {Release|"Release with Faust"} (based on Faust installation)
[TEST MODE] Executing: test -f "{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" (checking Projucer exists)
[TEST MODE] Executing: sysctl -n hw.ncpu (detecting CPU cores)
[TEST MODE] Result: {N} CPU cores detected
[TEST MODE] Would execute: "{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "projects/standalone/HISE Standalone.jucer"
[TEST MODE] Would execute: xcodebuild -project "projects/standalone/Builds/MacOSX/HISE.xcodeproj" -configuration "Release with Faust" -jobs {N} | "{hisePath}/tools/Projucer/xcbeautify"
[TEST MODE] Output path: projects/standalone/Builds/MacOSX/build/Release with Faust/HISE.app/Contents/MacOS/HISE
```

**Normal Mode (macOS - without Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "HISE Standalone.jucer"
# Detect CPU cores and use for parallel compilation
CORES=$(sysctl -n hw.ncpu)
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration Release -jobs $CORES | "{hisePath}/tools/Projucer/xcbeautify"
```

**Normal Mode (macOS - with Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "HISE Standalone.jucer"
# Detect CPU cores and use for parallel compilation
CORES=$(sysctl -n hw.ncpu)
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration "Release with Faust" -jobs $CORES | "{hisePath}/tools/Projucer/xcbeautify"
```

**Linux:**
- Navigate to projects/standalone
- Launch Projucer: `{hisePath}/JUCE/Projucer/Projucer --resave "HISE Standalone.jucer"`
- Build using Make

**Test Mode (Linux):**
```
[TEST MODE] Step 8: Compile HISE Standalone Application
[TEST MODE] Build configuration: {Release|ReleaseWithFaust} (based on Faust installation)
[TEST MODE] Executing: test -f "{hisePath}/JUCE/Projucer/Projucer" (checking Projucer exists)
[TEST MODE] Executing: nproc (detecting CPU cores)
[TEST MODE] Result: {N} CPU cores detected, will use {N-2} for compilation
[TEST MODE] Would execute: cd projects/standalone
[TEST MODE] Would execute: "{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
[TEST MODE] Would execute: cd Builds/LinuxMakefile
[TEST MODE] Would execute: make CONFIG=ReleaseWithFaust AR=gcc-ar -j{N-2}
[TEST MODE] Output path: projects/standalone/Builds/LinuxMakefile/build/HISE
```

**Normal Mode (Linux - without Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
cd Builds/LinuxMakefile
# Use all cores minus 2 to keep system responsive
make CONFIG=Release AR=gcc-ar -j$(nproc --ignore=2)
```

**Normal Mode (Linux - with Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
cd Builds/LinuxMakefile
# Use all cores minus 2 to keep system responsive
make CONFIG=ReleaseWithFaust AR=gcc-ar -j$(nproc --ignore=2)
```

- **High-level log:** "Compiling HISE Standalone application..."

### Step 9: Add HISE to PATH

**Path Selection:**
- If Faust was installed: Use `ReleaseWithFaust` output directory
- If Faust was not installed: Use `Release` output directory

**Windows:**
- Add HISE binary location to PATH

**Test Mode (Windows):**
```
[TEST MODE] Step 9: Add HISE to PATH
[TEST MODE] Build configuration used: {Release|"Release with Faust"}
[TEST MODE] Executing: echo %PATH% (checking current PATH)
[TEST MODE] Would execute: setx PATH "%PATH%;{hisePath}\projects\standalone\Builds\VisualStudio2026\x64\{Release|Release with Faust}\App"
```

**Normal Mode (Windows - without Faust):**
```batch
setx PATH "%PATH%;{hisePath}\projects\standalone\Builds\VisualStudio2026\x64\Release\App"
```

**Normal Mode (Windows - with Faust):**
```batch
setx PATH "%PATH%;{hisePath}\projects\standalone\Builds\VisualStudio2026\x64\Release with Faust\App"
```

**macOS:**
- Add HISE binary location to PATH
- Detect user's default shell and use appropriate config file

**Test Mode (macOS):**
```
[TEST MODE] Step 9: Add HISE to PATH
[TEST MODE] Build configuration used: {Release|"Release with Faust"}
[TEST MODE] Executing: echo $PATH (checking current PATH)
[TEST MODE] Executing: basename "$SHELL" (detecting user's shell)
[TEST MODE] Result: User shell is {zsh|bash}
[TEST MODE] Shell config file: {~/.zshrc|~/.bash_profile}
[TEST MODE] Would execute: echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/MacOSX/build/{Release|Release with Faust}/HISE.app/Contents/MacOS"' >> {~/.zshrc|~/.bash_profile}
[TEST MODE] Would execute: source {~/.zshrc|~/.bash_profile}
```

**Normal Mode (macOS - without Faust):**
```bash
# Note: On macOS, the binary is inside the app bundle at HISE.app/Contents/MacOS/HISE
# Detect shell and use appropriate config file
if [ "$(basename "$SHELL")" = "zsh" ]; then
    SHELL_CONFIG="$HOME/.zshrc"
else
    SHELL_CONFIG="$HOME/.bash_profile"
fi
echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/MacOSX/build/Release/HISE.app/Contents/MacOS"' >> "$SHELL_CONFIG"
source "$SHELL_CONFIG"
```

**Normal Mode (macOS - with Faust):**
```bash
# Note: On macOS, the binary is inside the app bundle at HISE.app/Contents/MacOS/HISE
# Detect shell and use appropriate config file
if [ "$(basename "$SHELL")" = "zsh" ]; then
    SHELL_CONFIG="$HOME/.zshrc"
else
    SHELL_CONFIG="$HOME/.bash_profile"
fi
echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/MacOSX/build/Release with Faust/HISE.app/Contents/MacOS"' >> "$SHELL_CONFIG"
source "$SHELL_CONFIG"
```

**Linux:**
- Add HISE binary location to PATH
- Detect user's default shell and use appropriate config file

**Test Mode (Linux):**
```
[TEST MODE] Step 9: Add HISE to PATH
[TEST MODE] Build configuration used: {Release|ReleaseWithFaust}
[TEST MODE] Executing: echo $PATH (checking current PATH)
[TEST MODE] Executing: basename "$SHELL" (detecting user's shell)
[TEST MODE] Result: User shell is {bash|zsh}
[TEST MODE] Shell config file: {~/.bashrc|~/.zshrc}
[TEST MODE] Would execute: echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/LinuxMakefile/build"' >> {~/.bashrc|~/.zshrc}
[TEST MODE] Would execute: source {~/.bashrc|~/.zshrc}
```

**Normal Mode (Linux - without Faust):**
```bash
# Detect shell and use appropriate config file
if [ "$(basename "$SHELL")" = "zsh" ]; then
    SHELL_CONFIG="$HOME/.zshrc"
else
    SHELL_CONFIG="$HOME/.bashrc"
fi
echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/LinuxMakefile/build"' >> "$SHELL_CONFIG"
source "$SHELL_CONFIG"
```

**Normal Mode (Linux - with Faust):**
```bash
# Detect shell and use appropriate config file
if [ "$(basename "$SHELL")" = "zsh" ]; then
    SHELL_CONFIG="$HOME/.zshrc"
else
    SHELL_CONFIG="$HOME/.bashrc"
fi
echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/LinuxMakefile/build"' >> "$SHELL_CONFIG"
source "$SHELL_CONFIG"
```

- **High-level log:** "Adding HISE to PATH environment variable..."

### Step 10: Verify Installation

**Test HISE Command Line:**
```bash
HISE --help
```

**Test Mode:**
```
[TEST MODE] Step 10: Verify Installation
[TEST MODE] Executing: which HISE (checking if HISE is in PATH - would require PATH update in normal mode)
[TEST MODE] Executing: test -f {hisePath}/projects/standalone/Builds/{platform}/build/{Release|ReleaseWithFaust}/HISE{.exe|.app|}
[TEST MODE] Result: HISE binary {found at expected location/not found}
[TEST MODE] Would execute: HISE --help (skipped - requires built binary)
```

**Normal Mode:**
- Expected output displays:
  - HISE Command Line Tool
  - Available commands (export, export_ci, full_exp, etc.)
  - Available arguments (-h, -ipp, -l, -t, -p, etc.)
- **High-level log:** "Verifying HISE installation and testing CLI commands..."

### Step 11: Compile Test Project

**Test Project Location:** `extras/demo_project/`

**Compile Test Project:**
```bash
HISE set_project_folder -p:"{hisePath}/extras/demo_project"
HISE export_ci "XmlPresetBackups/Demo.xml" -t:standalone -a:x64
```

**Test Mode:**
```
[TEST MODE] Step 11: Compile Test Project
[TEST MODE] Would execute: HISE set_project_folder -p:"{hisePath}/extras/demo_project"
[TEST MODE] Would execute: HISE export_ci "XmlPresetBackups/Demo.xml" -t:standalone -a:x64
[TEST MODE] Would verify: Demo project compiles successfully
[TEST MODE] Would check for: Compiled binary output in extras/demo_project/Binaries/
```

**Normal Mode:**
- Sets the demo project as the current HISE project folder
- Exports and compiles standalone application using CI export workflow
- Verifies all tools and SDKs are correctly configured
- **High-level log:** "Compiling test project to verify complete setup..."

### Step 12: Success Verification

**Successful completion criteria:**
1. HISE compiled from `projects/standalone/HISE Standalone.jucer`
2. HISE binary added to PATH
3. `HISE --help` displays CLI commands
4. Test project from `extras/demo_project/` compiles successfully
5. No errors during compilation

**Test Mode:**
```
[TEST MODE] Step 12: Success Verification
[TEST MODE] Would verify: All steps completed successfully
[TEST MODE] Would check: HISE is in PATH
[TEST MODE] Would check: HISE --help works
[TEST MODE] Would check: Demo project compiles
[TEST MODE] TEST MODE COMPLETE - Review commands above
[TEST MODE] No system modifications were performed
```

**Normal Mode:**
- **High-level log:** "Setup complete! HISE development environment is ready to use."

---

## Build Configuration Details

### Default Configuration
- **Configuration:** Release (without Faust) or ReleaseWithFaust (with Faust)
- **Architecture:** 64-bit (x64)
- **JUCE Version:** juce6 (stable)
- **Visual Studio:** 2026 (default on Windows)

### Build Configuration Selection
| Faust Installed | Platform | Configuration          | Output Directory                         |
|-----------------|----------|------------------------|------------------------------------------|
| No              | Windows  | `Release`              | `.../x64/Release/App/`                   |
|                 | macOS    | `Release`              | `.../build/Release/`                     |
|                 | Linux    | `Release`              | `.../build/`                             |
| Yes             | Windows  | `"Release with Faust"` | `.../x64/Release with Faust/App/`        |
|                 | macOS    | `"Release with Faust"` | `.../build/Release with Faust/`          |
|                 | Linux    | `ReleaseWithFaust`     | `.../build/`                             |

### Platform-Specific Build Commands

**Windows (VS2026 - without Faust):**
```batch
cd projects/standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration=Release /verbosity:minimal
```

**Windows (VS2026 - with Faust):**
```batch
cd projects/standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration="Release with Faust" /verbosity:minimal
```

**macOS (without Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "HISE Standalone.jucer"
# Detect CPU cores: sysctl -n hw.ncpu
CORES=$(sysctl -n hw.ncpu)
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration Release -jobs $CORES | "{hisePath}/tools/Projucer/xcbeautify"
```

**macOS (with Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "HISE Standalone.jucer"
# Detect CPU cores: sysctl -n hw.ncpu
CORES=$(sysctl -n hw.ncpu)
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration "Release with Faust" -jobs $CORES | "{hisePath}/tools/Projucer/xcbeautify"
```

**Linux (without Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
cd Builds/LinuxMakefile
# Detect CPU cores: nproc (uses all minus 2 to keep system responsive)
make CONFIG=Release AR=gcc-ar -j$(nproc --ignore=2)
```

**Linux (with Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
cd Builds/LinuxMakefile
# Detect CPU cores: nproc (uses all minus 2 to keep system responsive)
make CONFIG=ReleaseWithFaust AR=gcc-ar -j$(nproc --ignore=2)
```

> **Note:** Linux uses `ReleaseWithFaust` (no spaces), while Windows and macOS use `"Release with Faust"` (with spaces). This matches the configuration names in the `.jucer` file.

---

## Error Handling

### Common Issues & Solutions

**Git not found:**
- Windows: Download from https://git-scm.com/
- macOS: `xcode-select --install`
- Linux: `sudo apt-get install git`

**Visual Studio 2026 not found (Windows):**
- Direct to download page: https://visualstudio.microsoft.com/downloads/
- Specify "Visual Studio Community 2026" and "Desktop development with C++" workload

**Xcode not found (macOS):**
- Direct to Mac App Store
- Install Command Line Tools

**GCC version >11 (Linux):**
- **ABORT** with error message
- Suggest installing GCC 11: `sudo apt-get install gcc-11 g++-11`
- Provide alias instructions

**Projucer not found:**
- Verify JUCE submodule initialized
- Check path: `{hisePath}/JUCE/Projucer/Projucer`
- Run: `git submodule update --init --recursive`

**SDK extraction failed:**
- Verify tools/SDK/sdk.zip exists
- Check write permissions
- Manually guide extraction

**xcbeautify not found (macOS):**
- Verify HISE repository cloned completely
- Check path: `{hisePath}/tools/Projucer/xcbeautify`
- Verify executable permissions

**Projucer blocked by Gatekeeper (macOS):**
- Guide user to Security & Privacy settings
- Click "Open Anyway"

**HISE not in PATH after setup:**
- Check PATH configuration
- Verify shell configuration file (`.zshrc`, `.bashrc`)
- Restart shell or source configuration file

**Build failures:**
- Check compiler versions
- Verify SDK paths
- Review build output
- **ABORT** if non-trivial failure

**IPP not found (Windows):**
- Offer to disable IPP in Projucer
- Rebuild without IPP

**Test project compilation fails:**
- Verify demo project exists at `extras/demo_project/`
- Check all SDK paths
- Review error messages
- Provide specific troubleshooting steps

---

## User Interaction Model

### Interactive Prompts
- **Test Mode (Step 0):** "Run in test mode? (y/n)"
- Confirm optional component installation (IPP, Faust)
- No confirmation for core dependencies (automatic)

### High-Level Progress Feedback
- Display current step
- Show estimated time remaining
- Provide console output only for builds (not verbose)
- Summarize successful installations
- Display CLI help output when testing

### Test Mode Behavior
- **Executes (read-only checks):**
  - OS platform detection, OS version compatibility check
  - `git --version`, `gcc --version`, `xcodebuild -version` (tool availability)
  - `which` / `where` commands (path lookups)
  - `test -f` / `test -d` (file/directory existence checks)
  - `ls` (directory listing)
  - Environment variable reads (`echo $PATH`, `echo %PATH%`)
- **Skips (shows with `[TEST MODE] Would execute:` prefix):**
  - All installations (`apt-get install`, `brew install`, VS installer, etc.)
  - Git clone/checkout operations
  - File extractions (unzip)
  - Build commands (MSBuild, xcodebuild, make)
  - PATH modifications (setx, shell config writes)
  - Any command that modifies system state
- **Purpose:** Validate prerequisites and debug setup workflow before execution

### Normal Mode Behavior
- Automatically install core dependencies
- Only prompt for optional components
- Abort on errors requiring manual intervention

---

## Success Criteria

### Test Mode Completion Criteria
Agent completes successfully in test mode when:
1. OS platform detected and verified compatible
2. OS version verified
3. All commands for setup steps displayed with exact syntax
4. User can review commands for debugging
5. No installations or modifications performed

### Normal Mode Completion Criteria
Agent completes successfully in normal mode when:
1. Git is installed and repository cloned
2. IDE/compiler is installed and functional (VS2026/Xcode/Linux tools)
3. JUCE submodule is initialized with **juce6 branch**
4. Required SDKs are extracted and configured
5. HISE compiles from `projects/standalone/HISE Standalone.jucer` without errors
6. HISE binary is added to PATH environment variable
7. `HISE --help` displays available CLI commands
8. Test project from `extras/demo_project/` compiles successfully
9. System is fully ready for HISE development

---

## Technical Notes

### Compiler Requirements
- C++17 standard
- libc++ (LLVM C++ Standard Library)
- Windows: MSVC via Visual Studio 2026 (default)
- macOS: Clang via Xcode
- Linux: GCC ≤11 or Clang

### Project Files
- HISE Standalone: `projects/standalone/HISE Standalone.jucer`
- Test Project: `extras/demo_project/XmlPresetBackups/Demo.xml`
- Build Tool: Projucer (from JUCE submodule)
- JUCE Branch: juce6 (stable, only option)

### Build Artifacts

**Without Faust (Release configuration):**
- Windows: `projects/standalone/Builds/VisualStudio2026/x64/Release/App/HISE.exe`
- macOS: `projects/standalone/Builds/MacOSX/build/Release/HISE.app/Contents/MacOS/HISE`
- Linux: `projects/standalone/Builds/LinuxMakefile/build/HISE`

**With Faust ("Release with Faust" / ReleaseWithFaust configuration):**
- Windows: `projects/standalone/Builds/VisualStudio2026/x64/Release with Faust/App/HISE.exe`
- macOS: `projects/standalone/Builds/MacOSX/build/Release with Faust/HISE.app/Contents/MacOS/HISE`
- Linux: `projects/standalone/Builds/LinuxMakefile/build/HISE`

> **Note:** On macOS, the HISE binary is located inside the `.app` bundle. The PATH must include the full path to `HISE.app/Contents/MacOS` for the `HISE` command to be accessible from the terminal.

### Environment Variables
- **PATH:** Includes HISE binary directory for command-line access
- **Compiler Settings:** Stored in compilerSettings.xml (macOS) or equivalent
- **Visual Studio Version:** Default set to 2026 on Windows

### Optional Features
- Faust JIT compiler
- Perfetto profiling (disabled by default)
- Loris, RLottie (Minimal build excludes these)
- Intel IPP (Windows only, optional but recommended)

---

## Maintenance & Updates
- Follows develop branch by default
- Supports switching between branches
- Handles submodule updates
- Provides update workflow when new versions are available

---

## Limitations
- Requires internet connection for downloads
- Requires admin/sudo privileges for installations
- Does not handle code signing certificates automatically
- Does not handle licensing (GPL v3)
- Does not create installers (focus on development environment)
- JUCE branch is fixed to juce6 (stable)

---

## Support Resources
- Website: https://hise.audio
- Forum: https://forum.hise.audio/
- Documentation: https://docs.hise.dev/
- Repository: https://github.com/christophhart/HISE
- CLI Help: Run `HISE --help` for complete command reference
