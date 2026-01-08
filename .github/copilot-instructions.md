# Copilot Instructions for Zephyr RTOS

## Repository Overview

Zephyr is a scalable real-time operating system (RTOS) supporting multiple hardware architectures (ARM, x86, ARC, Xtensa, RISC-V, SPARC, MIPS) optimized for resource-constrained embedded devices. The codebase is large (~80,000+ files) written primarily in C with Python tooling.

**Key versions:** Python 3.10+, CMake 3.20.5+, Zephyr SDK 0.17.4

## Build System

Zephyr uses CMake with the `west` meta-tool for workspace and build management.

### Essential Commands

```bash
# Environment setup (always source before building)
source zephyr-env.sh
export ZEPHYR_BASE=$(pwd)

# Build a sample application (replace board name)
west build -p always -b native_sim samples/hello_world

# Run tests with twister (Zephyr's test runner)
./scripts/twister -p native_sim -T tests/kernel/common

# Run compliance checks locally before submitting PR
./scripts/ci/check_compliance.py -c origin/main..
```

### Important Notes
- **Never invoke CMakeLists.txt directly** - Zephyr's CMakeLists.txt requires building through an application directory
- Use `west build -p always` for pristine builds to avoid stale cache issues
- The `native_sim` board is useful for testing without hardware

## CI Workflows (Triggered on PRs)

| Workflow | Purpose | Local Equivalent |
|----------|---------|------------------|
| `compliance.yml` | Code style, commit format, licensing | `./scripts/ci/check_compliance.py -c origin/main..` |
| `twister.yaml` | Build and test on multiple platforms | `./scripts/twister -p <board> -T <test_path>` |
| `doc-build.yml` | Documentation builds | `make -C doc html` |
| `coding_guidelines.yml` | MISRA-C guidelines via coccinelle | `./scripts/ci/guideline_check.py -c origin/main..` |

## Code Style & Formatting

- **C/C++:** Linux kernel style using tab characters (displayed as 8 spaces) for indentation (see `.clang-format`, `.editorconfig`)
- **Python:** 4-space indentation, 100 char line limit (see `.ruff.toml`)
- **YAML:** 2-space indentation (see `.yamllint`)
- **Commit messages:** Format: `area: summary` with non-empty body and `Signed-off-by:` line

### File Headers (Required)
```c
/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
```

## Project Structure

```
arch/           # Architecture-specific code (arm, x86, riscv, etc.)
boards/         # Board definitions and configurations
doc/            # Documentation (Sphinx/RST)
drivers/        # Device drivers
dts/            # Devicetree source files and bindings
include/        # Public API headers
kernel/         # Core kernel code
lib/            # Libraries (libc, etc.)
samples/        # Sample applications
scripts/        # Build tools, twister, compliance scripts
  ci/           # CI-specific scripts (check_compliance.py)
  twister       # Test runner
subsys/         # Subsystems (Bluetooth, networking, USB, etc.)
tests/          # Test cases
west.yml        # West manifest (external module dependencies)
```

## Key Configuration Files

- `Kconfig`, `Kconfig.zephyr` - Kernel configuration options
- `CMakeLists.txt` - Build system (do not invoke directly)
- `.checkpatch.conf` - C code style checks
- `.clang-format` - C/C++ formatting rules
- `.gitlint` - Commit message rules
- `MAINTAINERS.yml` - Code ownership (not CODEOWNERS)
- `west.yml` - External module manifest

## Testing

```bash
# Run specific test
west twister -p native_sim -s tests/kernel/common/kernel.common

# Run tests for changes (CI does this automatically)
python3 ./scripts/ci/test_plan.py -c origin/main..

# Test metadata files: testcase.yaml or sample.yaml in test directories
```

## Making Changes

1. **Drivers/Subsystems:** Check `MAINTAINERS.yml` for ownership
2. **Devicetree bindings:** Located in `dts/bindings/`, follow YAML schema
3. **Kconfig options:** Follow existing patterns in `Kconfig*` files
4. **API changes:** Update documentation in `doc/` and headers in `include/zephyr/`

## Common Pitfalls

- Missing `Signed-off-by:` line in commits (use `git commit -s`)
- Empty commit message body (always include description)
- Incorrect commit title format (must be `area: description`)
- C code not indented with tab characters
- PR contains merge commits (rebase required)

## Python Dependencies

```bash
pip install -r scripts/requirements.txt  # Full development
pip install -r scripts/requirements-compliance.txt  # For compliance checks
```

## Trust These Instructions

These instructions are validated against the repository. Only search for additional information if:
- The specific file path mentioned doesn't exist
- A command fails with an unexpected error
- You need details about a specific subsystem not covered here
