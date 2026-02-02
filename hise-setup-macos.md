---
name: hise-setup-macos
description: Setup macOS computer to work with HISE.
---

# HISE Development Environment Setup - macOS

## Overview
Automates complete development environment setup for HISE (Hart Instrument Software Environment) on macOS.

## Supported Platforms
- **macOS 10.7+** (10.13+ for development, supports both Intel x64 and Apple Silicon arm64)

---

## Core Capabilities

### 1. Platform Detection
- Automatically detects macOS version
- Verifies OS version compatibility
- **Detects CPU architecture** (x64, arm64)
- Adjusts setup workflow based on platform

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
    - Installations (brew install, etc.)
    - Git operations that change state (`git clone`, `git checkout`, etc.)
    - File modifications (unzip, write, delete)
    - Build commands (xcodebuild, make)
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

- Detects Xcode installation
- If not installed: **HALT** and direct user to install Xcode with Command Line Tools
- Handles Gatekeeper permission issues
- **Cannot proceed without Xcode**

### 5. SDK & Tool Installation

**Required SDKs:**
- Extracts and configures ASIO SDK 2.3 (not used on macOS but included)
- Extracts and configures VST3 SDK (all platforms)

**Optional (User Prompted):**
- **Faust DSP programming language**
  - User prompted before installation
  - Download architecture-specific DMG (x64 or arm64), extract to `{hisePath}/tools/faust/`
  - Requires Faust version 2.54.0+ (recommended)

### 6. HISE Compilation
- Compiles HISE from `projects/standalone/HISE Standalone.jucer`
- Uses Projucer (from JUCE submodule) to generate IDE project files
- Compiles HISE using Xcode (Release) using **xcbeautify** for output formatting
- **Aborts on non-trivial build failures**

### 7. Environment Setup
- Adds compiled HISE binary to PATH environment variable
- Displays code signing certificate setup instructions
- Verifies HISE is callable from command line

### 8. Verification
- Runs `HISE --help` to display available CLI commands
- Compiles test project from `extras/demo_project/`
- Confirms system is ready for HISE development

---

## Verified Download URLs

### macOS Specific
- **Xcode:** https://developer.apple.com/xcode/
  - Available from Mac App Store

### All Platforms
- **Faust DSP Language (2.54.0+):** https://github.com/grame-cncm/faust/releases
  - macOS Intel: `Faust-VERSION-x64.dmg`
  - macOS Apple Silicon: `Faust-VERSION-arm64.dmg`

---

## Required Tools & Paths

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

**Default Installation Path:**
- **macOS:** `~/HISE`

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
  - Detect macOS version
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
  [TEST MODE] Platform detected: macOS
  [TEST MODE] OS version: macOS 13.0 - COMPATIBLE
  [TEST MODE] Would execute: git clone https://github.com/christophhart/HISE.git
  ```

### Step 2: System Check & Platform Detection
- Verify macOS compatibility
- **Detect CPU architecture**
- Check disk space requirements (~2-5 GB)
- Detect existing installations

**Architecture Detection Commands:**
- **macOS:** `uname -m` (returns `x86_64` for Intel, `arm64` for Apple Silicon)

**Test Mode:**
```
[TEST MODE] Platform detected: macOS
[TEST MODE] OS version: {version} - {COMPATIBLE/NOT COMPATIBLE}
[TEST MODE] CPU architecture: {x86_64|arm64}
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
[TEST MODE] If not installed, would execute: xcode-select --install
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

**All platforms:**
- **Prompt:** "Install Faust DSP programming language? [Y/n]"
  - Yes: Install based on platform/architecture, use `ReleaseWithFaust` build configuration
  - No: Skip Faust installation, use `Release` build configuration

### Step 4a: Faust Installation (If Selected)

> **Faust Download URL:** https://github.com/grame-cncm/faust/releases
> **Recommended Version:** 2.54.0 or later

**macOS Faust Installation:**
1. Detect CPU architecture: `uname -m` (returns `x86_64` or `arm64`)
2. Direct user to download the correct DMG from https://github.com/grame-cncm/faust/releases:
   - Intel Mac: `Faust-VERSION-x64.dmg`
   - Apple Silicon: `Faust-VERSION-arm64.dmg`
3. Instruct user to open the DMG and extract **all folders** to `{hisePath}/tools/faust/`:
   - `{hisePath}/tools/faust/include/`
   - `{hisePath}/tools/faust/lib/`
   - `{hisePath}/tools/faust/bin/`
   - `{hisePath}/tools/faust/share/`
4. **WAIT** for user to confirm extraction is complete
5. Verify installation:
   ```bash
   test -f "{hisePath}/tools/faust/lib/libfaust.dylib" && echo "Faust installed successfully" || echo "Faust installation not found"
   ```
6. **If verification fails:** Re-prompt user to complete extraction, do NOT proceed
7. Modify the .jucer file to build for **single architecture only**:

   **Apple Silicon:**
   ```bash
   sed -i '' 's/xcodeValidArchs="arm64,arm64e,x86_64"/xcodeValidArchs="arm64"/' "{hisePath}/projects/standalone/HISE Standalone.jucer"
   ```

   **Intel Mac:**
   ```bash
   sed -i '' 's/xcodeValidArchs="arm64,arm64e,x86_64"/xcodeValidArchs="x86_64"/' "{hisePath}/projects/standalone/HISE Standalone.jucer"
   ```

8. First run: macOS Gatekeeper may block unsigned Faust libraries
   - Inform user: Go to System Preferences → Security & Privacy → Allow

**Normal Mode (macOS Faust) - Waiting State:**
```
Faust DSP is required for the selected build configuration.

Detected architecture: {arm64|x86_64}

Please complete the following steps:
1. Download Faust from: https://github.com/grame-cncm/faust/releases
   - Download file: Faust-X.XX.X-{arm64|x64}.dmg
2. Open the DMG file
3. Copy ALL folders (include, lib, bin, share) to:
   {hisePath}/tools/faust/
4. The fakelib folder should remain (do not delete it)

Press Enter when extraction is complete, or type 'skip' to build without Faust...
```

After user confirms, verify installation:
- Check: `{hisePath}/tools/faust/lib/libfaust.dylib` exists
- If not found: Display error and re-prompt
- If found: Modify .jucer file and proceed with `Release with Faust` build configuration

**Test Mode (macOS Faust):**
```
[TEST MODE] Step 4a: Faust Installation
[TEST MODE] Executing: uname -m (detecting architecture)
[TEST MODE] Result: {x86_64|arm64}
[TEST MODE] Would direct user to: https://github.com/grame-cncm/faust/releases
[TEST MODE] Would instruct: Download Faust-VERSION-{x64|arm64}.dmg
[TEST MODE] Would instruct: Extract all folders to {hisePath}/tools/faust/
[TEST MODE] Would WAIT for user confirmation
[TEST MODE] Executing: test -f "{hisePath}/tools/faust/lib/libfaust.dylib"
[TEST MODE] Result: libfaust.dylib {found|not found}
[TEST MODE] Would modify: projects/standalone/HISE Standalone.jucer
[TEST MODE]   - Change xcodeValidArchs="arm64,arm64e,x86_64" to xcodeValidArchs="{arm64|x86_64}"
```

**Post-Installation (macOS):**
- After HISE is built and running, set the `FaustPath` in HISE Settings:
  - macOS: `{hisePath}/tools/faust/`
- This allows HISE to find Faust libraries for DSP compilation

### Step 5: IDE & Compiler Setup (REQUIRED)

> **IMPORTANT:** This step is **mandatory** and cannot be skipped. HISE requires a C++ compiler to build. The setup process must wait for the IDE/compiler installation to complete before proceeding.

- Check if Xcode is installed
- If not installed:
  1. Direct user to install Xcode from Mac App Store
  2. Install Command Line Tools: `xcode-select --install`
  3. **WAIT** for user to complete installation before proceeding
  4. Do NOT offer option to skip this step
- Accept Xcode license (automatic)

**Test Mode (macOS):**
```
[TEST MODE] Step 5: IDE & Compiler Setup (REQUIRED)
[TEST MODE] Executing: xcode-select -p (checking Xcode path)
[TEST MODE] Executing: which xcodebuild
[TEST MODE] Executing: xcodebuild -version
[TEST MODE] Result: Xcode {version} {installed/not installed}
[TEST MODE] If not installed: HALT and instruct user to install Xcode
[TEST MODE] This step is MANDATORY - do not offer skip option
```

**Normal Mode:**
- **High-level log:** "Checking IDE and compiler tools (REQUIRED)..."
- **HALT** if compiler not available - do not proceed to next step
- **NO SKIP OPTION** - user must install required tools to continue

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
# Timeout: 600000ms (10 minutes) - compilation takes 5-15 minutes
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration Release -jobs $CORES | "{hisePath}/tools/Projucer/xcbeautify"
```

**Normal Mode (macOS - with Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer.app/Contents/MacOS/Projucer" --resave "HISE Standalone.jucer"
# Detect CPU cores and use for parallel compilation
CORES=$(sysctl -n hw.ncpu)
# Timeout: 600000ms (10 minutes) - compilation takes 5-15 minutes
xcodebuild -project Builds/MacOSX/HISE.xcodeproj -configuration "Release with Faust" -jobs $CORES | "{hisePath}/tools/Projucer/xcbeautify"
```

- **High-level log:** "Compiling HISE Standalone application..."

### Step 9: Add HISE to PATH

**Path Selection:**
- If Faust was installed: Use `ReleaseWithFaust` output directory
- If Faust was not installed: Use `Release` output directory

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
[TEST MODE] Executing: test -f {hisePath}/projects/standalone/Builds/MacOSX/build/{Release|Release with Faust}/HISE.app/Contents/MacOS/HISE
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
- **Architecture:** 64-bit (x64 or arm64 based on system)
- **JUCE Version:** juce6 (stable)

### Build Configuration Selection
| Faust Installed | Platform | Configuration          | Output Directory                         |
|-----------------|----------|------------------------|------------------------------------------|
| No              | macOS    | `Release`              | `.../build/Release/`                     |
| Yes             | macOS    | `"Release with Faust"` | `.../build/Release with Faust/`          |

### Platform-Specific Build Commands

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

---

## Error Handling

### Common Issues & Solutions

**Git not found:**
- macOS: `xcode-select --install`

**Xcode not found (macOS):**
- Direct to Mac App Store
- Install Command Line Tools

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
- Verify shell configuration file (`.zshrc`, `.bash_profile`)
- Restart shell or source configuration file

**Build failures:**
- Check compiler versions
- Verify SDK paths
- Review build output
- **ABORT** if non-trivial failure

**Faust installation failed (macOS):**
- Verify correct architecture DMG was downloaded (`x64` for Intel, `arm64` for Apple Silicon)
- Check that all folders were extracted to `{hisePath}/tools/faust/`:
  - `include/`, `lib/`, `bin/`, `share/`
- Verify `libfaust.dylib` exists in `{hisePath}/tools/faust/lib/`
- If Gatekeeper blocks libraries: System Preferences → Security & Privacy → Allow

**Faust build fails with architecture mismatch (macOS):**
- Error: `building for macOS-x86_64 but attempting to link with file built for macOS-arm64` (or vice versa)
- **Solution:** Edit the .jucer file to set single architecture:
  ```bash
  # For Apple Silicon (arm64 Faust):
  sed -i '' 's/xcodeValidArchs="[^"]*"/xcodeValidArchs="arm64"/' "projects/standalone/HISE Standalone.jucer"

  # For Intel Mac (x64 Faust):
  sed -i '' 's/xcodeValidArchs="[^"]*"/xcodeValidArchs="x86_64"/' "projects/standalone/HISE Standalone.jucer"
  ```
- Re-run Projucer to regenerate Xcode project: `"{hisePath}/JUCE/extras/Projucer/Builds/MacOSX/build/Release/Projucer.app/Contents/MacOS/Projucer" --resave "projects/standalone/HISE Standalone.jucer"`
- Rebuild HISE

**Faust version too old:**
- HISE requires Faust 2.54.0 or later
- If using older version (e.g., 2.50.6), enable `HI_FAUST_NO_WARNING_MESSAGES` flag in Projucer
- Recommended: Update to latest Faust release

**Test project compilation fails:**
- Verify demo project exists at `extras/demo_project/`
- Check all SDK paths
- Review error messages
- Provide specific troubleshooting steps

---

## User Interaction Model

### Interactive Prompts
- **Test Mode (Step 0):** "Run in test mode? (y/n)"
- Confirm optional component installation (Faust)
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
  - `which` / `ls` commands (path lookups)
  - `test -f` / `test -d` (file/directory existence checks)
  - Environment variable reads (`echo $PATH`)
- **Skips (shows with `[TEST MODE] Would execute:` prefix):**
  - All installations (`brew install`, downloads)
  - Git clone/checkout operations
  - File extractions (unzip)
  - Build commands (xcodebuild, make)
  - PATH modifications (shell config writes)
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
2. IDE/compiler is installed and functional (Xcode)
3. JUCE submodule is initialized with **juce6 branch**
4. Required SDKs are extracted and configured
5. HISE compiles from `projects/standalone/HISE Standalone.jucer` without errors
6. HISE binary is added to PATH environment variable
7. `HISE --help` displays available CLI commands
8. Test project from `extras/demo_project/` compiles successfully
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
- libc++ (LLVM C++ Standard Library)
- macOS: Clang via Xcode

### Project Files
- HISE Standalone: `projects/standalone/HISE Standalone.jucer`
- Test Project: `extras/demo_project/XmlPresetBackups/Demo.xml`
- Build Tool: Projucer (from JUCE submodule)
- JUCE Branch: juce6 (stable, only option)

### Build Artifacts

**Without Faust (Release configuration):**
- macOS: `projects/standalone/Builds/MacOSX/build/Release/HISE.app/Contents/MacOS/HISE`

**With Faust ("Release with Faust" / ReleaseWithFaust configuration):**
- macOS: `projects/standalone/Builds/MacOSX/build/Release with Faust/HISE.app/Contents/MacOS/HISE`

> **Note:** On macOS, the HISE binary is located inside the `.app` bundle. The PATH must include the full path to `HISE.app/Contents/MacOS` for the `HISE` command to be accessible from the terminal.

### Environment Variables
- **PATH:** Includes HISE binary directory for command-line access
- **Compiler Settings:** Stored in compilerSettings.xml (macOS)

### Optional Features
- Faust JIT compiler
- Perfetto profiling (disabled by default)
- Loris, RLottie (Minimal build excludes these)

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
