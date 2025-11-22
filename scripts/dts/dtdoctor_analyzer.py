#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
A script to help diagnose build errors related to Devicetree.

To use this script as a standalone tool, provide the path to an edt.pickle file
(e.g ./build/zephyr/edt.pickle) and a symbol that appeared in the build error
message (e.g. __device_dts_ord_123).

Example usage:

./scripts/dts/dtdoctor_analyzer.py \\
    --edt-pickle ./build/zephyr/edt.pickle \\
    --symbol __device_dts_ord_123

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

    # Add macro usage hints
    add_macro_usage_hints(node, lines)

    return lines


def find_nodes_with_parent(edt: edtlib.EDT, parent_node: edtlib.Node) -> list[edtlib.Node]:
    """
    Find all nodes that have the given node as their parent.
    """
    children = []
    for node in edt.nodes:
        if node.parent is parent_node:
            children.append(node)
    return children


def find_nodes_with_grandparent(edt: edtlib.EDT, grandparent_node: edtlib.Node) -> list[edtlib.Node]:
    """
    Find all nodes that have the given node as their grandparent.
    """
    grandchildren = []
    for node in edt.nodes:
        if node.parent and node.parent.parent is grandparent_node:
            grandchildren.append(node)
    return grandchildren


def add_macro_usage_hints(node: edtlib.Node, lines: list[str]) -> None:
    """
    Add hints about potential macro usage (DT_PARENT, DT_GPARENT, etc.) based on
    node relationships in the devicetree.
    """
    edt = node.edt
    
    # Check if this node is a parent of other nodes
    children = find_nodes_with_parent(edt, node)
    if children:
        lines.append("\nThis node is the parent of:")
        for child in children[:5]:  # Limit to 5 for readability
            lines.append(f" - {format_node(child)}")
        if len(children) > 5:
            lines.append(f" ... and {len(children) - 5} more")
        lines.append("\nIf you're using DT_PARENT() to access this node from one of its children,")
        lines.append("consider the suggestions above to make this node available.")
    
    # Check if this node is a grandparent of other nodes
    grandchildren = find_nodes_with_grandparent(edt, node)
    if grandchildren:
        lines.append("\nThis node is the grandparent of:")
        for grandchild in grandchildren[:5]:  # Limit to 5 for readability
            lines.append(f" - {format_node(grandchild)}")
        if len(grandchildren) > 5:
            lines.append(f" ... and {len(grandchildren) - 5} more")
        lines.append("\nIf you're using DT_GPARENT() to access this node from one of its grandchildren,")
        lines.append("consider the suggestions above to make this node available.")


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

    # Add macro usage hints
    add_macro_usage_hints(node, lines)

    lines.append("\nTry enabling the node by setting its 'status' property to 'okay'.")

    return lines


def main() -> int:
    args = parse_args()

    m = re.search(r"__device_dts_ord_(\d+)", args.symbol)
    if not m:
        return 1

    # Find node by ordinal amongst all nodes
    edt = load_edt(args.edt_pickle)
    node = next((n for n in edt.nodes if n.dep_ordinal == int(m.group(1))), None)
    if not node:
        print(f"Ordinal {m.group(1)} not found in edt.pickle", file=sys.stderr)
        return 1

    if node.status == "okay":
        lines = handle_enabled_node(node)
    else:
        lines = handle_disabled_node(node)

    print(tabulate([["\n".join(lines)]], headers=["DT Doctor"], tablefmt="grid"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
