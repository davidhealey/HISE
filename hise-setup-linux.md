---
name: hise-setup-linux
description: Setup Linux computer to work with HISE.
---

# HISE Development Environment Setup - Linux

## Overview
Automates complete development environment setup for HISE (Hart Instrument Software Environment) on Linux.

## Supported Platforms
- **Linux (Ubuntu 16.04 LTS tested, x64 only)**

---

## Core Capabilities

### 1. Platform Detection
- Automatically detects Linux distribution
- Verifies OS version compatibility
- **Detects CPU architecture** (x64)
- Adjusts setup workflow based on platform

### 2. Test Mode Operation
- **Test Mode:** Validates environment without making changes
  - **Executes:** All non-modifying commands (read-only operations)
    - `git --version` (check if Git is installed)
    - `which gcc` / `gcc --version` (check compiler)
    - `ls` / `test -f` / `test -d` (check file/directory existence)
    - Platform detection and OS version checks
    - Disk space checks
  - **Skips:** All modifying commands (shows what would be executed)
    - Installations (`apt-get install`, etc.)
    - Git operations that change state (`git clone`, `git checkout`, etc.)
    - File modifications (unzip, write, delete)
    - Build commands (make)
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

- Installs dependencies: build-essential, LLVM/Clang, and GUI libraries
- Ensures GCC/G++ ≤11 (**ABORTS** if newer version detected)
- Optionally installs mold linker for faster builds
- **Cannot proceed without GCC ≤11**

### 5. SDK & Tool Installation

**Required SDKs:**
- Extracts and configures VST3 SDK (all platforms)

**Optional (User Prompted):**
- **Faust DSP programming language**
  - User prompted before installation
  - Install via package manager or build from source
  - Requires Faust version 2.54.0+ (recommended)

### 6. HISE Compilation
- Compiles HISE from `projects/standalone/HISE Standalone.jucer`
- Uses Projucer (from JUCE submodule) to generate IDE project files
- Compiles HISE using Makefile (Release)
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

### Linux Specific
- Dependencies installed via `apt-get` from Ubuntu repositories

### All Platforms
- **Faust DSP Language (2.54.0+):** https://github.com/grame-cncm/faust/releases
  - Linux: Use package manager or build from source

---

## Required Tools & Paths

### Linux Build Tools
1. **Projucer** (from JUCE submodule)
   - Path: `{hisePath}/JUCE/Projucer/Projucer`

2. **Make** (via build-essential)
   - Path: `{buildPath}/Builds/LinuxMakefile/`

3. **GCC/Clang** toolchain

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
- **Linux:** `~/HISE`

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
  - Detect Linux distribution
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
  [TEST MODE] Platform detected: Linux
  [TEST MODE] OS version: Ubuntu 22.04 - COMPATIBLE
  [TEST MODE] Would execute: git clone https://github.com/christophhart/HISE.git
  ```

### Step 2: System Check & Platform Detection
- Verify Linux compatibility
- **Detect CPU architecture**
- Check disk space requirements (~2-5 GB)
- Detect existing installations

**Architecture Detection Commands:**
- **Linux:** `uname -m` (returns `x86_64` for x64)

**Test Mode:**
```
[TEST MODE] Platform detected: Linux
[TEST MODE] OS version: {version} - {COMPATIBLE/NOT COMPATIBLE}
[TEST MODE] CPU architecture: {x86_64}
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
[TEST MODE] If not installed, would execute: sudo apt-get install git
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

**Linux Faust Installation:**
1. Check if Faust is already installed:
   ```bash
   which faust && faust --version
   ldconfig -p | grep faust
   ```
2. **If not installed:** Install via package manager (requires sudo):
   ```bash
   # Debian/Ubuntu
   sudo apt-get install faust libfaust-dev

   # Arch Linux
   sudo pacman -S faust
   ```
3. **If package not available:** Build from source:
   ```bash
   git clone https://github.com/grame-cncm/faust.git
   cd faust
   make
   sudo make install
   sudo ldconfig  # Update library cache
   ```
4. **WAIT** if installation requires user action (sudo password, confirmation)
5. Verify installation:
   ```bash
   faust --version        # Should show 2.54.0 or later
   ldconfig -p | grep faust  # Should show libfaust
   ```
6. **If verification fails:** Display error with installation instructions, do NOT proceed
7. **No .jucer modifications needed** - Linux configuration links to system `faust` library via `externalLibraries="faust"`

**Normal Mode (Linux Faust) - If Not Installed:**
```
Faust DSP is required for the selected build configuration.

Faust is not currently installed on this system.

To install Faust, run one of the following commands:

  # Debian/Ubuntu:
  sudo apt-get install faust libfaust-dev

  # Arch Linux:
  sudo pacman -S faust

  # Or build from source:
  git clone https://github.com/grame-cncm/faust.git
  cd faust && make && sudo make install && sudo ldconfig

Press Enter when installation is complete, or type 'skip' to build without Faust...
```

After user confirms, verify installation:
- Check: `which faust` returns a path
- Check: `ldconfig -p | grep faust` shows libfaust
- If not found: Display error and re-prompt
- If found: Proceed with `ReleaseWithFaust` build configuration

**Test Mode (Linux Faust):**
```
[TEST MODE] Step 4a: Faust Installation
[TEST MODE] Executing: which faust (checking if Faust is installed)
[TEST MODE] Executing: faust --version (checking Faust version)
[TEST MODE] Executing: ldconfig -p | grep faust (checking if libfaust is available)
[TEST MODE] Result: Faust {installed version X.X.X | not installed}
[TEST MODE] Result: libfaust {found | not found}
[TEST MODE] If not installed, would prompt user with installation instructions
[TEST MODE] Would WAIT for user confirmation
[TEST MODE] No .jucer modifications required for Linux
```

**Post-Installation (Linux):**
- After HISE is built and running, set the `FaustPath` in HISE Settings:
  - Linux: Path to Faust installation (e.g., `/usr/local/`)
- This allows HISE to find Faust libraries for DSP compilation

### Step 5: IDE & Compiler Setup (REQUIRED)

> **IMPORTANT:** This step is **mandatory** and cannot be skipped. HISE requires a C++ compiler to build. The setup process must wait for the IDE/compiler installation to complete before proceeding.

- Check if GCC/build tools are installed
- If not installed: Install dependencies (automatic with sudo)
- **ABORT if GCC version > 11** (incompatible)

**Test Mode (Linux):**
```
[TEST MODE] Step 5: IDE & Compiler Setup (REQUIRED)
[TEST MODE] Executing: which gcc
[TEST MODE] Executing: gcc --version
[TEST MODE] Executing: which clang
[TEST MODE] Executing: clang --version
[TEST MODE] Result: GCC version {X.X} {≤11 OK / >11 ABORT}
[TEST MODE] If not installed: Install via apt-get (MANDATORY)
[TEST MODE] This step is MANDATORY - do not offer skip option
[TEST MODE] Would execute: sudo apt-get -y install build-essential make llvm clang libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev mesa-common-dev libasound2-dev freeglut3-dev libxcomposite-dev libcurl4-gnutls-dev libgtk-3-dev libjack-jackd2-dev libwebkit2gtk-4.0-dev libpthread-stubs0-dev ladspa-sdk
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
# Timeout: 600000ms (10 minutes) - compilation takes 5-15 minutes
make CONFIG=Release AR=gcc-ar -j$(nproc --ignore=2)
```

**Normal Mode (Linux - with Faust):**
```bash
cd projects/standalone
"{hisePath}/JUCE/Projucer/Projucer" --resave "HISE Standalone.jucer"
cd Builds/LinuxMakefile
# Use all cores minus 2 to keep system responsive
# Timeout: 600000ms (10 minutes) - compilation takes 5-15 minutes
make CONFIG=ReleaseWithFaust AR=gcc-ar -j$(nproc --ignore=2)
```

- **High-level log:** "Compiling HISE Standalone application..."

### Step 9: Add HISE to PATH

**Path Selection:**
- If Faust was installed: Use `ReleaseWithFaust` output directory
- If Faust was not installed: Use `Release` output directory

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
[TEST MODE] Executing: test -f {hisePath}/projects/standalone/Builds/LinuxMakefile/build/HISE
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

### Build Configuration Selection
| Faust Installed | Platform | Configuration          | Output Directory                         |
|-----------------|----------|------------------------|------------------------------------------|
| No              | Linux    | `Release`              | `.../build/`                             |
| Yes             | Linux    | `ReleaseWithFaust`     | `.../build/`                             |

### Platform-Specific Build Commands

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
- Linux: `sudo apt-get install git`

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

**HISE not in PATH after setup:**
- Check PATH configuration
- Verify shell configuration file (`.bashrc`, `.zshrc`)
- Restart shell or source configuration file

**Build failures:**
- Check compiler versions
- Verify SDK paths
- Review build output
- **ABORT** if non-trivial failure

**Faust not found (Linux):**
- Check if Faust is in PATH: `which faust`
- Check if libfaust is available: `ldconfig -p | grep faust`
- If not installed, install via package manager:
  ```bash
  sudo apt-get install faust libfaust-dev  # Debian/Ubuntu
  sudo pacman -S faust                      # Arch Linux
  ```
- If libfaust not found but faust command exists, you may need to build from source with `make install`
- Verify version is 2.54.0+: `faust --version`

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
  - All installations (`apt-get install`, downloads)
  - Git clone/checkout operations
  - File extractions (unzip)
  - Build commands (make)
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
2. IDE/compiler is installed and functional (GCC ≤11 or Clang)
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
- Linux: GCC ≤11 or Clang

### Project Files
- HISE Standalone: `projects/standalone/HISE Standalone.jucer`
- Test Project: `extras/demo_project/XmlPresetBackups/Demo.xml`
- Build Tool: Projucer (from JUCE submodule)
- JUCE Branch: juce6 (stable, only option)

### Build Artifacts

**Without Faust (Release configuration):**
- Linux: `projects/standalone/Builds/LinuxMakefile/build/HISE`

**With Faust ("Release with Faust" / ReleaseWithFaust configuration):**
- Linux: `projects/standalone/Builds/LinuxMakefile/build/HISE`

### Environment Variables
- **PATH:** Includes HISE binary directory for command-line access

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
- Requires GCC ≤11 (newer versions are incompatible)
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
