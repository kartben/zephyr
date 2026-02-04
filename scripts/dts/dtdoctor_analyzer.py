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

import gen_defines


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


def ident2str(ident: str) -> str:
    """
    Convert a lowercase-and-underscores identifier back to devicetree form.

    Note: This conversion is best-effort since the transformation from DTS names
    to C identifiers is lossy (both hyphens and underscores in DTS become
    underscores in C). We assume the more common case where hyphens were
    converted to underscores.
    """
    return ident.replace('_', '-')


def parse_dt_macro(symbol: str) -> dict:
    """
    Parse a DT macro symbol and extract its components.

    Returns a dict with keys like:
    - 'type': 'nodelabel', 'alias', 'inst', or 'path'
    - 'node_ident': the node identifier part
    - 'property': property name (if accessing a property)
    - 'index': index in array (if applicable)
    - 'cell': cell name (if applicable for phandle-array)
    - 'exists': True if this is an _EXISTS macro

    Example: DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin
    Returns: {
        'type': 'nodelabel',
        'node_ident': 'vext',
        'property': 'gpios',
        'index': 0,
        'cell': 'pin'
    }
    """
    result = {}

    # Remove DT_ prefix if present
    if symbol.startswith('DT_'):
        symbol = symbol[3:]

    # Check if it's an _EXISTS macro
    if symbol.endswith('_EXISTS'):
        result['exists'] = True
        symbol = symbol[:-7]  # Remove _EXISTS
    else:
        result['exists'] = False

    # Parse node identifier type
    if symbol.startswith('N_NODELABEL_'):
        result['type'] = 'nodelabel'
        rest = symbol[12:]  # Remove N_NODELABEL_
    elif symbol.startswith('N_ALIAS_'):
        result['type'] = 'alias'
        rest = symbol[8:]  # Remove N_ALIAS_
    elif symbol.startswith('N_INST_'):
        result['type'] = 'inst'
        rest = symbol[7:]  # Remove N_INST_
    elif symbol.startswith('N_S_') or symbol == 'N' or symbol.startswith('N_P_'):
        result['type'] = 'path'
        rest = symbol
    else:
        return None

    if '_P_' in rest:
        parts = rest.split('_P_', 1)
        result['node_ident'] = parts[0]
        prop_part = parts[1]

        # Parse property access pattern
        # Format: <prop>_IDX_<idx>_VAL_<cell> or <prop>_IDX_<idx> or just <prop>

        # Check for _IDX_ pattern
        idx_match = re.search(r'(.+?)_IDX_(\d+)', prop_part)
        if idx_match:
            result['property'] = idx_match.group(1)
            result['index'] = int(idx_match.group(2))

            # Check for _VAL_ pattern after index
            val_match = re.search(r'_IDX_\d+_VAL_(\w+)', prop_part)
            if val_match:
                result['cell'] = val_match.group(1)
        else:
            result['property'] = prop_part
    elif '_IRQ_' in rest:
        parts = rest.split('_IRQ_', 1)
        result['node_ident'] = parts[0]
        irq_part = parts[1]

        # Check for NAME pattern
        name_match = re.search(r'NAME_(.+?)_VAL_(\w+)', irq_part)
        if name_match:
            result['irq_name'] = name_match.group(1)
            result['cell'] = name_match.group(2)
        else:
            # Check for IDX pattern
            idx_match = re.search(r'IDX_(\d+)_VAL_(\w+)', irq_part)
            if idx_match:
                result['irq_index'] = int(idx_match.group(1))
                result['cell'] = idx_match.group(2)
    else:
        result['node_ident'] = rest

    return result


def find_node_by_ident(edt: edtlib.EDT, parsed: dict) -> edtlib.Node:
    """
    Find a node in the EDT based on the parsed identifier.

    Returns the node if found, None otherwise.
    """
    if not parsed:
        return None

    node_type = parsed.get('type')
    node_ident = parsed.get('node_ident')

    if not node_type or not node_ident:
        return None

    if node_type == 'nodelabel':
        for node in edt.nodes:
            for label in node.labels:
                if gen_defines.str2ident(label) == node_ident:
                    return node
    elif node_type == 'alias':
        aliases = edt.aliases if hasattr(edt, 'aliases') else {}
        # Try the alias with hyphens (more common in DTS)
        alias_name = ident2str(node_ident)
        if alias_name in aliases:
            return aliases[alias_name]
        # Also try without conversion (in case original had underscores)
        if node_ident in aliases:
            return aliases[node_ident]
    elif node_type == 'path':
        for node in edt.nodes:
            if gen_defines.node_z_path_id(node) == node_ident:
                return node

    return None


def handle_dt_macro_error(edt: edtlib.EDT, symbol: str) -> list[str]:
    """
    Handle diagnosis for a DT macro error.

    Analyzes symbols like DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin
    and provides meaningful diagnostics.
    """
    return [f"Unable to parse devicetree symbol: {symbol}"]


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
