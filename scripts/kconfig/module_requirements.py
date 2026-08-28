#!/usr/bin/env python3
#
# Copyright (c) 2026 Zephyr Project members and individual contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Work out which external modules a configuration needs.

A Zephyr module is needed when something in the build depends on it, and
Kconfig already says so: a feature that needs a module depends on that
module's presence symbol.

    config ICM42X70
            default y
            depends on DT_HAS_INVENSENSE_ICM42670P_ENABLED
            depends on ZEPHYR_HAL_TDK_MODULE

Reading that as "ICM42X70 needs hal_tdk" is not enough, because it holds for
every feature in the tree whether or not this build has any use for it. The
question is narrower: would this symbol be enabled if the module were there?
For the symbol above, that is only true on a board that has the sensor.

This answers it from Kconfig's own model, by evaluating the configuration
as if every module presence symbol were y — including defaults, selects and
implies that depend on those symbols — and asking two things:

  * would the symbol be enabled anyway, whether because the configuration
    asked for it, because a default applies, or because something selects
    or implies it;
  * and is a given module necessary for that, meaning the symbol cannot be
    enabled without it.

Presence is not read from the current assignment. A SoC family that depends
on its HAL is n when the module is missing; using that n would hide every
capability the family selects. Other symbols are therefore evaluated in the
same all-modules-present world, not from ``tri_value``.

The second question is what keeps 'depends on ZEPHYR_A_MODULE ||
ZEPHYR_B_MODULE' from demanding both modules: neither is necessary on its
own, so neither is reported, and the build is left to say which one it
wants.

Module names come from the module list this is given, so a module's identity
stays what its module.yml says it is; nothing is reverse engineered from a
sanitized Kconfig symbol name.

The output is the requirements file that scripts/zephyr_module.py reads::

    {"schema_version": 1,
     "required": [{"name": "hal_tdk", "required_by": ["ICM42X70"]}]}

Known limits of this prototype: a symbol inside a choice is only considered
enabled if the configuration selects it, since which member a choice settles
on depends on values this analysis is deliberately not committing to.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

from kconfiglib import (
    AND,
    BOOL,
    NOT,
    OR,
    TRISTATE,
    Choice,
    Kconfig,
    Symbol,
    expr_value,
)


def sanitize(name: str) -> str:
    """Return the Kconfig spelling of a module name."""
    return re.sub(r"[^a-zA-Z0-9]", "_", name).upper()


def presence_symbols(module_names: list[str]) -> dict[str, str]:
    """Map each module's presence symbol to the module's canonical name."""
    return {f"ZEPHYR_{sanitize(name)}_MODULE": name for name in module_names}


def module_names_from_file(path: Path) -> list[str]:
    """Read module names from the module list zephyr_module.py writes."""
    with path.open(encoding="utf-8") as f:
        content = json.load(f)

    if content.get("schema_version") != 1:
        sys.exit(
            f"ERROR: {path} has unsupported schema version "
            f"{content.get('schema_version')}, expected 1"
        )

    return [entry["name"] for entry in content.get("modules", [])]


def evaluate(expr, overrides: dict[str, int], memo: dict[str, int] | None = None) -> int:
    """Evaluate a Kconfig expression with some symbols held at a value.

    Overridden symbols (the module presence symbols) answer what they are
    told. Every other named symbol is evaluated in that same world, so a
    default or select that depends on a currently-unset family still sees
    the value it would have if the modules were present.
    """
    if memo is None:
        memo = {}

    if expr.__class__ is tuple:
        if expr[0] is AND:
            return min(
                evaluate(expr[1], overrides, memo),
                evaluate(expr[2], overrides, memo),
            )
        if expr[0] is OR:
            return max(
                evaluate(expr[1], overrides, memo),
                evaluate(expr[2], overrides, memo),
            )
        if expr[0] is NOT:
            return 2 - evaluate(expr[1], overrides, memo)
        # Comparisons cannot be affected by a presence symbol, which is
        # always a bool, so kconfiglib can answer those itself.
        return expr_value(expr)

    if isinstance(expr, Symbol):
        if expr.name in overrides:
            return overrides[expr.name]
        if expr.name and not expr.is_constant:
            return wanted_value(expr, overrides, memo)
        return expr.tri_value

    return expr.tri_value


def symbols_in(expr) -> set[str]:
    """The named symbols an expression mentions."""
    if expr.__class__ is tuple:
        return set().union(*(symbols_in(operand) for operand in expr[1:]))
    if isinstance(expr, Symbol | Choice) and expr.name:
        return {expr.name}
    return set()


def wanted_value(
    sym: Symbol, overrides: dict[str, int], memo: dict[str, int] | None = None
) -> int:
    """The value a symbol would take if the modules it needs were present.

    A symbol is enabled because the configuration says so, because one of its
    defaults applies, or because another symbol selects or implies it. Its
    dependencies then decide whether that can happen at all.
    """
    if memo is None:
        memo = {}
    if sym.name in overrides:
        return overrides[sym.name]
    if sym.name in memo:
        return memo[sym.name]

    # Assume n while this symbol is being computed, so a select cycle ends.
    memo[sym.name] = 0

    value = sym.user_value if isinstance(sym.user_value, int) else 0

    for entry in sym.defaults:
        # Zephyr's kconfiglib records where a default came from, so take the
        # value and the condition rather than unpacking the whole entry.
        default, condition = entry[0], entry[1]
        # Kconfig takes the first default whose condition holds.
        if evaluate(condition, overrides, memo):
            value = max(value, evaluate(default, overrides, memo))
            break

    value = max(
        value,
        evaluate(sym.rev_dep, overrides, memo),
        evaluate(sym.weak_rev_dep, overrides, memo),
    )
    value = min(value, evaluate(sym.direct_dep, overrides, memo))
    memo[sym.name] = value
    return value


def necessary_modules(sym: Symbol, modules: dict[str, str], overrides: dict[str, int]) -> list[str]:
    """The modules the symbol cannot be enabled without.

    A module is necessary when taking it away makes the symbol's dependencies
    unsatisfiable. Modules that merely appear in the dependency expression are
    not necessary: either of 'depends on ZEPHYR_A_MODULE || ZEPHYR_B_MODULE'
    would do, so neither is reported.
    """
    necessary = []
    for symbol_name in sorted(symbols_in(sym.direct_dep) & modules.keys()):
        without = dict(overrides, **{symbol_name: 0})
        # Fresh memo: values computed with every module present must not
        # leak into the "this module is absent" world.
        if not evaluate(sym.direct_dep, without, {}):
            necessary.append(modules[symbol_name])
    return necessary


def required_modules(kconf: Kconfig, module_names: list[str]) -> dict[str, list[str]]:
    """Map each module this configuration needs to the symbols that need it."""
    modules = presence_symbols(module_names)
    # Ask what the configuration would look like if every module were there.
    overrides = dict.fromkeys(modules, 2)
    memo: dict[str, int] = {}

    required: dict[str, set[str]] = {}
    for sym in kconf.unique_defined_syms:
        if sym.orig_type not in (BOOL, TRISTATE):
            continue
        # Which member a choice settles on depends on values this analysis
        # does not commit to, so only an explicit choice counts.
        if sym.choice is not None and not isinstance(sym.user_value, int):
            continue
        if not symbols_in(sym.direct_dep) & modules.keys():
            continue
        if not wanted_value(sym, overrides, memo):
            continue

        for name in necessary_modules(sym, modules, overrides):
            required.setdefault(name, set()).add(sym.name)

    return {name: sorted(symbols) for name, symbols in sorted(required.items())}


def requirements_report(required: dict[str, list[str]]) -> dict:
    """The requirements file scripts/zephyr_module.py reads."""
    return {
        "schema_version": 1,
        "required": [
            {"name": name, "required_by": symbols} for name, symbols in sorted(required.items())
        ],
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    parser.add_argument(
        "--kconfig-file",
        default="Kconfig",
        help="top level Kconfig file to analyze (default: Kconfig)",
    )
    parser.add_argument(
        "--modules-file",
        type=Path,
        required=True,
        help="module list written by zephyr_module.py --modules-out",
    )
    parser.add_argument(
        "--config",
        type=Path,
        action="append",
        default=[],
        help="configuration to analyze; may be given more than once",
    )
    parser.add_argument(
        "--out", type=Path, help="file to write the requirements to (default: stdout)"
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    kconf = Kconfig(args.kconfig_file, warn_to_stderr=False, suppress_traceback=True)
    for config in args.config:
        kconf.load_config(os.fspath(config), replace=False)

    report = requirements_report(required_modules(kconf, module_names_from_file(args.modules_file)))
    content = json.dumps(report, indent=2) + "\n"

    if args.out:
        args.out.write_text(content, encoding="utf-8")
    else:
        sys.stdout.write(content)

    return 0


if __name__ == "__main__":
    sys.exit(main())
