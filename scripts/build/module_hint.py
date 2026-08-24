#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Name the modules a build is made of that the workspace does not have.

A module goes missing in one of two ways, and neither says so. Devicetree is
resolved before Kconfig runs, so a module providing a devicetree root fails as
an include nothing can explain; and Kconfig reports a capability it cannot
satisfy rather than the module behind it.

The dependency is written down either way: the board selects its SoC, the SoC
and the application's configuration select capabilities, and those select the
module. Following those selects in the Kconfig sources answers the question
without configuring anything.
"""

import argparse
import re
import sys
from pathlib import Path

CONFIG = re.compile(r"^\s*(?:menu)?config\s+(\w+)", re.M)
SELECT = re.compile(r"^\s*select\s+(\w+)", re.M)
ACTIVE = re.compile(r"^ZEPHYR_(\w+)_MODULE_ACTIVE$")
ENABLED = re.compile(r"^CONFIG_(\w+)=y\s*$", re.M)


def sanitize(name):
    return re.sub(r"[^A-Z0-9_]", "_", name.upper())


def kconfig_files(root):
    for path in Path(root).rglob("Kconfig*"):
        if ".git" not in path.parts and path.is_file():
            yield path


def main():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("--zephyr-base", required=True)
    parser.add_argument(
        "--seed-dir",
        nargs="*",
        default=[],
        help="Kconfig under these directories describes the platform",
    )
    parser.add_argument(
        "--seed-conf",
        nargs="*",
        default=[],
        help="configuration files whose enabled options are what this build asked for",
    )
    parser.add_argument("--inactive", nargs="*", default=[])
    args = parser.parse_args()

    if not args.inactive:
        return 0

    # What each symbol selects, which of them are choice members, and what the
    # platform's own Kconfig mentions.
    selects = {}
    in_choice = set()
    seeds = set()
    seed_dirs = [Path(d).resolve() for d in args.seed_dir if d]

    for path in kconfig_files(args.zephyr_base):
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue

        current, choice_depth = None, 0
        for line in text.splitlines():
            stripped = line.strip()
            if stripped == "endchoice" or stripped.startswith("endchoice "):
                choice_depth = max(0, choice_depth - 1)
                continue
            if stripped == "choice" or stripped.startswith("choice "):
                choice_depth += 1
                continue
            m = CONFIG.match(line)
            if m:
                current = m.group(1)
                if choice_depth:
                    in_choice.add(current)
                continue
            s = SELECT.match(line)
            if s and current:
                selects.setdefault(current, set()).add(s.group(1))

        resolved = path.resolve()
        if any(d == resolved or d in resolved.parents for d in seed_dirs):
            seeds |= set(CONFIG.findall(text)) | set(SELECT.findall(text))

    for conf in args.seed_conf:
        try:
            text = Path(conf).read_text(errors="replace")
        except OSError:
            continue
        seeds |= set(ENABLED.findall(text))

    # Picking an implementation is a choice, and the module hangs off the
    # choice rather than off the option the build asked for. Step back one hop
    # through choices only: doing it for every symbol reaches most of the tree.
    selected_by = {}
    for sym, targets in selects.items():
        if sym not in in_choice:
            continue
        for target in targets:
            selected_by.setdefault(target, set()).add(sym)
    seeds |= {s for seed in seeds for s in selected_by.get(seed, ())}

    # Follow the selects and collect the module activation symbols they reach.
    wanted, seen, queue = set(), set(), list(seeds)
    while queue:
        sym = queue.pop()
        if sym in seen:
            continue
        seen.add(sym)
        m = ACTIVE.match(sym)
        if m:
            wanted.add(m.group(1))
        queue.extend(selects.get(sym, ()))

    by_symbol = {sanitize(name): name for name in args.inactive}
    found = [by_symbol[stem] for stem in sorted(wanted) if stem in by_symbol]

    # A select carrying a condition is followed without evaluating it, so a
    # platform can reach more than it needs. Name the one whose project the
    # platform is filed under first.
    where = " ".join(str(d).lower() for d in seed_dirs)
    found.sort(key=lambda n: n.removeprefix("hal_") not in where)

    for name in found:
        print(name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
