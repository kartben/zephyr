#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
A script to help diagnose build errors related to Devicetree.

To use this script as a standalone tool, provide the path to an edt.pickle file
(e.g ./build/zephyr/edt.pickle) and a symbol that appeared in the build error
message.

The script supports diagnosing:
- Device ordinal symbols (e.g. __device_dts_ord_123)
- DT macro symbols (e.g. DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin)

Example usage:

./scripts/dts/dtdoctor_analyzer.py \\
    --edt-pickle ./build/zephyr/edt.pickle \\
    --symbol __device_dts_ord_123

./scripts/dts/dtdoctor_analyzer.py \\
    --edt-pickle ./build/zephyr/edt.pickle \\
    --symbol DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin

"""

import argparse
import os
import pickle
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "python-devicetree" / "src"))
sys.path.insert(0, str(Path(__file__).parents[1] / "kconfig"))

import kconfiglib
from devicetree import edtlib
from tabulate import tabulate


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    parser.add_argument(
        "--edt-pickle",
        required=True,
        help="path to edt.pickle file corresponding to the build to analyze",
    )
    parser.add_argument(
        "--symbol", required=True, help="symbol for which to obtain troubleshooting information"
    )

    return parser.parse_args()


def load_edt(path: str) -> edtlib.EDT:
    with open(path, "rb") as f:
        return pickle.load(f)


def setup_kconfig() -> kconfiglib.Kconfig:
    kconf = kconfiglib.Kconfig(os.path.join(os.environ.get("ZEPHYR_BASE"), "Kconfig"), warn=False)
    return kconf


def format_node(node: edtlib.Node) -> str:
    return f"{node.labels[0]}: {node.path}" if node.labels else node.path


import gen_defines


def _build_node_prefix_map(edt: edtlib.EDT) -> dict[str, edtlib.Node]:
    """
    Build a mapping from every possible DT node identifier prefix to its node.

    Uses the same logic as gen_defines.write_idents_and_existence() so that
    the prefixes exactly match those that appear in generated macros.
    """
    prefix_map = {}
    for node in edt.nodes:
        prefix_map[gen_defines.node_z_path_id(node)] = node
        for label in node.labels:
            prefix_map[f"N_NODELABEL_{gen_defines.str2ident(label)}"] = node
        for alias in node.aliases:
            prefix_map[f"N_ALIAS_{gen_defines.str2ident(alias)}"] = node
        for compat in node.compats:
            instance_no = edt.compat2nodes[compat].index(node)
            prefix_map[f"N_INST_{instance_no}_{gen_defines.str2ident(compat)}"] = node
    return prefix_map


def _find_node_and_suffix(
    symbol: str, prefix_map: dict[str, edtlib.Node]
) -> tuple[edtlib.Node | None, str]:
    """
    Match a DT macro symbol against known node prefixes.

    Strips DT_ prefix and _EXISTS suffix, then finds the longest matching
    prefix in the map.  Returns (node, remaining_suffix) or (None, symbol).
    """
    s = symbol.removeprefix("DT_")
    if s.endswith("_EXISTS"):
        s = s[:-7]

    best_node = None
    best_suffix = s
    best_len = 0
    for prefix, node in prefix_map.items():
        if len(prefix) > best_len and (s == prefix or s.startswith(prefix + "_")):
            best_node = node
            best_suffix = s[len(prefix):]
            best_len = len(prefix)

    # Guard against the root node "N" matching as a false prefix when the
    # symbol actually refers to a named node (NODELABEL, ALIAS, INST, or
    # path).  In those cases the suffix will start with one of these keywords,
    # meaning the intended node was not found in the EDT.
    if best_node is not None and best_suffix.lstrip("_").startswith(
        ("NODELABEL_", "ALIAS_", "INST_", "S_")
    ):
        return None, s

    return best_node, best_suffix


def _match_prop(node: edtlib.Node, suffix: str) -> tuple[str | None, str]:
    """Forward-match a property name at the start of suffix using str2ident."""
    best_name = None
    best_len = 0
    for prop_name in node.props:
        ident = gen_defines.str2ident(prop_name)
        if len(ident) > best_len and (suffix == ident or suffix.startswith(ident + "_")):
            best_name = prop_name
            best_len = len(ident)
    if best_name is not None:
        return best_name, suffix[best_len:]
    return None, suffix


def _match_cell(data: dict, cell_ident: str) -> str | None:
    """Forward-match a cell name from a ControllerAndData.data dict."""
    for cell_name in data:
        if gen_defines.str2ident(cell_name) == cell_ident:
            return cell_name
    return None


def _diagnose_missing_property(node: edtlib.Node, prop_ident: str) -> list[str]:
    lines = [f"Property matching '{prop_ident}' not found on node '{format_node(node)}'.\n"]
    if node.props:
        lines.append("Available properties on this node:")
        for pname in sorted(node.props.keys()):
            lines.append(f" - {pname}")
    else:
        lines.append("This node has no properties.")
    lines.append("\nCheck your devicetree binding and node definition.")
    return lines


def _diagnose_index_and_cell(
    node: edtlib.Node, prop: edtlib.Property, idx: int, rest: str
) -> list[str]:
    """Diagnose _IDX_<n>[_VAL_<cell>] issues for a phandle-array property."""
    if prop.type != 'phandle-array':
        return [
            f"Property '{prop.name}' on '{format_node(node)}' is of type "
            f"'{prop.type}', not 'phandle-array'.",
            "Index-based access with _IDX_ is only valid for phandle-array properties.",
        ]
    if idx >= len(prop.val):
        return [
            f"Index {idx} out of bounds for '{prop.name}' on '{format_node(node)}'.",
            f"Property has {len(prop.val)} element(s).",
        ]
    entry = prop.val[idx]
    if entry is None:
        return [
            f"Element at index {idx} of '{prop.name}' on "
            f"'{format_node(node)}' is unspecified (gap in phandle-array).",
        ]

    val_match = re.match(r'_VAL_(\w+)', rest)
    if val_match:
        cell_ident = val_match.group(1)
        if _match_cell(entry.data, cell_ident) is not None:
            return []
        lines = [
            f"Cell '{cell_ident}' not found in element {idx} of "
            f"'{prop.name}' on '{format_node(node)}'."
        ]
        if entry.data:
            lines.append("Available cells:")
            lines.extend(f" - {c}" for c in entry.data)
        return lines
    return []


def _check_irq_cell(node: edtlib.Node, irq, cell_ident: str) -> list[str]:
    """Check if a cell exists in an interrupt specifier entry."""
    if _match_cell(irq.data, cell_ident) is not None:
        return []
    lines = [f"Cell '{cell_ident}' not found in interrupt specifier for '{format_node(node)}'."]
    if irq.data:
        lines.append("Available cells:")
        lines.extend(f" - {c}" for c in irq.data)
    return lines


def _diagnose_irq_suffix(node: edtlib.Node, suffix: str) -> list[str]:
    """Diagnose _IRQ_<...> suffix patterns."""
    if not node.interrupts:
        return [f"Node '{format_node(node)}' has no interrupts defined."]

    idx_match = re.match(r'IDX_(\d+)_VAL_(\w+)', suffix)
    if idx_match:
        idx, cell_ident = int(idx_match.group(1)), idx_match.group(2)
        if idx >= len(node.interrupts):
            return [
                f"Interrupt index {idx} out of bounds for '{format_node(node)}'.",
                f"Node has {len(node.interrupts)} interrupt(s).",
            ]
        return _check_irq_cell(node, node.interrupts[idx], cell_ident)

    name_match = re.match(r'NAME_(.+?)_VAL_(\w+)', suffix)
    if name_match:
        name_ident, cell_ident = name_match.group(1), name_match.group(2)
        for irq in node.interrupts:
            if irq.name and gen_defines.str2ident(irq.name) == name_ident:
                return _check_irq_cell(node, irq, cell_ident)
        lines = [f"Interrupt name '{name_ident}' not found on '{format_node(node)}'."]
        named = [irq.name for irq in node.interrupts if irq.name]
        if named:
            lines.append("Available interrupt names:")
            lines.extend(f" - {n}" for n in named)
        return lines

    return [f"Unrecognized IRQ suffix: _IRQ_{suffix}"]


def _diagnose_suffix(node: edtlib.Node, suffix: str) -> list[str]:
    """Given a matched node and the remaining suffix, diagnose what's wrong."""
    if not suffix:
        return [
            f"Node '{format_node(node)}' exists in the devicetree.",
            "The error may be due to macro expansion or preprocessor issues.",
        ]

    suffix = suffix.lstrip("_")

    if suffix.startswith("P_"):
        prop_suffix = suffix[2:]
        prop_name, rest = _match_prop(node, prop_suffix)
        if prop_name is None:
            return _diagnose_missing_property(node, prop_suffix)
        prop = node.props[prop_name]
        idx_match = re.match(r'_IDX_(\d+)(.*)', rest)
        if idx_match:
            return _diagnose_index_and_cell(
                node, prop, int(idx_match.group(1)), idx_match.group(2)
            )
        return []

    if suffix.startswith("IRQ_"):
        return _diagnose_irq_suffix(node, suffix[4:])

    return [f"Unrecognized suffix '_{suffix}' for node '{format_node(node)}'."]


def _diagnose_missing_node(symbol: str) -> list[str]:
    """Diagnose when no EDT node matches the symbol's prefix."""
    s = symbol.removeprefix("DT_")

    if s.startswith("N_NODELABEL_"):
        label = s[12:].split("_P_")[0].split("_IRQ_")[0]
        return [
            f"No node with a label matching '{label}' found in devicetree.\n",
            "Possible causes:",
            " - The node label is misspelled",
            " - The node is not defined in any included devicetree file",
            " - The devicetree overlay containing this node is not being applied",
        ]
    if s.startswith("N_ALIAS_"):
        alias = s[8:].split("_P_")[0].split("_IRQ_")[0]
        return [
            f"No alias matching '{alias}' found in /aliases.",
            "Check the /aliases node in your devicetree files.",
        ]
    if s.startswith("N_INST_"):
        return [
            f"No instance-based node found for '{symbol}'.",
            "Check that the compatible string and instance number are correct.",
        ]
    return [f"Unable to match devicetree symbol: {symbol}"]


def handle_dt_macro_error(edt: edtlib.EDT, symbol: str) -> list[str]:
    """
    Diagnose a DT macro error by forward-matching against EDT data.

    Instead of parsing the macro name back into DTS concepts (which is lossy),
    this builds all possible node identifier prefixes from the EDT using the
    same functions as gen_defines, then matches the symbol to find which node
    it refers to and what part of the suffix is missing or wrong.
    """
    prefix_map = _build_node_prefix_map(edt)
    node, suffix = _find_node_and_suffix(symbol, prefix_map)
    if node is None:
        return _diagnose_missing_node(symbol)
    return _diagnose_suffix(node, suffix)


def find_kconfig_deps(kconf: kconfiglib.Kconfig, dt_has_symbol: str) -> set[str]:
    """
    Find all Kconfig symbols that depend on the provided DT_HAS symbol.
    """
    prefix = os.environ.get("CONFIG_", "CONFIG_")
    target = f"{prefix}{dt_has_symbol}"
    deps = set()

    def collect_syms(expr):
        # Recursively collect all symbol names in the expression tree except the target
        for item in kconfiglib.expr_items(expr):
            if not isinstance(item, kconfiglib.Symbol):
                continue
            sym_name = f"{prefix}{item.name}"
            if sym_name != target:
                deps.add(sym_name)

    for sym in getattr(kconf, "unique_defined_syms", []):
        for node in sym.nodes:
            # Check dependencies
            if node.dep is None:
                continue
            dep_str = kconfiglib.expr_str(
                node.dep,
                lambda sc: f"{prefix}{sc.name}" if hasattr(sc, 'name') and sc.name else str(sc),
            )
            if target in dep_str:
                collect_syms(node.dep)

            # Check selects/implies
            for attr in ["orig_selects", "orig_implies"]:
                for value, _ in getattr(node, attr, []) or []:
                    value_str = kconfiglib.expr_str(value, str)
                    if target in value_str:
                        collect_syms(value)

    return deps


def handle_enabled_node(node: edtlib.Node) -> list[str]:
    """
    Handle diagnosis for an enabled DT node (linker error, one or more Kconfigs might be gating
    the device driver).
    """
    lines = [f"'{format_node(node)}' is enabled but no driver appears to be available for it.\n"]

    compats = list(getattr(node, "compats", []))
    if compats:
        kconf = setup_kconfig()
        deps = set()
        for compat in compats:
            dt_has = f"DT_HAS_{edtlib.str_as_token(compat.upper())}_ENABLED"
            deps.update(find_kconfig_deps(kconf, dt_has))

        if deps:
            lines.append("Try enabling these Kconfig options:\n")
            lines.extend(f" - {dep}=y" for dep in sorted(deps))
    else:
        lines.append("Could not determine compatible; check driver Kconfig manually.")

    return lines


def handle_disabled_node(node: edtlib.Node) -> list[str]:
    """
    Handle diagnosis for a disabled DT node.
    """
    edt = node.edt
    status_prop = node._node.props.get('status')
    lines = [f"'{format_node(node)}' is disabled in {status_prop.filename}:{status_prop.lineno}"]

    # Show dependency
    users = getattr(node, "required_by", [])
    if users:
        lines.append("The following nodes depend on it:")
        lines.extend(f" - {u.path}" for u in users)

    # Show chosen/alias references
    chosen_refs = [
        name
        for name, n in (getattr(edt, "chosen_nodes", {}) or getattr(edt, "chosen", {})).items()
        if n is node
    ]
    alias_refs = [name for name, n in getattr(edt, "aliases", {}).items() if n is node]

    if chosen_refs or alias_refs:
        lines.append("")

    if chosen_refs:
        lines.append(
            "It is referenced as a \"chosen\" in "
            f"""{', '.join([f"'{ref}'" for ref in sorted(chosen_refs)])}"""
        )
    if alias_refs:
        lines.append(
            "It is referenced by the following aliases: "
            f"""{', '.join([f"'{ref}'" for ref in sorted(alias_refs)])}"""
        )

    lines.append("\nTry enabling the node by setting its 'status' property to 'okay'.")

    return lines


def main() -> int:
    args = parse_args()
    edt = load_edt(args.edt_pickle)

    m = re.search(r"__device_dts_ord_([A-Za-z0-9_]+)", args.symbol)
    if m:
        ord_str = m.group(1)
        try:
            dep_ordinal = int(ord_str)
        except ValueError:
            lines = [f"Symbol '{args.symbol}' indicates a macro expansion failure.\n"]
            lines.append(
                "This usually happens when DEVICE_DT_GET() is used with a node identifier "
                "that is not valid or does not resolve to a valid node ordinal."
            )
            print(tabulate([["\n".join(lines)]], headers=["DT Doctor"], tablefmt="grid"))
            return 0

        # Find node by ordinal amongst all nodes
        node = next((n for n in edt.nodes if n.dep_ordinal == dep_ordinal), None)
        if not node:
            print(f"Ordinal {dep_ordinal} not found in edt.pickle", file=sys.stderr)
            return 1

        if node.status == "okay":
            lines = handle_enabled_node(node)
        else:
            lines = handle_disabled_node(node)

        print(tabulate([["\n".join(lines)]], headers=["DT Doctor"], tablefmt="grid"))
        return 0

    # Check if it's a DT macro error (e.g., DT_N_NODELABEL_...)
    if re.match(r"DT_N_(NODELABEL|ALIAS|INST|S)_", args.symbol):
        lines = handle_dt_macro_error(edt, args.symbol)
        print(tabulate([["\n".join(lines)]], headers=["DT Doctor"], tablefmt="grid"))
        return 0

    # Unknown symbol format
    print(f"Unable to diagnose symbol: {args.symbol}", file=sys.stderr)
    print("dtdoctor can diagnose:", file=sys.stderr)
    print("  - __device_dts_ord_<N> symbols", file=sys.stderr)
    print("  - DT_N_NODELABEL_<label>_... macros", file=sys.stderr)
    print("  - DT_N_ALIAS_<alias>_... macros", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
