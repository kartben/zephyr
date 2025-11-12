# Zephyr RTOS Development Guide for Copilot Agents

## Repository Overview

**Zephyr** is a scalable real-time operating system (RTOS) supporting multiple hardware architectures (ARM, x86, ARC, RISC-V, Xtensa, SPARC, MIPS), optimized for resource-constrained devices. Version 4.3.0-rc2.

- **Size**: Large-scale project with ~150 board vendors, 50+ subsystems, extensive HAL modules
- **Languages**: C (primary), Python (scripts/tools), CMake (build system), Assembly
- **Build System**: CMake + West (meta-tool)
- **Testing**: Twister (custom test runner)
- **Key Standards**: MISRA-C 2012 subset, Apache 2.0 license, DCO sign-off required

## Critical Build & Environment Setup

### Prerequisites (ALWAYS verify these first)
- **Python**: 3.10+ (current: 3.12)
- **CMake**: 3.20.5+
- **West**: Install via `pip install west` (meta-tool for Zephyr)
- **Device Tree Compiler**: 1.4.6+

### Environment Initialization (REQUIRED for all operations)

**ALWAYS run these commands in order before any build/test operations:**

```bash
# 1. Install Python dependencies (choose appropriate file)
pip install -r scripts/requirements-base.txt          # Core dependencies
pip install -r scripts/requirements-compliance.txt    # For compliance checks
pip install -r scripts/requirements-build-test.txt    # For building/testing

# 2. Initialize west workspace (REQUIRED - run from repo root)
west init -l .

# 3. Set up environment (adds scripts to PATH)
source zephyr-env.sh  # Linux/macOS
# OR
zephyr-env.cmd        # Windows

# 4. Update west projects (if needed for full builds)
west update -o=--depth=1 -n
```

**Common Error**: If `west` command not found after pip install, add to PATH:
```bash
export PATH="$HOME/.local/bin:$PATH"
```

## Building Applications

### Basic Build Pattern (west build)

```bash
# From application directory (e.g., samples/hello_world)
west build -b <BOARD>

# Specify build directory explicitly
west build -b <BOARD> --build-dir build/custom

# Clean rebuild (force CMake rerun)
west build -c -b <BOARD>

# Multi-domain builds with sysbuild
west build --sysbuild -b <BOARD>
```

**Board Examples**: `qemu_x86`, `qemu_cortex_m3`, `native_sim`, `reel_board`, `frdm_k64f`

**Critical**: Board name is REQUIRED for new builds. For incremental builds in existing build directory, board is detected automatically.

### Build from Specific Source Directory

```bash
west build -b <BOARD> path/to/source/directory
```

### Common Build Targets

```bash
west build -t run        # Build and run in emulator (QEMU/native_sim)
west build -t flash      # Build and flash to hardware
west build -t debug      # Build and start debugger
```

## Testing with Twister

### Basic Test Execution

```bash
# Run all tests for specific platform
./scripts/twister --platform <BOARD>

# Build-only mode (no execution)
./scripts/twister --build-only

# Run tests for specific test path
./scripts/twister -T tests/kernel/threads

# Verbose output
./scripts/twister -v --platform <BOARD>
```

**Test Discovery**: Twister automatically scans for `testcase.yaml` or `sample.yaml` files in `tests/` and `samples/` directories.

**Time Consideration**: Full test suite (`--all --enable-slow`) can take hours. Use targeted testing for development.

### Test Configuration Files
- `tests/test_config.yaml` - Test levels/categories
- `tests/test_config_ci.yaml` - CI-specific config
- Board-specific test configs in `boards/<vendor>/<board>/board.yaml`

## Compliance & Code Quality Checks

### Running Compliance Checks (CRITICAL before PR)

```bash
# Set ZEPHYR_BASE (if not already set)
export ZEPHYR_BASE=$PWD

# Run compliance against base branch
./scripts/ci/check_compliance.py -c origin/main..

# Exclude specific checks (if needed)
./scripts/ci/check_compliance.py -e KconfigBasic -e ClangFormat -c origin/main..

# Generate annotated output
./scripts/ci/check_compliance.py --annotate -c origin/main..
```

**Compliance Checks Include**:
- Checkpatch (kernel coding style)
- Gitlint (commit message format)
- SPDX license tags
- YAML linting
- Device tree binding validation
- Maintainer validation
- Identity (Signed-off-by required)

**Common Compliance Failures**:
1. Missing `Signed-off-by` line in commits
2. Commit subject line format must be: `subsystem: short description`
3. Max line length 100 chars (code), 75 chars (commit messages)
4. No merge commits allowed (rebase instead)

### Code Style Tools

```bash
# Python linting (Ruff)
ruff check .                    # Check for issues
ruff format .                   # Auto-format

# Checkpatch (C code style)
./scripts/checkpatch.pl --no-tree -f <file>

# ClangFormat (NOT enforced in CI but available)
clang-format -i <file>
```

**Style Configuration Files**:
- `.ruff.toml` + `.ruff-excludes.toml` - Python (100 char line length)
- `.checkpatch.conf` - C code style
- `.clang-format` - ClangFormat config (optional)
- `.editorconfig` - Editor settings (tabs for C/C++, spaces for Python/CMake)

### Coding Standards
- **C Code**: Tabs (8 spaces), MISRA-C 2012 subset
- **Python**: Spaces (4), line length 100
- **CMake**: Spaces (2)
- **YAML**: Spaces (2)
- **Device Tree**: Tabs (8 spaces)

## Repository Structure

### Root Directory Key Files
- `CMakeLists.txt` - Top-level build config (NOT for direct invocation)
- `Kconfig.zephyr` - Main Kconfig entry point
- `west.yml` - West manifest defining all modules/HALs
- `VERSION` - Version information (4.3.0-rc2)
- `zephyr-env.sh/cmd` - Environment setup scripts

### Major Directories

**Core Source Code**:
- `arch/` - Architecture-specific code (arm, arm64, x86, riscv, xtensa, etc.)
- `kernel/` - Zephyr kernel implementation
- `drivers/` - Device drivers (94 subdirectories)
- `lib/` - Core libraries (libc, heap, os, utils, etc.)
- `subsys/` - Subsystems (bluetooth, net, usb, fs, shell, logging, etc.)
- `include/` - Public header files

**Board Support**:
- `boards/` - Board definitions organized by vendor (148 vendors)
- `soc/` - SoC-specific code (55 SoC families)
- `dts/` - Device tree source files and bindings

**Build & Configuration**:
- `cmake/` - CMake modules and toolchain files
- `modules/` - Integration with external modules/HALs
- `Kconfig*` - Configuration system files

**Testing & Samples**:
- `tests/` - Comprehensive test suite (25+ categories)
- `samples/` - Sample applications (24 categories)

**Scripts & Tools**:
- `scripts/` - Build scripts, twister, compliance tools
- `scripts/ci/` - CI-specific scripts
- `scripts/west-commands.yml` - West command extensions
- `doc/` - Documentation source (reStructuredText)

### Configuration Files in Root
- `.checkpatch.conf` - Checkpatch style rules
- `.ruff.toml` - Python linter config
- `.editorconfig` - Editor formatting
- `.gitlint` - Commit message linting
- `.yamllint` - YAML linting
- `.codecov.yml` - Code coverage config

## GitHub Workflows & CI

### Main PR Checks (must pass before merge)

1. **Compliance** (`.github/workflows/compliance.yml`)
   - Runs on all PRs
   - Checks: commit format, code style, licenses, maintainer files
   - Must rebase on target branch (no merge commits)
   - Command: `./scripts/ci/check_compliance.py`

2. **Twister Tests** (`.github/workflows/twister.yaml`)
   - Builds subset of tests based on changed files
   - Matrix of 10-20 parallel jobs (configurable)
   - Tests selected architectures and boards
   - Full run on main branch, selective on PRs

3. **Documentation Build** (`.github/workflows/doc-build.yml`)
   - Validates documentation builds correctly
   - Checks for Sphinx warnings/errors

4. **Additional Checks**:
   - CodeQL security scanning
   - Python library tests (`pylib_tests.yml`)
   - License checking (`license_check.yml`)
   - West command tests (`west_cmds.yml`)

### Replicating CI Locally

```bash
# 1. Compliance (most critical)
export ZEPHYR_BASE=$PWD
git config diff.renameLimit 10000
./scripts/ci/check_compliance.py -c origin/main..

# 2. Build affected tests (mimics CI)
./scripts/ci/test_plan.py -c origin/main.. --pull-request

# 3. Run selected tests
./scripts/twister --platform native_sim --platform qemu_x86
```

## Common Development Workflows

### Adding New Driver
1. Place code in `drivers/<subsystem>/`
2. Add Kconfig entries in `drivers/<subsystem>/Kconfig.<driver>`
3. Update `drivers/<subsystem>/CMakeLists.txt`
4. Add device tree bindings in `dts/bindings/<subsystem>/`
5. Add tests in `tests/drivers/<subsystem>/`
6. Update MAINTAINERS.yml if needed

### Adding New Board
1. Create directory: `boards/<vendor>/<board>/`
2. Required files: `board.yml`, `<board>_defconfig`, `board.cmake`
3. Device tree: `<board>.dts` or `<board>.dtsi`
4. Documentation: `doc/index.rst`
5. Test on board with twister

### Modifying Kernel Code
1. Kernel sources in `kernel/`
2. Headers in `include/zephyr/kernel/`
3. Architecture-specific in `arch/<arch>/core/`
4. ALWAYS run kernel tests: `./scripts/twister -T tests/kernel/`

## Known Issues & Workarounds

### West Update Failures
If `west update` fails, retry with increased verbosity:
```bash
west update -o=--depth=1 -n 2>&1 | tee west.update.log
west update -o=--depth=1 -n  # Retry if first attempt fails
```

### Build Directory Conflicts
Clean build directory if strange CMake errors occur:
```bash
rm -rf build/
west build -c -b <BOARD>
```

### Git Rebase Required
CI enforces rebase workflow. If you have merge commits:
```bash
git rebase -i origin/main  # Interactive rebase to clean up
```

## Quick Reference Commands

```bash
# Setup (first time)
pip install west
west init -l .
source zephyr-env.sh
pip install -r scripts/requirements-base.txt

# Build sample
cd samples/hello_world
west build -b qemu_x86

# Run in QEMU
west build -t run

# Test specific component
./scripts/twister -T tests/kernel/threads --platform native_sim

# Check compliance
export ZEPHYR_BASE=$PWD
./scripts/ci/check_compliance.py -c origin/main..

# List available boards
west boards

# Clean everything
west build -t pristine
```

## Important Notes for Agents

1. **ALWAYS source `zephyr-env.sh`** before building - it sets critical environment variables
2. **Install correct requirements file** for your task (base/compliance/build-test)
3. **Board name is mandatory** for new builds
4. **Compliance checks must pass** - run before finalizing changes
5. **No merge commits** - use rebase workflow
6. **Signed-off-by required** in all commits
7. **Line length**: 100 chars for code, 75 for commit messages
8. **Test locally** with twister before pushing
9. **Follow subsystem naming** in commit subjects (e.g., "drivers: serial: fix bug")
10. **Device tree changes** require binding validation

Trust these instructions for standard workflows. Only search/explore when encountering edge cases not covered here or when working with uncommon architectures/boards.
