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


def ident2str(ident: str) -> str:
    """Convert a lowercase-and-underscores identifier back to devicetree form."""
    # For most cases, hyphens become underscores, but we keep underscores as-is
    # This is a best-effort conversion since the transformation is lossy
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
    elif symbol.startswith('N_S_'):
        result['type'] = 'path'
        rest = symbol[4:]  # Remove N_S_
    else:
        # Unknown format
        return None
    
    # Split by _P_ to separate node identifier from property access
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
            # No index, just property name
            result['property'] = prop_part
    else:
        # No property access, just node identifier
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
        # Search for node with this label
        for node in edt.nodes:
            for label in node.labels:
                # Convert label to lowercase-and-underscores for comparison
                label_ident = edtlib.str_as_token(label.lower())
                if label_ident == node_ident:
                    return node
    elif node_type == 'alias':
        # Look up in aliases
        aliases = getattr(edt, 'aliases', {})
        # Try the alias with hyphens
        alias_name = ident2str(node_ident)
        if alias_name in aliases:
            return aliases[alias_name]
        # Also try without conversion
        if node_ident in aliases:
            return aliases[node_ident]
    elif node_type == 'path':
        # Reconstruct path from path identifier
        # Path identifiers use _S_ for / separators
        path_parts = node_ident.split('_S_')
        path = '/' + '/'.join(ident2str(part) for part in path_parts if part)
        return edt.get_node(path)
    
    return None


def handle_dt_macro_error(edt: edtlib.EDT, symbol: str) -> list[str]:
    """
    Handle diagnosis for a DT macro error.
    
    Analyzes symbols like DT_N_NODELABEL_vext_P_gpios_IDX_0_VAL_pin
    and provides meaningful diagnostics.
    """
    parsed = parse_dt_macro(symbol)
    
    if not parsed:
        return [f"Unable to parse devicetree symbol: {symbol}"]
    
    lines = []
    
    # Try to find the node
    node = find_node_by_ident(edt, parsed)
    
    if not node:
        # Node not found
        node_type = parsed.get('type')
        node_ident = parsed.get('node_ident')
        
        if node_type == 'nodelabel':
            lines.append(f"Node with label '{ident2str(node_ident)}' not found in devicetree.\n")
            lines.append("Possible causes:")
            lines.append(" - The node label is misspelled")
            lines.append(" - The node is not defined in any devicetree file")
            lines.append(" - The devicetree overlay containing this node is not being included")
            lines.append("\nCheck your devicetree files and overlays.")
        elif node_type == 'alias':
            lines.append(f"Alias '{ident2str(node_ident)}' not found in /aliases.\n")
            lines.append("Check the /aliases node in your devicetree files.")
        else:
            lines.append(f"Node not found for identifier: {node_ident}")
        
        return lines
    
    # Node was found, check if it's a property/cell access issue
    if 'property' in parsed:
        prop_name = ident2str(parsed['property'])
        
        # Check if property exists
        if prop_name not in node.props:
            lines.append(f"Property '{prop_name}' not found on node '{format_node(node)}'.\n")
            
            # List available properties
            if node.props:
                lines.append("Available properties on this node:")
                for pname in sorted(node.props.keys()):
                    lines.append(f" - {pname}")
            else:
                lines.append("This node has no properties.")
            
            lines.append("\nCheck your devicetree binding and node definition.")
            return lines
        
        prop = node.props[prop_name]
        
        # Check index access
        if 'index' in parsed:
            idx = parsed['index']
            
            # For phandle-array properties, check if index is valid
            if prop.type == 'phandle-array':
                if idx >= len(prop.val):
                    lines.append(
                        f"Index {idx} is out of bounds for property '{prop_name}' "
                        f"on node '{format_node(node)}'.\n"
                    )
                    lines.append(f"Property has {len(prop.val)} element(s), valid indices are 0-{len(prop.val)-1}.")
                    return lines
                
                # Check if the element at this index is None (unspecified)
                if prop.val[idx] is None:
                    lines.append(
                        f"Element at index {idx} of property '{prop_name}' "
                        f"on node '{format_node(node)}' is unspecified.\n"
                    )
                    lines.append("This means the phandle-array has a gap at this position.")
                    return lines
                
                # Check cell access
                if 'cell' in parsed:
                    cell_name = ident2str(parsed['cell'])
                    entry = prop.val[idx]
                    
                    if cell_name not in entry.data:
                        lines.append(
                            f"Cell '{cell_name}' not found in element {idx} of "
                            f"property '{prop_name}' on node '{format_node(node)}'.\n"
                        )
                        
                        # Show available cells
                        if entry.data:
                            lines.append("Available cells:")
                            for cname in entry.data.keys():
                                lines.append(f" - {cname}")
                        
                        # Show the controller node's binding info
                        if entry.controller:
                            lines.append(
                                f"\nThe phandle points to '{format_node(entry.controller)}'."
                            )
                            if hasattr(entry.controller, 'binding') and entry.controller.binding:
                                binding = entry.controller.binding
                                if hasattr(binding, 'specifier_cells') and binding.specifier_cells:
                                    lines.append(
                                        f"Expected cells based on binding: "
                                        f"{', '.join(binding.specifier_cells)}"
                                    )
                        
                        return lines
            else:
                # Not a phandle-array but trying to use index access
                lines.append(
                    f"Property '{prop_name}' on node '{format_node(node)}' "
                    f"is of type '{prop.type}', not 'phandle-array'.\n"
                )
                lines.append("Index-based access with _IDX_ is only valid for phandle-array properties.")
                return lines
    
    # If we get here, the macro exists but there might be another issue
    lines.append(f"Devicetree symbol '{symbol}' appears to be correctly formed.\n")
    lines.append(
        "The node exists and the property/index/cell access pattern is valid. "
        "The error may be due to:"
    )
    lines.append(" - Using the symbol in an unsupported context")
    lines.append(" - Macro expansion or preprocessor issues")
    lines.append(" - Check that you're using the correct macro for your use case")
    
    return lines


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

    # Check if it's a __device_dts_ord_ error
    m = re.search(r"__device_dts_ord_(\d+)", args.symbol)
    if m:
        # Find node by ordinal amongst all nodes
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
