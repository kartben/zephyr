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


def _symbol_root_key(symbol: str) -> str:
    """Extract a root-cause grouping key from a DT-related symbol.

    Symbols with the same root key stem from the same underlying devicetree
    issue (e.g. a missing property) and will be reported as a single
    diagnostic.

    For DT_N_ macros with sub-accessors (_NAME_, _IDX_, _VAL_, _PH_ORD),
    the key is the node+property prefix (e.g. DT_N_S_soc_S_uart_P_dmas).

    For __device_dts_ord_ symbols containing an unexpanded DT_N_ macro,
    the key is derived from the embedded macro.
    """
    inner = symbol

    # Unwrap __device_dts_ord_ prefix to get the inner macro
    if inner.startswith('__device_dts_ord_'):
        inner = inner[len('__device_dts_ord_'):]
        try:
            int(inner)
            return symbol  # Valid ordinal number — unique root cause
        except ValueError:
            pass  # Contains unexpanded macro, fall through

    # Group DT_N_..._P_<prop>_<sub-accessors> by DT_N_..._P_<prop>
    if '_P_' in inner:
        node_part, prop_rest = inner.split('_P_', 1)
        base_prop = re.split(r'_(?:NAME|IDX|VAL)_|_(?:PH_ORD|EXISTS)$', prop_rest)[0]
        return f"{node_part}_P_{base_prop}"

    return symbol


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

    # Extract symbols from errors and run diagnostics
    if proc.returncode != 0 and args.edt_pickle:
        # Patterns for __device_dts_ord_xxx symbols
        ord_patterns = [
            r"(__device_dts_ord_[A-Za-z0-9_]+).*undeclared",  # gcc
            r"undefined reference to.*(__device_dts_ord_[A-Za-z0-9_]+)",  # ld
            r"use of undeclared identifier '(__device_dts_ord_[A-Za-z0-9_]+)'",  # LLVM/clang (ATfE)
            r"undefined symbol: \(__device_dts_ord_([A-Za-z0-9_]+)",  # LLVM/lld (ATfE)
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

        # Deduplicate symbols by root cause: group symbols that stem from the
        # same missing property/node into a single diagnostic.  For example,
        # eight DT_N_…_P_dmas_NAME_{tx,rx}_VAL_{channel,request,config} symbols
        # plus their __device_dts_ord_ counterparts all collapse into one
        # diagnosis for the missing 'dmas' property.
        root_groups: dict[str, list[str]] = {}
        for symbol in symbols:
            key = _symbol_root_key(symbol)
            root_groups.setdefault(key, []).append(symbol)

        diag_script = os.path.join(os.path.dirname(__file__), "dtdoctor_analyzer.py")
        for key in sorted(root_groups):
            members = root_groups[key]
            # Use the root key as the representative symbol when it is a valid
            # DT_N_ macro (it points at the base property that is missing).
            # Otherwise fall back to the shortest member symbol.
            if key.startswith('DT_N_'):
                representative = key
            else:
                representative = min(members, key=len)

            cmd_args = [
                sys.executable,
                diag_script,
                "--edt-pickle",
                args.edt_pickle,
                "--symbol",
                representative,
            ]
            if len(members) > 1:
                cmd_args.extend(["--related-count", str(len(members))])

            subprocess.run(cmd_args)

    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
