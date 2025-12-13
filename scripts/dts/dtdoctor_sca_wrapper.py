#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
Compiler launcher wrapper that captures what appears to be Devicetree-related build errors, and
diagnoses them using dtdoctor_analyzer.py.

The wrapper detects and diagnoses:
- Device ordinal errors (e.g. undefined reference to __device_dts_ord_123)
- DT macro errors (e.g. DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin undeclared)

The tool is meant to be configured as a CMAKE_<LANG>_COMPILER_LAUNCHER or as a
CMAKE_<LANG>_LINKER_LAUNCHER.

Example usage:

  ./scripts/dts/dtdoctor_sca_wrapper.py --edt-pickle <path> \\
    -- <compiler-or-linker> <args...>
"""

import argparse
import os
import re
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    parser.add_argument(
        "--edt-pickle", help="path to edt.pickle file corresponding to the build to analyze"
    )
    parser.add_argument(
        "--macro-db", help="path to macro database JSON file (optional, for improved diagnostics)"
    )

    if "--" in sys.argv:
        idx = sys.argv.index("--")
        args, cmd = parser.parse_known_args(sys.argv[1:idx])[0], sys.argv[idx + 1 :]
    else:
        args, cmd = parser.parse_known_args(sys.argv[1:])[0], sys.argv[1:]

    # Run compiler/linker command
    proc = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)

    # Extract symbols from errors and run diagnostics
    if proc.returncode != 0 and args.edt_pickle:
        # Patterns for __device_dts_ord_xxx symbols
        ord_patterns = [
            r"(__device_dts_ord_\d+).*undeclared here",  # gcc
            r"undefined reference to.*(__device_dts_ord_\d+)",  # ld
            r"use of undeclared identifier '(__device_dts_ord_\d+)'",  # LLVM/clang (ATfE)
            r"undefined symbol: \(__device_dts_ord_(\d+)",  # LLVM/lld (ATfE)
        ]
        
        # Patterns for DT macro symbols like DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin
        dt_macro_patterns = [
            r"(DT_N_(?:NODELABEL|ALIAS|INST|S)_[A-Za-z0-9_]+).*undeclared",  # gcc
            r"(DT_N_(?:NODELABEL|ALIAS|INST|S)_[A-Za-z0-9_]+).*not\s+defined",  # preprocessor
            r"use of undeclared identifier '(DT_N_(?:NODELABEL|ALIAS|INST|S)_[A-Za-z0-9_]+)'",  # clang
            r"undefined reference to.*(DT_N_(?:NODELABEL|ALIAS|INST|S)_[A-Za-z0-9_]+)",  # ld
            r"'(DT_N_(?:NODELABEL|ALIAS|INST|S)_[A-Za-z0-9_]+)'.*was not declared",  # gcc
        ]
        
        symbols = {m for p in ord_patterns for m in re.findall(p, proc.stderr)}
        symbols.update({m for p in dt_macro_patterns for m in re.findall(p, proc.stderr)})

        diag_script = os.path.join(os.path.dirname(__file__), "dtdoctor_analyzer.py")
        for symbol in sorted(symbols):
            cmd_args = [
                sys.executable,
                diag_script,
                "--edt-pickle",
                args.edt_pickle,
                "--symbol",
                symbol,
            ]
            # Add macro database if provided
            if args.macro_db and os.path.exists(args.macro_db):
                cmd_args.extend(["--macro-db", args.macro_db])
            
            subprocess.run(cmd_args)

    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
