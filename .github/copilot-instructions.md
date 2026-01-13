# GitHub Copilot Coding Agent Instructions for Zephyr RTOS

## Project Overview

**Zephyr** is a scalable real-time operating system (RTOS) supporting multiple hardware architectures (ARM Cortex-A/R/M, x86, ARC, RISC-V, Xtensa, SPARC, MIPS, etc.), optimized for resource-constrained embedded devices. The project uses C (primary), Python (build/test infrastructure), CMake, Kconfig, and Devicetree (DTS) for hardware description.

**Repository Size**: Large (100s of board configurations, extensive driver support, comprehensive subsystems)
**License**: Apache 2.0 (with some components under other licenses, clearly marked)
**Build System**: CMake + West (meta-tool for multi-repository management)
**Primary Language**: C (C89 style with some C99 features)
**Configuration**: Kconfig-based
**Hardware Description**: Devicetree (DTS)

---

## Critical Contribution Requirements

### 1. Developer Certificate of Origin (DCO)

**MANDATORY**: Every commit MUST include a `Signed-off-by` line:

```
Signed-off-by: Your Full Name <your.email@example.com>
```

- Use `git commit -s` to add automatically
- Legal name required (no pseudonyms)
- Email must match Git commit author email (CI will fail otherwise)
- If modifying others' commits, add your Signed-off-by without removing existing ones

### 2. Commit Message Format

**REQUIRED FORMAT** (CI will fail if not followed):

```
[area]: [summary of change]

[Non-empty commit message body explaining what, why, and how]

Signed-off-by: Your Full Name <your.email@example.com>
```

**Rules**:

- Title: Max 72 characters, followed by blank line
- Area prefix examples: `drivers: sensor:`, `Bluetooth: Shell:`, `net: ethernet:`, `doc:`, `dts:`, `arch: arm:`, `boards:`, `tests:`
- Body: Required (even for trivial changes), 75 chars/line, explain what/why/how
- Use `Fixes #1234` or `Fixes zephyrproject-rtos/zephyr#1234` to auto-close issues

### 3. Copyright and License Headers

**All new files MUST include**:

```c
// For C files:
/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
```

Place at the very top using the file's native comment syntax.

---

## Coding Style Guidelines (STRICTLY ENFORCED)

### C Code Style (doc/contribute/style/code.rst)

Follow Linux kernel coding style with Zephyr exceptions:

- **Tabs**: 8-character tabs for indentation (NOT spaces)
- **Line Length**: 100 columns max
- **Naming**: snake_case for functions, variables
- **Braces**: ALWAYS use braces for `if`, `else`, `for`, `while`, `do`, `switch` (even single-line)
- **Comments**: C89-style `/* */` only (NO `//` comments)
- **Doxygen**: Use `/** */` for API documentation
- **Alignment**: Use spaces (not tabs) for aligning comments after declarations
- **Binary literals**: Avoid `0b` prefix
- **Proper nouns**: Capitalize in comments (e.g., `UART` not `uart`, `CMake` not `cmake`)

**Format with clang-format**:

```bash
clang-format -i <file.c>
```

Configuration in `.clang-format`. When guidelines conflict with clang-format output, guidelines take precedence.

### Python Style (doc/contribute/style/python.rst)

- **PEP 8 compliant** via `ruff` formatter
- **Line length**: 100 characters max
- **Format commands**:
  ```bash
  ruff check --select I --fix <file.py>  # Sort imports
  ruff format <file.py>                   # Format code
  ```

### CMake Style (doc/contribute/style/cmake.rst)

- **Indentation**: 2 spaces (no tabs)
- **Commands**: Lowercase (e.g., `add_library`, not `ADD_LIBRARY`)
- **Line length**: 100 characters
- **No space before parentheses**: `if(...)` not `if (...)`
- **Variables**: UPPERCASE for cache/shared, lowercase for local
- **Quote all strings and variables**

### Devicetree Style (doc/contribute/style/devicetree.rst)

- **Indentation**: Tabs (8 characters)
- **Follow**: [Linux DTS Coding Style](https://docs.kernel.org/devicetree/bindings/dts-coding-style.html)
- **Node names**: Use dashes `-` as separators
- **Node labels**: Use underscores `_` as separators
- **Property assignment**: Space around `=` (e.g., `prop = <value>;`)
- **Format with dts-linter**:
  ```bash
  npx --prefix ./scripts/ci dts-linter --format --file <board.dts> --patchFile diff.patch
  git apply diff.patch
  ```

### Kconfig Style (doc/contribute/style/kconfig.rst)

- **Indentation**: Tabs (help text: 1 tab + 2 spaces)
- **Line length**: 100 columns
- **Spacing**: Empty line between options
- **Symbol naming**: Use common prefixes (e.g., `CONFIG_SENSOR_BME280`)
- **Drivers**: Format `{Driver Type}_{Driver Name}`, prompt `{Driver Name} {Driver Type} driver`

---

## Build System and Workflow

### Prerequisites

**Install west** (Zephyr meta-tool):

```bash
pip install west
```

**Initial setup** (from outside Zephyr directory):

```bash
west init -l zephyr/    # If already cloned
west update             # Fetch all modules
west zephyr-export      # Register Zephyr as CMake package
```

### Building Applications

**Standard build command**:

```bash
west build -b <board> <application_path>
# Example: west build -b qemu_x86 samples/hello_world
```

**Clean build**:

```bash
west build -b <board> <application_path> -p auto  # Or -p always
```

**With Sysbuild** (for multi-image builds):

```bash
west build -b <board> <application_path> --sysbuild
```

**Build and run**:

```bash
west build -b qemu_x86 samples/hello_world -t run
```

**Flash to hardware**:

```bash
west flash
```

### Testing with Twister

**Twister** is Zephyr's test runner. Test definitions are in `testcase.yaml` or `sample.yaml` files.

**Run all tests for a board**:

```bash
./scripts/twister -p qemu_x86
```

**Run specific tests**:

```bash
./scripts/twister -T tests/kernel/
```

**Run with hardware**:

```bash
./scripts/twister -p <board> --device-testing
```

---

## CI/CD and Compliance Checks

### Continuous Integration (GitHub Actions)

**All PRs trigger**:

1. **Compliance checks** (`.github/workflows/compliance.yml`)
2. **Twister tests** (`.github/workflows/twister.yaml`)
3. **Documentation build** (`.github/workflows/doc-build.yml`)
4. **Coding guidelines check** (`.github/workflows/coding_guidelines.yml`)

**Common CI failure reasons**:

- Missing `Signed-off-by` line
- Incorrect commit message format (empty body, wrong title format)
- Coding style violations (tabs vs spaces, line length)
- Checkpatch errors
- Email mismatch between Git config and Signed-off-by
- Merge commits in PR (rebase required)

### Local Compliance Checking (CRITICAL)

**ALWAYS run before submitting PR**:

```bash
# From Zephyr root directory
./scripts/ci/check_compliance.py -c origin/main..HEAD
```

**Note**: Requires Python dependencies:

```bash
pip install -r scripts/requirements-compliance.txt
```

**Checks performed**:

- **Checkpatch**: C code style (Linux kernel checkpatch)
- **Gitlint**: Commit message format
- **Identity**: DCO Signed-off-by verification
- **KconfigBasic**: Kconfig file linting
- **DevicetreeBindings**: DTS binding validation
- **YAML/Python linting**: Ruff, pylint, yamllint
- **Maintainers format**: MAINTAINERS.yml syntax
- **BinaryFiles**: No unintended binaries committed
- **ImageSize**: Footprint tracking

**Excludes** (per CI workflow):

- `KconfigBasic` and `SysbuildKconfigBasic` (excluded with `-e KconfigBasic`)
- `ClangFormat` (excluded with `-e ClangFormat`)
- `Identity` for dependabot PRs

**Checkpatch pre-commit hook** (optional):

```bash
# Create .git/hooks/pre-commit (make executable)
#!/bin/sh
set -e exec
exec git diff --cached | ${ZEPHYR_BASE}/scripts/checkpatch.pl -
```

---

## Project Structure (Key Locations)

### Top-Level Files

- `CMakeLists.txt`: Top-level build logic
- `Kconfig`, `Kconfig.zephyr`: Top-level configuration
- `west.yml`: External module manifest
- `VERSION`: Version information
- `.clang-format`: C formatting rules
- `MAINTAINERS.yml`: Code ownership/maintainers

### Important Directories

- `arch/`: Architecture-specific code (x86, arm, arm64, riscv, xtensa, etc.)
- `soc/`: SoC-specific code and configs
- `boards/`: Board definitions and configs
- `drivers/`: Device drivers (sensor, gpio, uart, spi, i2c, etc.)
- `dts/`: Devicetree includes and bindings
- `include/`: Public API headers
- `kernel/`: OS kernel (scheduler, threads, memory, synchronization)
- `subsys/`: Subsystems (Bluetooth, networking, USB, file systems, logging)
- `lib/`: Libraries (libc, posix, os)
- `samples/`: Example applications
- `tests/`: Test suites
- `scripts/`: Build/test tools
  - `scripts/ci/`: CI-specific scripts
  - `scripts/west_commands/`: West extensions
  - `scripts/dts/`: Devicetree tooling
- `doc/`: Documentation (reStructuredText)
  - `doc/contribute/`: Contribution guidelines (**READ THESE**)
  - `doc/develop/`: Developer guides
  - `doc/hardware/`: Board/SoC documentation

### Configuration Files

- `prj.conf`: Application Kconfig settings
- `boards/<board>.conf`: Board-specific Kconfig overlays
- `boards/<board>.overlay`: Board-specific DTS overlays
- `testcase.yaml`: Test suite definitions
- `sample.yaml`: Sample application metadata

---

## Common Pitfalls and Solutions

### Build Errors

**"No BOARD given"**:

- **Solution**: Always specify `-b <board>` with `west build`

**"CMake Error: The source directory does not appear to contain CMakeLists.txt"**:

- **Solution**: Check application path, ensure it has `CMakeLists.txt` and `prj.conf`

**"Kconfig warnings"**:

- **Solution**: Fix undefined symbols, check `depends on` clauses, verify `select` usage

**"Devicetree error"**:

- **Solution**: Check DTS syntax, verify node references, ensure bindings exist in `dts/bindings/`

### CI Failures

**"Identity check failed"**:

- **Solution**: Add `Signed-off-by` line (use `git commit -s --amend`, then `git push --force`)

**"Gitlint failed"**:

- **Solution**: Fix commit message (title too long, missing body, etc.)
  ```bash
  git rebase -i HEAD~N  # Edit N commits
  # Change "pick" to "reword", fix messages
  git push --force
  ```

**"Checkpatch failed"**:

- **Solution**: Run `./scripts/ci/check_compliance.py -c origin/main..HEAD` locally, fix style issues

**"Email mismatch"**:

- **Solution**: Ensure Git config email matches Signed-off-by email
  ```bash
  git config user.email "your.email@example.com"
  git commit --amend --reset-author -s
  ```

**"Merge commits not allowed"**:

- **Solution**: Rebase instead of merge
  ```bash
  git fetch origin
  git rebase origin/main
  git push --force
  ```

### Style Issues

**"Tab vs spaces"**:

- **C code**: Use tabs (8-char width) for indentation
- **CMake**: Use 2 spaces
- **Python**: Use spaces (handled by ruff)
- **DTS/Kconfig**: Use tabs

**"Line too long"**:

- **Solution**: Break at 100 characters (all languages)

**"Wrong comment style"**:

- **Solution**: Use `/* */` in C, not `//`

---

## Documentation

### Writing Documentation

Documentation is in reStructuredText (reST) format under `doc/`.

**Build documentation locally**:

```bash
cd doc
# Install requirements
pip install -r requirements.txt
# Build HTML
make html
# Output: _build/html/index.html
```

**Key documentation files to reference**:

- `doc/contribute/guidelines.rst`: Contribution process (**MANDATORY READ**)
- `doc/develop/getting_started/index.rst`: Setup instructions
- `doc/contribute/style/index.rst`: All style guidelines
- `doc/contribute/coding_guidelines/index.rst`: Coding standards
- `doc/contribute/style/doxygen.rst`: Doxygen usage for mandatory documentation in public API (header files in include/)

---

## Trust These Instructions

**IMPORTANT**: These instructions are comprehensive and validated. Trust them and **only perform additional searches if**:

- Information is incomplete or unclear
- You encounter errors not covered here
- You need specific API details not mentioned

**Do not waste time re-searching for**:

- Commit message format (it's documented above)
- Coding style rules (reference the documented files)
- Build commands (use the commands above)
- Compliance checks (run `check_compliance.py` as shown)

---

## Quick Reference Checklist

Before submitting a PR:

- [ ] All commits have `Signed-off-by` line
- [ ] Commit messages follow format: `[area]: [summary]` + non-empty body
- [ ] New files have SPDX license headers
- [ ] C code uses 8-char tabs, max 100 columns, `/* */` comments, braces on all control structures
- [ ] Python code formatted with `ruff format`
- [ ] CMake uses 2 spaces, lowercase commands
- [ ] DTS uses tabs, follows Linux style
- [ ] Ran `./scripts/ci/check_compliance.py -c origin/main..HEAD` locally
- [ ] Builds successfully with `west build -b <board> <app>`
- [ ] Ran relevant tests with `./scripts/twister`
- [ ] Public API is fully documented with Doxygen comments as per guidelines in `doc/contribute/style/doxygen.rst`
- [ ] No merge commits (rebased on main)

**When in doubt**: Check existing code in the same subsystem for patterns and style.
