#!/usr/bin/env python3
#
# Copyright (c) 2026 Zephyr Project members and individual contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Audit how the Zephyr tree depends on external modules.

Most workspaces have every module checked out, so code can use a module
without ever saying that it does. This tool finds those dependencies, declared
or not, by looking for the traces a module dependency leaves in the tree.

Declared dependencies are the ones the build model knows about, so that the
feature depending on the module disappears when the module does:

  kconfig     'depends on ZEPHYR_<MODULE>_MODULE'
  proxy       depending on a module-conditional symbol: one that Zephyr's
              in-tree glue for the module declares under the module's
              presence symbol, and that therefore cannot be set without it

Undeclared dependencies are the places where a build would only succeed
because the module happens to be in the workspace:

  select      selecting a module-conditional symbol, which quietly does
              nothing when the module is not there, leaving the selecting
              feature enabled
  capability  using a symbol the module's glue declares without gating it on
              the module, such as the HAS_<VENDOR>_HAL symbols a SoC selects
  cmake       ZEPHYR_<MODULE>_MODULE_DIR, ZEPHYR_<MODULE>_CMAKE_DIR, ...
  source      #include of a header that only a module provides

Zephyr sources every modules/<module>/Kconfig file under 'if 0' so that the
symbols exist for dependency checking even when the module is absent, and
sources it for real from the generated Kconfig.modules once the module is
active. A symbol in there is only reachable through the module when its own
declaration depends on the module's presence symbol; otherwise it is an
ordinary symbol that anyone can set, module or no module.

The candidate modules come from the manifest (west.yml by default), so module
identity stays canonical instead of being reverse engineered from sanitized
Kconfig symbol names. Modules that are checked out are inspected further, both
for the name they give themselves and for the build metadata (board, SoC, DTS,
... roots) that has to be available before ordinary dependency resolution can
run. The source scan needs the modules to be checked out as well: without
them, a module that the tree only uses from C code is reported as having no
evidence rather than as unused.

Typical use::

    ./scripts/module_dependency_audit.py                   # inventory table
    ./scripts/module_dependency_audit.py --module hal_tdk  # what uses hal_tdk
    ./scripts/module_dependency_audit.py --format json     # machine readable
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import yaml

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

# Module presence symbol, e.g. ZEPHYR_HAL_TDK_MODULE. The lookahead keeps
# ZEPHYR_HAL_TDK_MODULE_DIR and ..._MODULE_BLOBS out of the match.
MODULE_SYMBOL_RE = re.compile(r"\bZEPHYR_([A-Z0-9_]+)_MODULE(?![A-Z0-9_])")
# CMake and Kconfig variables pointing into a module.
MODULE_VARIABLE_RE = re.compile(r"\bZEPHYR_([A-Z0-9_]+)_(?:MODULE_DIR|CMAKE_DIR|KCONFIG)\b")
# 'config FOO' and 'menuconfig FOO', the only places a symbol comes into being.
KCONFIG_DECLARATION_RE = re.compile(r"^\s*(?:menu)?config\s+([A-Z0-9_]+)\s*$")
MODULE_DECLARATION_RE = re.compile(r"^\s*(?:menu)?config\s+ZEPHYR_([A-Z0-9_]+)_MODULE\s*$")
KCONFIG_SYMBOL_RE = re.compile(r"\b([A-Z][A-Z0-9_]{2,})\b")
CMAKE_SYMBOL_RE = re.compile(r"\bCONFIG_([A-Z][A-Z0-9_]{2,})\b")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]', re.MULTILINE)
KCONFIG_HELP_RE = re.compile(r"\s*(?:help|---help---)\s*$")
QUOTED_RE = re.compile(r'"[^"]*"')
KCONFIG_COMMENT_RE = re.compile(r"(?<!\$)#.*$")
# 'select FOO if BAR' enables FOO, it does not depend on it.
KCONFIG_SELECT_RE = re.compile(r"^\s*(?:select|imply)\s+([A-Z0-9_]+)(.*)$")

CMAKE_FILES = ("CMakeLists.txt",)
CMAKE_SUFFIXES = (".cmake",)
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".hpp", ".S")

# Directories that hold no build dependency information.
PRUNED_DIRS = {".git", ".github", "build", "doc"}
# Zephyr's in-tree glue for external modules, one subdirectory per module.
GLUE_ROOT = "modules"
# Manifest groups whose projects do not take part in firmware builds.
TOOLING_GROUPS = {"babblesim", "ci", "testing", "tools"}

DECLARED_KINDS = ("kconfig", "proxy")
# What a reference in a given part of the tree says about a module.
AREA_CLASSES = {
    "arch": "platform", "dts": "platform", "soc": "platform",
    "drivers": "driver",
}


def sanitize(name: str) -> str:
    """Return the Kconfig/CMake spelling of a module name."""
    return re.sub(r"[^a-zA-Z0-9]", "_", name).upper()


@dataclass(frozen=True)
class Reference:
    """One place in the tree that uses a module."""

    kind: str
    path: str
    line: int
    text: str

    @property
    def declares(self) -> bool:
        return self.kind in DECLARED_KINDS

    @property
    def area(self) -> str:
        """The top level tree area the reference lives in, e.g. 'drivers'."""
        return self.path.split("/", 1)[0]

    @property
    def consumer(self) -> str:
        """The directory that owns the reference."""
        return str(Path(self.path).parent)


@dataclass
class ModuleAudit:
    """Everything the audit knows about one external module."""

    name: str
    path: str
    groups: list[str] = field(default_factory=list)
    imports_manifest: bool = False
    available: bool = False
    metadata_roots: list[str] = field(default_factory=list)
    depends: list[str] = field(default_factory=list)
    glue: str = ""
    conditional_symbols: list[str] = field(default_factory=list)
    references: list[Reference] = field(default_factory=list)

    @property
    def symbol(self) -> str:
        return f"ZEPHYR_{sanitize(self.name)}_MODULE"

    @property
    def declared(self) -> list[Reference]:
        return [ref for ref in self.references if ref.declares]

    @property
    def undeclared(self) -> list[Reference]:
        return [ref for ref in self.references if not ref.declares]

    @property
    def consumers(self) -> set[str]:
        return {ref.consumer for ref in self.references}

    @property
    def areas(self) -> dict[str, int]:
        """How many references each part of the tree makes to this module."""
        areas: dict[str, int] = defaultdict(int)
        for ref in self.references:
            areas[ref.area] += 1
        return dict(sorted(areas.items(), key=lambda item: (-item[1], item[0])))

    @property
    def undeclared_consumers(self) -> set[str]:
        """Consumer directories that use the module without declaring it.

        A directory that declares the dependency in its Kconfig also covers
        the module paths its CMakeLists.txt uses, because the build only
        reaches them once the Kconfig symbol is enabled.
        """
        declared = {ref.consumer for ref in self.declared}
        return {ref.consumer for ref in self.undeclared if ref.consumer not in declared}

    @property
    def classification(self) -> str:
        """Classify the module by what it is to the build.

        tooling      does not take part in firmware builds at all
        bootstrap    contributes build metadata needed before Kconfig runs
        platform     used by a SoC, an architecture or a board
        driver       used by a device driver
        software     used by a subsystem or a library
        no-evidence  nothing in the tree refers to it
        """
        if TOOLING_GROUPS.intersection(self.groups):
            return "tooling"
        if self.metadata_roots or self.imports_manifest:
            return "bootstrap"
        if not self.references:
            return "no-evidence"
        # Modules are used from several places at once; what the module is to
        # the build is what most of its users make of it.
        classes: dict[str, int] = defaultdict(int)
        for ref in self.references:
            classes[AREA_CLASSES.get(ref.area, "software")] += 1
        return max(classes, key=lambda name: (classes[name], name))

    @property
    def blockers(self) -> list[str]:
        """Why the module cannot be inactive by default yet, if it cannot."""
        if self.classification == "tooling":
            return []
        blockers = []
        if self.classification == "bootstrap":
            blockers.append("provides build metadata")
        if self.undeclared_consumers:
            blockers.append(f"{len(self.undeclared_consumers)} undeclared consumers")
        if not self.declared:
            blockers.append("no declared dependency")
        return blockers

    @property
    def candidate(self) -> str:
        """Could this module be inactive by default?"""
        if self.classification == "tooling":
            return "n/a"
        return "no" if self.blockers else "yes"

    def as_dict(self) -> dict:
        return {
            "name": self.name,
            "path": self.path,
            "groups": self.groups,
            "symbol": self.symbol,
            "class": self.classification,
            "available": self.available,
            "metadata_roots": self.metadata_roots,
            "depends": self.depends,
            "conditional_symbols": self.conditional_symbols,
            "consumers": sorted(self.consumers),
            "declared": [f"{ref.path}:{ref.line}" for ref in self.declared],
            "undeclared_consumers": sorted(self.undeclared_consumers),
            "candidate": self.candidate,
            "blockers": self.blockers,
        }


def manifest_modules(manifest: Path, zephyr_base: Path) -> dict[str, ModuleAudit]:
    """Read the candidate module list from a west manifest.

    Only the manifest is needed, so the audit also works outside a west
    workspace.
    """
    with manifest.open(encoding="utf-8") as f:
        projects = yaml.load(f.read(), Loader=SafeLoader)["manifest"]["projects"]

    glue_directories = {sanitize(path.name): path.name
                        for path in (zephyr_base / GLUE_ROOT).iterdir() if path.is_dir()}

    modules = {}
    for project in projects:
        path = project.get("path", project["name"])
        module = ModuleAudit(
            name=project["name"],
            path=path,
            groups=project.get("groups", []),
            imports_manifest=bool(project.get("import", False)),
            available=(zephyr_base.parent / path).is_dir(),
        )
        if module.available:
            _inspect_checkout(module, zephyr_base.parent / path)
        glue = glue_directories.get(sanitize(module.name))
        module.glue = f"{GLUE_ROOT}/{glue}/" if glue else ""
        modules[module.name] = module
    return modules


def _inspect_checkout(module: ModuleAudit, module_path: Path) -> None:
    """Read a checked out module's own metadata."""
    for candidate in ("zephyr/module.yml", "zephyr/module.yaml"):
        meta_file = module_path / candidate
        if not meta_file.is_file():
            continue
        with meta_file.open(encoding="utf-8") as f:
            meta = yaml.load(f.read(), Loader=SafeLoader) or {}
        # The name in module.yml is the module's identity; the manifest
        # project name is only where it was found.
        module.name = meta.get("name", module.name)
        build = meta.get("build", {})
        module.depends = build.get("depends", [])
        module.metadata_roots = sorted(build.get("settings", {}))
        return


def build_files(zephyr_base: Path):
    """Yield the Kconfig and CMake files of the Zephyr tree."""
    for dirpath, dirnames, filenames in os.walk(zephyr_base):
        dirnames[:] = [d for d in dirnames
                       if d != "build" and (Path(dirpath) != zephyr_base or d not in PRUNED_DIRS)]
        for filename in filenames:
            if (filename.startswith("Kconfig") or filename in CMAKE_FILES
                    or filename.endswith(CMAKE_SUFFIXES)):
                yield Path(dirpath) / filename


def read_tree(zephyr_base: Path) -> dict[str, str]:
    """Read every build file once, keyed by its path relative to the tree."""
    tree = {}
    for path in build_files(zephyr_base):
        try:
            tree[path.relative_to(zephyr_base).as_posix()] = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
    return tree


def glue_owners(tree: dict[str, str], modules: dict[str, ModuleAudit]) -> dict[str, str]:
    """Map each in-tree glue file to the module it integrates.

    A glue file names its module by declaring that module's presence symbol,
    which is how Zephyr keeps the symbol defined while the module is absent.
    Files in a module's glue directory belong to it as well.
    """
    known = {sanitize(name) for name in modules}
    directories = {module.glue: module.name for module in modules.values() if module.glue}

    owners = {}
    for path, text in tree.items():
        if not path.startswith(f"{GLUE_ROOT}/"):
            continue
        declared = {match.group(1) for line in text.splitlines()
                    for match in [MODULE_DECLARATION_RE.match(line)] if match}
        declared &= known
        if len(declared) == 1:
            owners[path] = next(name for name in modules if sanitize(name) in declared)
        else:
            owner = next((name for glue, name in directories.items() if path.startswith(glue)),
                         None)
            if owner:
                owners[path] = owner
    return owners


def module_symbols(tree: dict[str, str],
                   modules: dict[str, ModuleAudit]) -> dict[str, tuple[str, bool]]:
    """Map every symbol declared by module glue to (module, module-conditional).

    A symbol is module-conditional when it cannot be set without the module:
    its declaration, a menu or an 'if' block around it depends on the module's
    presence symbol, or on another symbol of the same module that is itself
    module-conditional. The rest are ordinary symbols that anyone can set, so
    using one asks for a module without making the build require it.
    """
    owners = glue_owners(tree, modules)
    symbols: dict[str, tuple[str, bool]] = {}
    ambiguous: set[str] = set()

    for path, owner in owners.items():
        presence = f"ZEPHYR_{sanitize(owner)}_MODULE"
        declarations = list(_declarations(tree[path]))
        conditional = _gated_by(declarations, {presence, f"{presence}_BLOBS"})
        for symbol, _ in declarations:
            if MODULE_SYMBOL_RE.fullmatch(symbol):
                # The presence symbol itself is evidence in its own right.
                continue
            entry = (owner, symbol in conditional)
            if symbols.get(symbol, entry) != entry:
                # Two modules, or two different answers: neither is evidence.
                ambiguous.add(symbol)
            symbols[symbol] = entry

    for symbol in ambiguous:
        del symbols[symbol]

    # A symbol Zephyr also declares outside module glue is an ordinary symbol.
    for path, text in tree.items():
        if path in owners or not Path(path).name.startswith("Kconfig"):
            continue
        for line in text.splitlines():
            match = KCONFIG_DECLARATION_RE.match(line)
            if match:
                symbols.pop(match.group(1), None)

    # The blobs companion of a presence symbol is only set for a module that
    # is both present and has its blobs fetched.
    for module in modules.values():
        symbols[f"ZEPHYR_{sanitize(module.name)}_MODULE_BLOBS"] = (module.name, True)

    for module in modules.values():
        module.conditional_symbols = sorted(
            symbol for symbol, (owner, conditional) in symbols.items()
            if owner == module.name and conditional)

    return symbols


def _gated_by(declarations: list[tuple[str, set[str]]], roots: set[str]) -> set[str]:
    """Return the declared symbols that cannot be set unless a root symbol is."""
    gated = set()
    growing = True
    while growing:
        growing = False
        for symbol, gates in declarations:
            if symbol not in gated and gates & (roots | gated):
                gated.add(symbol)
                growing = True
    return gated


def _declarations(text: str):
    """Yield (symbol, the symbols that gate it) for one Kconfig file.

    Kconfig has no file level dependency, so a symbol is gated by its own
    'depends on' and by every menu and 'if' block it sits in.
    """
    enclosing: list[set[str]] = []
    symbol: str | None = None
    gates: set[str] = set()
    in_menu = False

    for raw in text.splitlines():
        line = KCONFIG_COMMENT_RE.sub("", raw).strip()
        if not line:
            continue

        if line.startswith("depends on "):
            if in_menu and enclosing:
                enclosing[-1] |= _required_symbols(line)
            elif symbol:
                gates |= _required_symbols(line)
            continue

        in_menu = line.startswith(("menu ", "menu	", 'menu"'))

        if line.startswith("if "):
            enclosing.append(_required_symbols(line))
        elif line.startswith(("endif", "endmenu", "endchoice")):
            if enclosing:
                enclosing.pop()
        elif line.startswith(("menu ", "menu	", 'menu"', "choice")):
            enclosing.append(set())
        else:
            match = KCONFIG_DECLARATION_RE.match(raw)
            if match:
                if symbol:
                    yield symbol, gates
                symbol = match.group(1)
                gates = set().union(*enclosing) if enclosing else set()

    if symbol:
        yield symbol, gates


def _required_symbols(expression: str) -> set[str]:
    """The symbols an expression requires, ignoring the ones it negates."""
    return {match.group(1) for match in KCONFIG_SYMBOL_RE.finditer(expression)
            if not expression[:match.start()].rstrip().endswith("!")}


def scan_tree(tree: dict[str, str], modules: dict[str, ModuleAudit]) -> None:
    """Attribute every module reference in the tree to the module it names."""
    by_symbol = {sanitize(module.name): module for module in modules.values()}
    symbols = module_symbols(tree, modules)

    for path, text in tree.items():
        is_kconfig = Path(path).name.startswith("Kconfig")
        for number, line in _significant_lines(text, is_kconfig):
            code = QUOTED_RE.sub('""', KCONFIG_COMMENT_RE.sub("", line) if is_kconfig else line)
            for symbol, kind in _line_references(code, is_kconfig, symbols):
                module = by_symbol.get(symbol)
                # A module's own glue is only processed while the module is
                # active: what it says describes the module, not a use of it.
                if module is None or (module.glue and path.startswith(module.glue)):
                    continue
                module.references.append(Reference(kind, path, number, line.strip()))


def _significant_lines(text: str, is_kconfig: bool):
    """Yield the numbered lines of a build file, skipping Kconfig help text.

    Help text names symbols in prose, which would otherwise be indistinguishable
    from using them.
    """
    help_indent = None
    for number, line in enumerate(text.splitlines(), start=1):
        if is_kconfig:
            indent = len(line) - len(line.lstrip())
            if help_indent is not None:
                if not line.strip() or indent > help_indent:
                    continue
                help_indent = None
            if KCONFIG_HELP_RE.fullmatch(line):
                help_indent = indent
                continue
        yield number, line


def _line_references(line: str, is_kconfig: bool, symbols: dict[str, tuple[str, bool]]):
    """Yield (sanitized module name, evidence kind) for one line of a build file."""

    def owned(symbol, selected):
        """What naming a module owned symbol here says about the module."""
        owner, conditional = symbols[symbol]
        if not conditional:
            # An ordinary symbol: setting it does not require the module.
            return sanitize(owner), "capability"
        # A module-conditional symbol gates its user, unless it is selected,
        # in which case the select is simply lost without the module.
        return sanitize(owner), "select" if selected else "proxy"

    conditions = line
    if is_kconfig:
        if KCONFIG_DECLARATION_RE.match(line):
            # A fallback declaration for an absent module, not a use of it.
            return
        for match in MODULE_SYMBOL_RE.finditer(line):
            # 'depends on !ZEPHYR_<MODULE>_MODULE' reacts to a module being
            # absent, which is the opposite of depending on it.
            if not line[:match.start()].rstrip().endswith("!"):
                yield match.group(1), "kconfig"

        selection = KCONFIG_SELECT_RE.match(line)
        if selection:
            if selection.group(1) in symbols:
                yield owned(selection.group(1), selected=True)
            conditions = selection.group(2)
        symbol_re = KCONFIG_SYMBOL_RE
    else:
        symbol_re = CMAKE_SYMBOL_RE

    for match in symbol_re.finditer(conditions):
        if match.group(1) in symbols and not conditions[:match.start()].rstrip().endswith("!"):
            yield owned(match.group(1), selected=False)

    for match in MODULE_VARIABLE_RE.finditer(line):
        if match.group(1) != "CURRENT":
            yield match.group(1), "cmake"


def scan_sources(zephyr_base: Path, modules: dict[str, ModuleAudit]) -> None:
    """Find Zephyr sources including a header that only a module provides.

    This needs the modules to be checked out, and does nothing otherwise.
    Header names that also exist in Zephyr are ignored, so that a module
    cannot be blamed for an include that resolves in the Zephyr tree.
    """
    zephyr_headers = {path.name for path in (zephyr_base / "include").rglob("*.h")}
    owners: dict[str, str] = {}

    for module in modules.values():
        if not module.available:
            continue
        for header in (zephyr_base.parent / module.path).rglob("*.h"):
            if header.name in zephyr_headers:
                continue
            # A header two modules provide cannot identify either of them.
            owners[header.name] = "" if header.name in owners else module.name

    if not owners:
        return

    by_name = {module.name: module for module in modules.values()}
    for dirpath, dirnames, filenames in os.walk(zephyr_base):
        dirnames[:] = [d for d in dirnames
                       if d != "build" and (Path(dirpath) != zephyr_base or d not in PRUNED_DIRS)]
        for filename in filenames:
            if not filename.endswith(SOURCE_SUFFIXES):
                continue
            path = Path(dirpath) / filename
            try:
                text = path.read_text(encoding="utf-8")
            except (UnicodeDecodeError, OSError):
                continue
            relative = path.relative_to(zephyr_base).as_posix()
            for match in INCLUDE_RE.finditer(text):
                module = by_name.get(owners.get(Path(match.group(1)).name, ""))
                if module is None or (module.glue and relative.startswith(module.glue)):
                    continue
                module.references.append(Reference(
                    "source", relative, text.count("\n", 0, match.start()) + 1,
                    match.group(0).strip()))


def audit(zephyr_base: Path, manifest: Path, sources: bool = True) -> dict[str, ModuleAudit]:
    """Build the module inventory for a Zephyr tree."""
    modules = manifest_modules(manifest, zephyr_base)
    scan_tree(read_tree(zephyr_base), modules)
    if sources:
        scan_sources(zephyr_base, modules)
    return modules


def unattributed_glue(tree: dict[str, str],
                      modules: dict[str, ModuleAudit]) -> dict[str, list[str]]:
    """Return the in-tree module glue that names no module, and what it declares.

    Zephyr's glue for a module normally declares that module's presence symbol,
    which both keeps the symbol defined while the module is absent and says
    which module the file integrates. A glue file that does not cannot be tied
    to a module, so nothing it declares can be recognized as a dependency on
    one.
    """
    owners = glue_owners(tree, modules)
    unattributed = {}
    for path, text in tree.items():
        # modules/Kconfig is the aggregator, not glue for any one module.
        if path in owners or path == f"{GLUE_ROOT}/Kconfig":
            continue
        if not path.startswith(f"{GLUE_ROOT}/") or not Path(path).name.startswith("Kconfig"):
            continue
        lines = text.splitlines()
        if any(MODULE_DECLARATION_RE.match(line) for line in lines):
            # It names a module, just not one the manifest provides; that is
            # what unknown_symbols() reports.
            continue
        declared = [match.group(1) for line in lines
                    for match in [KCONFIG_DECLARATION_RE.match(line)] if match]
        if declared:
            unattributed[path] = declared
    return unattributed


def unknown_symbols(tree: dict[str, str], modules: dict[str, ModuleAudit]) -> dict[str, set[str]]:
    """Return module symbols used in the tree that no manifest project provides."""
    known = {sanitize(name) for name in modules}
    unknown: dict[str, set[str]] = defaultdict(set)
    for path, text in tree.items():
        for pattern in (MODULE_SYMBOL_RE, MODULE_VARIABLE_RE):
            for match in pattern.finditer(text):
                if match.group(1) not in known and match.group(1) != "CURRENT":
                    unknown[match.group(1)].add(path)
    return unknown


def print_table(modules: list[ModuleAudit], out=None) -> None:
    """Print the inventory, the modules with the most dependency debt first."""
    out = out or sys.stdout
    header = ("Module", "Class", "Consumers", "Declared", "Undeclared", "Candidate")
    rows = [(m.name, m.classification, str(len(m.consumers)), str(len(m.declared)),
             str(len(m.undeclared_consumers)), m.candidate) for m in modules]
    widths = [max(len(row[column]) for row in [header, *rows]) for column in range(len(header))]

    def justify(row):
        return "  ".join(cell.ljust(width) for cell, width in zip(row, widths, strict=True))

    separator = "  ".join("-" * width for width in widths)
    print(justify(header), file=out)
    print(separator, file=out)
    for row in rows:
        print(justify(row), file=out)
    print(separator, file=out)
    print(f"{len(modules)} modules: "
          f"{sum(1 for m in modules if m.candidate == 'yes')} ready for selective activation, "
          f"{sum(1 for m in modules if m.candidate == 'no')} with dependency debt", file=out)


def print_details(module: ModuleAudit, out=None) -> None:
    """Print every reference to one module."""
    out = out or sys.stdout
    print(f"{module.name} ({module.path}) [{module.classification}]", file=out)
    print(f"  presence symbol: {module.symbol}", file=out)
    if module.metadata_roots:
        print(f"  build metadata:  {', '.join(module.metadata_roots)}", file=out)
    if module.depends:
        print(f"  module depends:  {', '.join(module.depends)}", file=out)
    if module.areas:
        print("  used from:       "
              + ", ".join(f"{area} ({count})" for area, count in module.areas.items()), file=out)
    if module.conditional_symbols:
        print("  symbols that require the module: "
              + ", ".join(module.conditional_symbols), file=out)

    for label, references in (("declared", module.declared), ("undeclared", module.undeclared)):
        print(f"  {label} ({len(references)}):", file=out)
        for ref in sorted(references, key=lambda r: (r.path, r.line)):
            print(f"    [{ref.kind}] {ref.path}:{ref.line}: {ref.text}", file=out)

    if module.blockers:
        print(f"  not a candidate for inactive by default: {'; '.join(module.blockers)}", file=out)
    else:
        print("  candidate for inactive by default", file=out)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter,
                                     allow_abbrev=False)
    parser.add_argument("-z", "--zephyr-base", type=Path,
                        default=Path(os.environ.get("ZEPHYR_BASE", Path(__file__).parents[1])),
                        help="path to the Zephyr repository")
    parser.add_argument("-m", "--manifest", type=Path,
                        help="manifest listing the candidate modules "
                             "(default: <zephyr-base>/west.yml)")
    parser.add_argument("--module", action="append", default=[],
                        help="report every reference to this module; repeatable")
    parser.add_argument("--format", choices=("table", "json", "csv"), default="table",
                        help="output format of the inventory (default: table)")
    parser.add_argument("--class", dest="classes", action="append", default=[],
                        choices=("platform", "driver", "software", "bootstrap", "tooling",
                                 "no-evidence"),
                        help="only report modules of this class; repeatable")
    parser.add_argument("--candidates", action="store_true",
                        help="only report modules that could be inactive by default")
    parser.add_argument("--no-sources", action="store_true",
                        help="skip the source include scan, which needs checked out modules")
    parser.add_argument("--unknown-symbols", action="store_true",
                        help="report module symbols that no manifest project provides")
    parser.add_argument("--unattributed-glue", action="store_true",
                        help="report in-tree module glue that names no module")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    zephyr_base = args.zephyr_base.resolve()
    manifest = args.manifest or zephyr_base / "west.yml"

    modules = audit(zephyr_base, manifest, sources=not args.no_sources)

    if args.unknown_symbols:
        for symbol, paths in sorted(unknown_symbols(read_tree(zephyr_base), modules).items()):
            print(f"ZEPHYR_{symbol}_MODULE* is not provided by any project in {manifest.name}:")
            for path in sorted(paths):
                print(f"    {path}")
        return 0

    if args.unattributed_glue:
        for path, declared in sorted(unattributed_glue(read_tree(zephyr_base), modules).items()):
            print(f"{path} declares no ZEPHYR_<MODULE>_MODULE symbol, so its "
                  f"{len(declared)} symbols belong to no module:")
            print("    " + ", ".join(declared))
        return 0

    if args.module:
        for name in args.module:
            if name not in modules:
                print(f"error: {name} is not a project in {manifest}", file=sys.stderr)
                return 1
            print_details(modules[name])
        return 0

    selected = sorted(modules.values(), key=lambda m: (-len(m.undeclared_consumers), m.name))
    if args.classes:
        selected = [m for m in selected if m.classification in args.classes]
    if args.candidates:
        selected = [m for m in selected if m.candidate == "yes"]

    if args.format == "json":
        json.dump({"schema_version": 1, "modules": [m.as_dict() for m in selected]},
                  sys.stdout, indent=2)
        print()
    elif args.format == "csv":
        writer = csv.writer(sys.stdout)
        writer.writerow(("module", "class", "consumers", "declared", "undeclared", "candidate"))
        for module in selected:
            writer.writerow((module.name, module.classification, len(module.consumers),
                             len(module.declared), len(module.undeclared_consumers),
                             module.candidate))
    else:
        print_table(selected)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        # Output is being read by something like 'head'.
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        sys.exit(0)
