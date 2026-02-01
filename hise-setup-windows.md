---
name: hise-setup-windows
description: Setup Windows computer to work with HISE.
---

# HISE Development Environment Setup - Windows

## Overview
Automates complete development environment setup for HISE (Hart Instrument Software Environment) on Windows.

## Supported Platforms
- **Windows 7+** (64-bit x64 only - **ARM64 not currently supported**)

> **Windows ARM64 Note:** HISE does not currently have native Windows ARM64 build configurations. Windows ARM devices can run the x64 build through emulation, but native ARM64 builds would require modifications to the Projucer project file.

---

## Core Capabilities

### 1. Platform Detection
- Automatically detects Windows version
- Verifies OS version compatibility
- **Detects CPU architecture** (x64, arm64)
- Adjusts setup workflow based on platform
- **Windows ARM64:** Warns user that native ARM64 builds are not supported; x64 builds will run via emulation

### 2. Test Mode Operation
- **Test Mode:** Validates environment without making changes
  - **Executes:** All non-modifying commands (read-only operations)
    - `git --version` (check if Git is installed)
    - `where` / `dir` (check file/directory existence)
    - Platform detection and OS version checks
    - Disk space checks
  - **Skips:** All modifying commands (shows what would be executed)
    - Installations (VS installer, downloads)
    - Git operations that change state (`git clone`, `git checkout`, etc.)
    - File modifications (unzip, write, delete)
    - Build commands (MSBuild)
    - PATH modifications
  - Output format for skipped commands: `[TEST MODE] Would execute: {command}`
  - Used for debugging setup workflow and validating prerequisites
- **Normal Mode:** Executes full installation

### 3. Git Installation & Repository Setup
- Checks for Git installation
- Installs Git if missing
- Clones HISE repository from **develop branch** (default)
- Initializes and updates JUCE submodule
- Uses **juce6 branch only** (stable version)

### 4. IDE & Compiler Installation (REQUIRED - NO SKIP)

> **This step is mandatory.** HISE cannot be compiled without a C++ compiler. The agent must not offer an option to skip this step.

- Detects Visual Studio installation (**VS2026 required**)
- If not installed: **HALT** and direct user to install VS2026 Community with "Desktop development with C++" workload
- Verifies MSBuild availability before proceeding
- **Cannot proceed without Visual Studio**

### 5. SDK & Tool Installation

**Required SDKs:**
- Extracts and configures ASIO SDK 2.3 (low-latency audio)
- Extracts and configures VST3 SDK

**Optional (User Prompted):**
- **Intel IPP oneAPI:** Performance optimization (installed after Visual Studio)
  - User prompted after VS installation
  - Provides option to build without IPP
  - Configures Projucer setting accordingly
  - Downloads using curl to avoid timeout issues
- **Faust DSP programming language**
  - User prompted before installation
  - Download using curl or manually from GitHub releases
  - Install to `C:\Program Files\Faust\`
  - Requires Faust version 2.54.0+ (recommended)

### 6. HISE Compilation
- Compiles HISE from `projects/standalone/HISE Standalone.jucer`
- Uses Projucer (from JUCE submodule) to generate IDE project files
- Compiles HISE using Visual Studio 2026 (Release)
- **Aborts on non-trivial build failures**

### 7. Environment Setup
- Adds compiled HISE binary to PATH environment variable
- Verifies HISE is callable from command line

### 8. Verification
- Runs `HISE --help` to display available CLI commands
- Compiles test project from `extras/demo_project/`
- Confirms system is ready for HISE development

---

## Verified Download URLs

### Windows Specific
- **Visual Studio 2026 Community:** https://visualstudio.microsoft.com/downloads/
  - Download: "Visual Studio Community 2026" (Web Installer)
  - Workload: "Desktop development with C++"
- **Intel IPP oneAPI 2021.11.0.533:** https://registrationcenter-download.intel.com/akdlm/IRC_NAS/b4adec02-353b-4144-aa21-f2087040f316/w_ipp_oneapi_p_2021.11.0.533.exe
  - **Download using curl:** `curl -L -o "intel-ipp-installer.exe" "{URL}"`
- **ASIO SDK 2.3:** https://www.steinberg.net/de/company/developer.html

### All Platforms
- **Faust DSP Language (2.54.0+):** https://github.com/grame-cncm/faust/releases
  - Windows: `Faust-VERSION-win64.exe`

---

## Required Tools & Paths

### Windows Build Tools
1. **MSBuild** (from Visual Studio 2026)
   - Path: `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe`

2. **Projucer** (from JUCE submodule)
   - Path: `{hisePath}\JUCE\Projucer\Projucer.exe`

3. **Visual Studio Solution**
   - Path: `{buildPath}\Builds\VisualStudio2026\{project}.sln`

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
```batch
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

**Default Installation Path:**
- **Windows:** `C:\HISE` (requires administrator privileges) or `%USERPROFILE%\HISE` (no admin required)

> **Windows Note:** If running without administrator privileges, the agent should detect this and suggest `%USERPROFILE%\HISE` (e.g., `C:\Users\YourName\HISE`) as the default instead of `C:\HISE`.

**Workflow:**
1. Detect current working directory
2. Compare with platform-specific default path
3. Check if default path exists and is writable
4. If different from default or default not accessible, prompt user:

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
[TEST MODE] Executing: Check if default path exists and is writable
[TEST MODE] Result: {exists and writable | does not exist | not writable}
[TEST MODE] Executing: Check if running as Administrator
[TEST MODE] Result: {Administrator | Standard User}
[TEST MODE] If Standard User, suggest: %USERPROFILE%\HISE
[TEST MODE] Would prompt user for installation location choice
[TEST MODE] Selected installation path: {selected_path}
```

**Normal Mode:**
- If current directory equals default path: proceed without prompting
- If current directory differs: display prompt and wait for user selection
- **Windows:** Check for administrator privileges and directory accessibility:
  ```batch
  REM Check if running as Administrator
  net session >nul 2>&1
  if %errorLevel% == 0 (
      REM Running as Admin - can use C:\HISE
      set "DEFAULT_PATH=C:\HISE"
  ) else (
      REM Standard user - use user profile directory
      set "DEFAULT_PATH=%USERPROFILE%\HISE"
  )
  REM Create directory if it doesn't exist
  if not exist "%DEFAULT_PATH%" mkdir "%DEFAULT_PATH%"
  cd /d "%DEFAULT_PATH%"
  ```
- Store selected path as `{hisePath}` for all subsequent steps
- **High-level log:** "Determining HISE installation location..."

---

### Step 1: Test Mode Detection
```
Prompt: "Run in test mode? (y/n)"

If test mode:
  - Detect Windows version
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
- Verify Windows compatibility
- **Detect CPU architecture**
- Check disk space requirements (~2-5 GB)
- Detect existing installations

**Architecture Detection Commands:**
- **Windows:** `echo %PROCESSOR_ARCHITECTURE%` (returns `AMD64` for x64, `ARM64` for ARM)

**Test Mode:**
```
[TEST MODE] Platform detected: Windows
[TEST MODE] OS version: {version} - {COMPATIBLE/NOT COMPATIBLE}
[TEST MODE] CPU architecture: {x64/arm64}
[TEST MODE] (Windows ARM64) WARNING: Native ARM64 builds not supported - will build x64 (runs via emulation)
[TEST MODE] Disk space check: {X GB available} - SUFFICIENT
```

**Normal Mode:**
- **High-level log:** "Detecting platform and checking system requirements..."
- **Windows ARM64:** Display warning that native ARM64 is not supported, x64 build will be created (runs via Windows x64 emulation)

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
```batch
git clone https://github.com/christophhart/HISE.git
cd HISE
git checkout develop
git submodule update --init
cd JUCE && git checkout juce6 && cd ..
```

- **High-level log:** "Installing Git and cloning HISE repository from develop branch..."

### Step 4: User Prompts (Before Installation)

**All platforms:**
- **Prompt:** "Install Faust DSP programming language? [Y/n]"
  - Yes: Install based on platform/architecture, use `ReleaseWithFaust` build configuration
  - No: Skip Faust installation, use `Release` build configuration

### Step 4a: Faust Installation (If Selected)

> **Faust Download URL:** https://github.com/grame-cncm/faust/releases
> **Recommended Version:** 2.54.0 or later

**Windows Faust Installation:**
1. **Option A - Download using curl (automatic):**
   ```batch
   curl -L -o "%TEMP%\faust-installer.exe" "https://github.com/grame-cncm/faust/releases/latest/download/Faust-2.54.0-win64.exe"
   "%TEMP%\faust-installer.exe"
   ```
2. **Option B - Manual download:**
   - Direct user to download `Faust-VERSION-win64.exe` from https://github.com/grame-cncm/faust/releases
   - Instruct user to run the installer and install to default path: `C:\Program Files\Faust\`
    - **IMPORTANT:** Must use default path - the .jucer file has this path hardcoded
3. **WAIT** for user to confirm installation is complete
4. Verify installation by checking that these paths exist:
    ```batch
    if exist "C:\Program Files\Faust\lib\faust.dll" (echo Faust installed successfully) else (echo Faust installation not found)
    ```
5. **If verification fails:** Re-prompt user to complete installation, do NOT proceed
6. **No .jucer modifications needed** - Windows Faust configurations already include:
    - Header path: `C:\Program Files\Faust\include`
    - Library path: `C:\Program Files\Faust\lib`
    - Post-build command to copy `faust.dll` to output directory

**Normal Mode (Windows Faust) - Waiting State:**
```
Faust DSP is required for the selected build configuration.

Option A - Automatic download using curl:
Would you like to download and install Faust automatically? [Y/n]

If yes, will execute:
curl -L -o "%TEMP%\faust-installer.exe" "https://github.com/grame-cncm/faust/releases/latest/download/Faust-2.54.0-win64.exe"
"%TEMP%\faust-installer.exe"

Option B - Manual installation:
If you prefer manual installation, please:
1. Download Faust from: https://github.com/grame-cncm/faust/releases
   - Download file: Faust-X.XX.X-win64.exe (latest version)
2. Run the installer
3. IMPORTANT: Install to the default path: C:\Program Files\Faust\
4. Complete the installation

Press Enter when installation is complete, or type 'skip' to build without Faust...
```

After user confirms, verify installation:
- Check: `C:\Program Files\Faust\lib\faust.dll` exists
- If not found: Display error and re-prompt
- If found: Proceed with `Release with Faust` build configuration

**Test Mode (Windows Faust):**
```
[TEST MODE] Step 4a: Faust Installation
[TEST MODE] Would offer automatic or manual installation
[TEST MODE] If automatic, would execute: curl -L -o "%TEMP%\faust-installer.exe" "https://github.com/grame-cncm/faust/releases/latest/download/Faust-2.54.0-win64.exe"
[TEST MODE] Would execute: "%TEMP%\faust-installer.exe"
[TEST MODE] If manual, would direct user to: https://github.com/grame-cncm/faust/releases
[TEST MODE] Would instruct: Download Faust-VERSION-win64.exe
[TEST MODE] Would instruct: Install to C:\Program Files\Faust\ (MUST use default path)
[TEST MODE] Would WAIT for user confirmation
[TEST MODE] Executing: if exist "C:\Program Files\Faust\lib\faust.dll" echo OK
[TEST MODE] Result: faust.dll {found|not found}
[TEST MODE] No .jucer modifications required for Windows
```

**Post-Installation (Windows):**
- After HISE is built and running, set the `FaustPath` in HISE Settings:
  - Windows: `C:\Program Files\Faust\`
- This allows HISE to find Faust libraries for DSP compilation

### Step 5: IDE & Compiler Setup (REQUIRED)

> **IMPORTANT:** This step is **mandatory** and cannot be skipped. HISE requires a C++ compiler to build. The setup process must wait for the IDE/compiler installation to complete before proceeding.

- Check if Visual Studio 2026 is installed
- If not installed:
  1. Direct user to download **Visual Studio 2026 Community** from https://visualstudio.microsoft.com/downloads/
  2. **IMPORTANT:** Must install the standard **Community edition**, NOT Preview/Insider editions
     - Preview/Insider editions install to different paths and will cause build failures
  3. Instruct user to select **"Desktop development with C++"** workload during installation
  4. **WAIT** for user to complete installation before proceeding
  5. Do NOT offer option to skip this step
- Verify MSBuild availability at `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe`
- **If MSBuild not found after user confirms installation:**
  - Check if Preview/Insider edition was installed instead (wrong path)
  - Re-prompt user to install the standard Community edition

**Test Mode (Windows):**
```
[TEST MODE] Step 5: IDE & Compiler Setup (REQUIRED)
[TEST MODE] Executing: where msbuild (checking for Visual Studio)
[TEST MODE] Executing: test -f "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe"
[TEST MODE] Result: Visual Studio 2026 {installed/not installed}
[TEST MODE] If not installed: HALT and instruct user to install Visual Studio 2026
[TEST MODE] This step is MANDATORY - do not offer skip option
[TEST MODE] Required workload: "Desktop development with C++"
```

**Normal Mode:**
- **High-level log:** "Checking IDE and compiler tools (REQUIRED)..."
- **HALT** if compiler not available - do not proceed to next step
- **NO SKIP OPTION** - user must install required tools to continue

### Step 5a: Intel IPP Installation (Optional)

> **Note:** Intel IPP should be installed AFTER Visual Studio to allow IPP installer to integrate properly with Visual Studio.

**Windows only:**
- **Prompt:** "Install Intel IPP oneAPI for performance optimization? (Recommended) [Y/n]"
  - Yes: Download and install from verified URL using curl
  - No: Configure Projucer to build without IPP

**Intel IPP Download & Installation:**
1. Download using curl:
   ```batch
   curl -L -o "%TEMP%\intel-ipp-installer.exe" "https://registrationcenter-download.intel.com/akdlm/IRC_NAS/b4adec02-353b-4144-aa21-f2087040f316/w_ipp_oneapi_p_2021.11.0.533.exe"
   ```
2. Run installer:
   ```batch
   "%TEMP%\intel-ipp-installer.exe"
   ```
3. **WAIT** for installer to complete
4. Verify installation:
   ```batch
   if exist "C:\Program Files (x86)\Intel\oneAPI\ipp\latest" (echo Intel IPP installed successfully) else (echo Intel IPP installation not found)
   ```

**Test Mode (Windows IPP):**
```
[TEST MODE] Step 5a: Intel IPP Installation
[TEST MODE] Would execute: curl -L -o "%TEMP%\intel-ipp-installer.exe" "https://registrationcenter-download.intel.com/akdlm/IRC_NAS/b4adec02-353b-4144-aa21-f2087040f316/w_ipp_oneapi_p_2021.11.0.533.exe"
[TEST MODE] Would execute: "%TEMP%\intel-ipp-installer.exe"
[TEST MODE] Would WAIT for installer completion
[TEST MODE] Executing: if exist "C:\Program Files (x86)\Intel\oneAPI\ipp\latest" echo OK
[TEST MODE] Result: Intel IPP {found|not found}
```

**Normal Mode (Windows IPP):**
```batch
REM Download Intel IPP installer using curl
curl -L -o "%TEMP%\intel-ipp-installer.exe" "https://registrationcenter-download.intel.com/akdlm/IRC_NAS/b4adec02-353b-4144-aa21-f2087040f316/w_ipp_oneapi_p_2021.11.0.533.exe"
REM Run installer
"%TEMP%\intel-ipp-installer.exe"
REM Wait for user to complete installation
```

**Note:** The -L flag in curl follows redirects, which is needed for Intel's download server.

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

> **IMPORTANT - Build Timeout:** HISE compilation can take **5-15 minutes** depending on the system.
> - Set command timeout to at least **600000ms (10 minutes)** for build commands
> - Do NOT abort the build while the compiler is still running
> - Monitor build output for progress (compiler messages indicate active compilation)
> - Only consider the build failed if the command returns a non-zero exit code

**Windows:**
- Launch Projucer: `{hisePath}\JUCE\Projucer\Projucer.exe`
- Load project: `projects\standalone\HISE Standalone.jucer`
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
REM Timeout: 600000ms (10 minutes) - compilation takes 5-15 minutes
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration=Release /verbosity:minimal
```

**Normal Mode (Windows - with Faust):**
```batch
cd projects\standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
REM Timeout: 600000ms (10 minutes) - compilation takes 5-15 minutes
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration="Release with Faust" /verbosity:minimal
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

- **High-level log:** "Adding HISE to PATH environment variable..."

### Step 10: Verify Installation

**Test HISE Command Line:**
```batch
HISE --help
```

**Test Mode:**
```
[TEST MODE] Step 10: Verify Installation
[TEST MODE] Executing: where HISE (checking if HISE is in PATH - would require PATH update in normal mode)
[TEST MODE] Executing: test -f {hisePath}\projects\standalone\Builds\VisualStudio2026\x64\{Release|Release with Faust}\App\HISE.exe
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

**Test Project Location:** `extras\demo_project\`

**Compile Test Project:**
```batch
HISE set_project_folder -p:"{hisePath}\extras\demo_project"
HISE export_ci "XmlPresetBackups\Demo.xml" -t:standalone -a:x64
```

**Test Mode:**
```
[TEST MODE] Step 11: Compile Test Project
[TEST MODE] Would execute: HISE set_project_folder -p:"{hisePath}\extras\demo_project"
[TEST MODE] Would execute: HISE export_ci "XmlPresetBackups\Demo.xml" -t:standalone -a:x64
[TEST MODE] Would verify: Demo project compiles successfully
[TEST MODE] Would check for: Compiled binary output in extras\demo_project\Binaries\
```

**Normal Mode:**
- Sets the demo project as the current HISE project folder
- Exports and compiles standalone application using CI export workflow
- Verifies all tools and SDKs are correctly configured
- **High-level log:** "Compiling test project to verify complete setup..."

### Step 12: Success Verification

**Successful completion criteria:**
1. HISE compiled from `projects\standalone\HISE Standalone.jucer`
2. HISE binary added to PATH
3. `HISE --help` displays CLI commands
4. Test project from `extras\demo_project\` compiles successfully
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
- **Visual Studio:** 2026 (default)

### Build Configuration Selection
| Faust Installed | Platform | Configuration          | Output Directory                         |
|-----------------|----------|------------------------|------------------------------------------|
| No              | Windows  | `Release`              | `...\x64\Release\App\`                   |
| Yes             | Windows  | `"Release with Faust"` | `...\x64\Release with Faust\App\`        |

### Platform-Specific Build Commands

**Windows (VS2026 - without Faust):**
```batch
cd projects\standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration=Release /verbosity:minimal
```

**Windows (VS2026 - with Faust):**
```batch
cd projects\standalone
"{hisePath}\JUCE\Projucer\Projucer.exe" --resave "HISE Standalone.jucer"
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MsBuild.exe" Builds\VisualStudio2026\HISE.sln /p:Configuration="Release with Faust" /verbosity:minimal
```

---

## Error Handling

### Common Issues & Solutions

**Git not found:**
- Windows: Download using curl or visit https://git-scm.com/
  ```batch
  curl -L -o "%TEMP%\git-installer.exe" "https://github.com/git-for-windows/git/releases/download/v2.43.0.windows.1/Git-2.43.0-64-bit.exe"
  "%TEMP%\git-installer.exe"
  ```

**Visual Studio 2026 not found (Windows):**
- Direct to download page: https://visualstudio.microsoft.com/downloads/
- Specify "Visual Studio Community 2026" and "Desktop development with C++" workload

**Windows ARM64 detected:**
- Display warning: Native ARM64 builds not currently supported
- Proceed with x64 build configuration (will run via Windows x64 emulation)
- Ensure Visual Studio includes x64 build tools (should be included by default)
- The resulting HISE.exe will be x64 and run through emulation on ARM64 Windows

**Projucer not found:**
- Verify JUCE submodule initialized
- Check path: `{hisePath}\JUCE\Projucer\Projucer.exe`
- Run: `git submodule update --init --recursive`

**SDK extraction failed:**
- Verify tools\SDK\sdk.zip exists
- Check write permissions
- Manually guide extraction

**HISE not in PATH after setup:**
- Check PATH configuration
- Restart shell to apply changes

**Build failures:**
- Check compiler versions
- Verify SDK paths
- Review build output
- **ABORT** if non-trivial failure

**IPP not found (Windows):**
- Download using curl:
  ```batch
  curl -L -o "%TEMP%\intel-ipp-installer.exe" "https://registrationcenter-download.intel.com/akdlm/IRC_NAS/b4adec02-353b-4144-aa21-f2087040f316/w_ipp_oneapi_p_2021.11.0.533.exe"
  "%TEMP%\intel-ipp-installer.exe"
  ```
- Offer to disable IPP in Projucer if download/installation fails
- Rebuild without IPP

**Faust installation failed (Windows):**
- Verify `C:\Program Files\Faust\` exists
- Check that `faust.dll` is present in `C:\Program Files\Faust\lib\`
- Re-download using curl:
  ```batch
  curl -L -o "%TEMP%\faust-installer.exe" "https://github.com/grame-cncm/faust/releases/latest/download/Faust-2.54.0-win64.exe"
  "%TEMP%\faust-installer.exe"
  ```
- Or download manually from https://github.com/grame-cncm/faust/releases
- Run installer as Administrator if permission issues occur

**Faust version too old:**
- HISE requires Faust 2.54.0 or later
- If using older version (e.g., 2.50.6), enable `HI_FAUST_NO_WARNING_MESSAGES` flag in Projucer
- Recommended: Update to latest Faust release

**Test project compilation fails:**
- Verify demo project exists at `extras\demo_project\`
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
  - `git --version` (tool availability)
  - `where` / `dir` commands (path lookups)
  - `test -f` / `test -d` (file/directory existence checks)
  - Environment variable reads (`echo %PATH%`)
- **Skips (shows with `[TEST MODE] Would execute:` prefix):**
  - All installations (VS installer, downloads via curl)
  - Git clone/checkout operations
  - File extractions (unzip)
  - Build commands (MSBuild)
  - PATH modifications (setx)
  - Any command that modifies system state
- **Purpose:** Validate prerequisites and debug setup workflow before execution
- **Download Method:** All downloads on Windows use `curl` instead of PowerShell Invoke-WebRequest to avoid timeout issues with large files

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
2. IDE/compiler is installed and functional (VS2026)
3. JUCE submodule is initialized with **juce6 branch**
4. Required SDKs are extracted and configured
5. HISE compiles from `projects\standalone\HISE Standalone.jucer` without errors
6. HISE binary is added to PATH environment variable
7. `HISE --help` displays available CLI commands
8. Test project from `extras\demo_project\` compiles successfully
9. System is fully ready for HISE development

---

## Technical Notes

### Build Timeouts
- **HISE compilation takes 5-15 minutes** depending on system specifications
- **Recommended timeout for build commands: 600000ms (10 minutes)**
- Do NOT abort build commands while compiler output is still being generated
- Build is only failed if the command returns a non-zero exit code
- Projucer `--resave` command is fast (< 30 seconds)

### Compiler Requirements
- C++17 standard
- MSVC via Visual Studio 2026 (default)

### Project Files
- HISE Standalone: `projects\standalone\HISE Standalone.jucer`
- Test Project: `extras\demo_project\XmlPresetBackups\Demo.xml`
- Build Tool: Projucer (from JUCE submodule)
- JUCE Branch: juce6 (stable, only option)

### Build Artifacts

**Without Faust (Release configuration):**
- Windows: `projects\standalone\Builds\VisualStudio2026\x64\Release\App\HISE.exe`

**With Faust ("Release with Faust" / ReleaseWithFaust configuration):**
- Windows: `projects\standalone\Builds\VisualStudio2026\x64\Release with Faust\App\HISE.exe`

### Environment Variables
- **PATH:** Includes HISE binary directory for command-line access
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
- **Windows:** Creating directories in `C:\` root requires administrator privileges. Either:
  - Run the terminal/agent as Administrator, OR
  - Choose a user-writable location like `%USERPROFILE%\HISE` (e.g., `C:\Users\YourName\HISE`)
- **Windows ARM64:** Native ARM64 builds are not currently supported
  - The HISE Projucer project only includes x64 configurations
  - On Windows ARM devices, the x64 build will be created and runs via Windows x64 emulation
  - Performance may be reduced compared to native ARM64 builds
  - Native ARM64 support would require adding ARM64 configurations to the `.jucer` file
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
