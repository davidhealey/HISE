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
- **Test Mode:** Displays exact commands without executing them
  - Performs OS compatibility checks (platform detection, OS version)
  - Skips all installations and modifications
  - Shows exact commands that would be executed
  - Output format: `[TEST MODE] Would execute: {command}`
  - Used for debugging setup workflow
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
[TEST MODE] Step 2: Git Setup
[TEST MODE] Would check: git --version
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
  - Yes: Install based on platform/architecture
  - No: Skip Faust installation

### Step 5: IDE & Compiler Setup

**Windows:**
- Download/install Visual Studio 2026 Community (automatic)
- Select "Desktop development with C++" workload (automatic)
- Verify MSBuild availability at `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe`

**Test Mode (Windows):**
```
[TEST MODE] Step 5: IDE & Compiler Setup
[TEST MODE] Would execute: Start installer from https://visualstudio.microsoft.com/downloads/
[TEST MODE] Would select: "Desktop development with C++" workload
[TEST MODE] Would verify: C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe exists
```

**macOS:**
- Install Xcode from App Store (automatic)
- Install Command Line Tools (automatic)
- Accept Xcode license (automatic)

**Test Mode (macOS):**
```
[TEST MODE] Step 5: IDE & Compiler Setup
[TEST MODE] Would execute: xcode-select --install
[TEST MODE] Would execute: Accept Xcode license automatically
[TEST MODE] Would verify: /usr/bin/xcodebuild exists
```

**Linux:**
- Install dependencies (automatic)

**Test Mode (Linux):**
```
[TEST MODE] Step 5: IDE & Compiler Setup
[TEST MODE] Would execute: sudo apt-get -y install build-essential make llvm clang libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev mesa-common-dev libasound2-dev freeglut3-dev libxcomposite-dev libcurl4-gnutls-dev libgtk-3-dev libjack-jackd2-dev libwebkit2gtk-4.0-dev libpthread-stubs0-dev ladspa-sdk
[TEST MODE] Would check: gcc --version ≤ 11
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
[TEST MODE] Would verify: tools/SDK/sdk.zip exists
[TEST MODE] Would execute: unzip tools/SDK/sdk.zip -d tools/SDK/
[TEST MODE] Would verify: tools/SDK/ASIOSDK2.3/common exists
[TEST MODE] Would verify: tools/SDK/VST3 SDK/public.sdk exists
```

**Normal Mode:**
- **High-level log:** "Extracting and configuring required SDKs..."

### Step 7: JUCE Submodule Verification
- Verify JUCE submodule is on **juce6 branch** (configured in Step 2)
- Validate JUCE structure is complete

**Test Mode:**
```
[TEST MODE] Step 7: JUCE Submodule Verification
[TEST MODE] Would verify: JUCE submodule is on juce6 branch
[TEST MODE] Would verify: JUCE/modules/juce_core/system/juce_StandardHeader.h exists
```

**Normal Mode:**
- **High-level log:** "Verifying JUCE submodule configuration..."

### Step 8: Compile HISE Standalone Application

**Windows:**
- Launch Projucer: `{hisePath}/JUCE/Projucer/Projucer.exe`
- Load project: `projects/standalone/HISE Standalone.jucer`
- Save project to generate IDE files
- Build using MSBuild

**Test Mode (Windows):**
```
[TEST MODE] Step 8: Compile HISE Standalone Application
[TEST MODE] Would execute: "{hisePath}\JUCE\Projucer\Projucer.exe" --resave "projects\standalone\HISE Standalone.jucer"
[TEST MODE] Would execute: "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" "projects\standalone\Builds\VisualStudio2026\HISE.sln" /p:Configuration=Release /verbosity:minimal
[TEST MODE] Would verify: projects\standalone\Builds\VisualStudio2026\x64\Release\App\HISE.exe exists
```

**Normal Mode (Windows):**
```batch
cd projects\standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration=Release /verbosity:minimal
```

**macOS:**
- Launch Projucer: `{hisePath}/JUCE/Projucer/Projucer.app`
- Load project: `projects/standalone/HISE Standalone.jucer`
- Save project to generate IDE files
- Build using xcodebuild

**Test Mode (macOS):**
```
[TEST MODE] Step 8: Compile HISE Standalone Application
[TEST MODE] Would execute: "{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "projects/standalone/HISE Standalone.jucer"
[TEST MODE] Would execute: xcodebuild -project "projects/standalone/Builds/MacOSX/HISE.xcodeproj" -configuration Release -jobs 4 | "{hisePath}/tools/Projucer/xcbeautify"
[TEST MODE] Would verify: projects/standalone/Builds/MacOSX/build/Release/HISE.app exists
```

**Normal Mode (macOS):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "HISE Standalone.jucer"
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration Release -jobs 4 | "{hisePath}/tools/Projucer/xcbeautify"
```

**Linux:**
- Navigate to projects/standalone
- Launch Projucer: `{hisePath}/JUCE/Projucer/Projucer --resave "HISE Standalone.jucer"`
- Build using Make

**Test Mode (Linux):**
```
[TEST MODE] Step 8: Compile HISE Standalone Application
[TEST MODE] Would execute: cd projects/standalone
[TEST MODE] Would execute: "{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
[TEST MODE] Would execute: cd Builds/LinuxMakefile
[TEST MODE] Would execute: make CONFIG=Release AR=gcc-ar -j`nproc --ignore=2`
[TEST MODE] Would verify: projects/standalone/Builds/LinuxMakefile/build/HISE exists
```

**Normal Mode (Linux):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
cd Builds/LinuxMakefile
make CONFIG=Release AR=gcc-ar -j`nproc --ignore=2`
```

- **High-level log:** "Compiling HISE Standalone application..."

### Step 9: Add HISE to PATH

**Windows:**
- Add HISE binary location to PATH

**Test Mode (Windows):**
```
[TEST MODE] Step 9: Add HISE to PATH
[TEST MODE] Would execute: setx PATH "%PATH%;{hisePath}\projects\standalone\Builds\VisualStudio2026\x64\Release\App"
[TEST MODE] Would execute: echo %PATH% to verify HISE in PATH
```

**Normal Mode (Windows):**
```batch
setx PATH "%PATH%;{hisePath}\projects\standalone\Builds\VisualStudio2026\x64\Release\App"
```

**macOS:**
- Add HISE binary location to PATH

**Test Mode (macOS):**
```
[TEST MODE] Step 9: Add HISE to PATH
[TEST MODE] Would execute: echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/MacOSX/build/Release"' >> ~/.zshrc
[TEST MODE] Would execute: source ~/.zshrc
[TEST MODE] Would execute: which HISE to verify PATH
```

**Normal Mode (macOS):**
```bash
echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/MacOSX/build/Release"' >> ~/.zshrc
source ~/.zshrc
```

**Linux:**
- Add HISE binary location to PATH

**Test Mode (Linux):**
```
[TEST MODE] Step 9: Add HISE to PATH
[TEST MODE] Would execute: echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/LinuxMakefile/build"' >> ~/.bashrc
[TEST MODE] Would execute: source ~/.bashrc
[TEST MODE] Would execute: which HISE to verify PATH
```

**Normal Mode (Linux):**
```bash
echo 'export PATH="$PATH:{hisePath}/projects/standalone/Builds/LinuxMakefile/build"' >> ~/.bashrc
source ~/.bashrc
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
[TEST MODE] Would execute: HISE --help
[TEST MODE] Would check for: HISE Command Line Tool output
[TEST MODE] Would check for: Available commands list
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
- **Configuration:** Release
- **Architecture:** 64-bit (x64)
- **JUCE Version:** juce6 (stable)
- **Visual Studio:** 2026 (default on Windows)

### Platform-Specific Build Commands

**Windows (VS2026 - Default):**
```batch
cd projects/standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration=Release /verbosity:minimal
```

**macOS:**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "HISE Standalone.jucer"
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration Release -jobs 4 | "{hisePath}/tools/Projucer/xcbeautify"
```

**Linux:**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
cd Builds/LinuxMakefile
make CONFIG=Release AR=gcc-ar -j`nproc --ignore=2`
```

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
- **Performs:** OS platform detection, OS version compatibility check
- **Skips:** All installations, all system modifications
- **Shows:** Exact commands with `[TEST MODE]` prefix
- **Purpose:** Debug and verify setup workflow before execution

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
- Windows: `projects/standalone/Builds/VisualStudio2026/x64/Release/App/HISE.exe`
- macOS: `projects/standalone/Builds/MacOSX/build/Release/HISE.app`
- Linux: `projects/standalone/Builds/LinuxMakefile/build/HISE`

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
