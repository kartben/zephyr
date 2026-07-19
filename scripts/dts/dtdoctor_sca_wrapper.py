#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
Compiler launcher wrapper that captures what appears to be Devicetree-related build errors, and
diagnoses them using dtdoctor_analyzer.py.

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

# Devicetree-related symbols that may show up in compiler/linker error messages:
# - __device_dts_ord_<N>: device symbol for a devicetree node (also matches the unresolved
#   __device_dts_ord_DT_..._ORD form produced by DEVICE_DT_GET() on a nonexistent node)
# - DT_N_*: unresolved node identifiers (nonexistent alias, node label, instance, path, or
#   property macros)
# - DT_CHOSEN_*: missing /chosen property
_DT_SYM = r"(?:__device_dts_ord_\w+|DT_N_\w+|DT_CHOSEN_\w+)"

_ERROR_PATTERNS = [
    rf"'({_DT_SYM})' undeclared",  # gcc
    rf"use of undeclared identifier '({_DT_SYM})'",  # LLVM/clang (ATfE)
    rf"undefined reference to `?'?({_DT_SYM})",  # GNU ld
    rf"undefined symbol: ({_DT_SYM})",  # LLVM/lld (ATfE)
]

# Zephyr builds compile with -fdiagnostics-color=always, so the captured error output
# contains ANSI SGR/erase-line escape sequences, including inside the quoted
# identifiers the patterns above match on.
_ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def extract_symbols(text: str) -> set[str]:
    """
    Extract devicetree-related symbols from compiler/linker error output.
    """
    text = _ANSI_ESCAPE_RE.sub("", text)
    # gcc quotes identifiers with Unicode quotation marks in UTF-8 locales
    text = text.replace("‘", "'").replace("’", "'")
    return {m for p in _ERROR_PATTERNS for m in re.findall(p, text)}


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    parser.add_argument(
        "--edt-pickle", help="path to edt.pickle file corresponding to the build to analyze"
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

    # Extract devicetree symbols from errors and run diagnostics
    if proc.returncode != 0 and args.edt_pickle:
        diag_script = os.path.join(os.path.dirname(__file__), "dtdoctor_analyzer.py")
        for symbol in sorted(extract_symbols(proc.stderr)):
            subprocess.run(
                [
                    sys.executable,
                    diag_script,
                    "--edt-pickle",
                    args.edt_pickle,
                    "--symbol",
                    symbol,
                ]
            )

    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
