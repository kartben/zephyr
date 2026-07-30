#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
A script to help diagnose build errors related to Devicetree.

To use this script as a standalone tool, provide the path to an edt.pickle file
(e.g ./build/zephyr/edt.pickle) and a symbol that appeared in the build error
message.

Supported symbol formats:
 - Device ordinal: __device_dts_ord_123
 - Node label: DT_N_NODELABEL_<label>[_P_<property>[_IDX_<index>][_VAL_<cell>]]
 - Alias: DT_N_ALIAS_<alias>[_P_<property>[_IDX_<index>][_VAL_<cell>]]
 - Instance: DT_N_INST_<inst>_<compat>[_P_<property>[_IDX_<index>][_VAL_<cell>]]

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


def ident2str(ident: str) -> str:
    """Convert an identifier back to original string form (reverse of str2ident from gen_defines.py)"""
    # This is a best-effort conversion since the original transformation is lossy
    # str2ident converts [-,.@/+] to '_' and lowercases
    return ident


def parse_dt_symbol(symbol: str, edt: edtlib.EDT) -> dict:
    """
    Parse a DT symbol and extract information about it.
    
    Returns a dict with keys like:
    - 'type': 'device_ord', 'nodelabel', 'alias', 'inst', 'property', etc.
    - 'node': the edtlib.Node if found
    - 'label': node label if applicable
    - 'property': property name if applicable
    - 'index': array index if applicable
    - 'cell': cell name if applicable
    - Additional context-specific information
    """
    result = {'symbol': symbol, 'type': 'unknown', 'node': None}
    
    # Strip DT_ prefix if present
    if symbol.startswith('DT_'):
        symbol = symbol[3:]
    
    # Check for N_NODELABEL pattern - split on _P_ to separate node identifier from property
    # Note: _P_ (uppercase) is safe as a delimiter because str2ident() in gen_defines.py
    # lowercases all identifiers, so _P_ can never appear in a node label identifier
    m = re.match(r'N_NODELABEL_(.+?)(?:_P_(.+))?$', symbol)
    if m and (not m.group(2) or not m.group(1).endswith('_P')):
        # Make sure we didn't split on something like "label_P" at the end
        # by checking if there's actually a _P_ separator (group 2 exists)
        label = ident2str(m.group(1))
        prop_part = m.group(2)
        
        # Find node by label
        node = next((n for n in edt.nodes if label in n.labels), None)
        result.update({
            'type': 'nodelabel',
            'label': label,
            'node': node
        })
        
        if prop_part:
            # Parse property information
            prop_info = parse_property_part(prop_part)
            result.update(prop_info)
        
        return result
    
    # Check for N_ALIAS pattern - split on _P_ to separate node identifier from property
    m = re.match(r'N_ALIAS_(.+?)(?:_P_(.+))?$', symbol)
    if m and (not m.group(2) or not m.group(1).endswith('_P')):
        alias = ident2str(m.group(1))
        prop_part = m.group(2)
        
        # Find node by alias
        node = edt.aliases.get(alias)
        result.update({
            'type': 'alias',
            'alias': alias,
            'node': node
        })
        
        if prop_part:
            prop_info = parse_property_part(prop_part)
            result.update(prop_info)
        
        return result
    
    # Check for N_INST pattern - match instance_number_compatible_P_prop
    m = re.match(r'N_INST_(\d+)_(.+?)(?:_P_(.+))?$', symbol)
    if m and (not m.group(3) or not m.group(2).endswith('_P')):
        inst_num = int(m.group(1))
        compat = ident2str(m.group(2))
        prop_part = m.group(3)
        
        # Find node by instance
        node = None
        if compat in edt.compat2nodes:
            nodes = edt.compat2nodes[compat]
            if inst_num < len(nodes):
                node = nodes[inst_num]
        
        result.update({
            'type': 'instance',
            'instance': inst_num,
            'compatible': compat,
            'node': node
        })
        
        if prop_part:
            prop_info = parse_property_part(prop_part)
            result.update(prop_info)
        
        return result
    
    # Check for path-based pattern N_S_xxx
    m = re.match(r'N(?:_S_[^_]+)+(?:_P_(.+))?$', symbol)
    if m:
        prop_part = m.group(1)
        # For path-based lookups, we'd need to reconstruct the path
        # This is complex, so we'll handle it as best effort
        result['type'] = 'path'
        
        if prop_part:
            prop_info = parse_property_part(prop_part)
            result.update(prop_info)
        
        return result
    
    return result


def parse_property_part(prop_part: str) -> dict:
    """
    Parse the property part of a DT symbol.
    
    Examples:
    - "gpios" -> property gpios
    - "gpios_IDX_0" -> property gpios, index 0
    - "gpios_IDX_0_VAL_pin" -> property gpios, index 0, cell pin
    - "gpios_IDX_0_EXISTS" -> property gpios, index 0, existence check
    """
    result = {}
    
    # Try to match pattern with _IDX_ or _VAL_
    # First check for patterns with IDX and/or VAL
    m = re.match(r'^(.+?)(?:_IDX_(\d+))?(?:_VAL_(\w+))?(?:_(EXISTS|STRING_TOKEN|STRING_UPPER_TOKEN|ENUM_IDX|PH|LEN|NUM))?$', prop_part)
    if m:
        prop_name = m.group(1)
        
        # Make sure we didn't incorrectly parse a property name that ends with _IDX or _VAL
        # by checking if there's actually an index number after _IDX
        if '_IDX_' in prop_part and m.group(2) is None:
            # No index number found, so _IDX is part of the property name
            result['property'] = ident2str(prop_part)
        else:
            result['property'] = ident2str(prop_name)
            
            if m.group(2) is not None:
                result['index'] = int(m.group(2))
            
            if m.group(3):
                result['cell'] = ident2str(m.group(3))
            
            if m.group(4):
                result['suffix'] = m.group(4)
    else:
        # Fallback: treat entire string as property name
        result['property'] = ident2str(prop_part)
    
    return result


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


def handle_property_error(symbol_info: dict, edt: edtlib.EDT) -> list[str]:
    """
    Handle diagnosis for property-related DT symbol errors.
    """
    lines = []
    
    node = symbol_info.get('node')
    label = symbol_info.get('label')
    alias = symbol_info.get('alias')
    prop_name = symbol_info.get('property')
    index = symbol_info.get('index')
    cell = symbol_info.get('cell')
    
    # Build a descriptive header
    if not node:
        if label:
            lines.append(f"Node with label '{label}' does not exist in the devicetree.\n")
            lines.append("Possible causes:")
            lines.append(" - The node label is misspelled")
            lines.append(" - The node is not defined in any .dts or .dtsi file")
            lines.append(" - The node definition is excluded by a conditional")
        elif alias:
            lines.append(f"Alias '{alias}' does not exist in the devicetree.\n")
            lines.append("Possible causes:")
            lines.append(" - The alias is misspelled")
            lines.append(" - The alias is not defined in any .dts or .dtsi file")
        else:
            lines.append("The referenced devicetree node does not exist.\n")
        return lines
    
    # Node exists - check property issues
    if prop_name:
        lines.append(f"Property '{prop_name}' on node '{format_node(node)}' has an issue.\n")
        
        if prop_name not in node.props:
            lines.append(f"Property '{prop_name}' does not exist on this node.")
            lines.append(f"\nNode path: {node.path}")
            if node.matching_compat:
                lines.append(f"Compatible: {node.matching_compat}")
            lines.append("\nPossible causes:")
            lines.append(f" - The property '{prop_name}' is not defined for this node")
            lines.append(" - The property name is misspelled in the source code")
            lines.append(" - The binding does not specify this property")
            
            # Show available properties
            if node.props:
                lines.append("\nAvailable properties on this node:")
                for p in sorted(node.props.keys()):
                    lines.append(f" - {p}")
        else:
            prop = node.props[prop_name]
            
            # Check index-related issues
            if index is not None:
                prop_len = len(prop.val) if isinstance(prop.val, list) else 1
                if index >= prop_len:
                    lines.append(f"Index {index} is out of bounds for property '{prop_name}'.")
                    lines.append(f"The property has {prop_len} element(s), valid indices are 0-{prop_len-1}.")
                    lines.append(f"\nProperty type: {prop.type}")
                    lines.append(f"Property value: {prop.val}")
                
                # Check cell-related issues for phandle-array properties
                elif cell and prop.type == 'phandle-array':
                    if index < len(prop.val) and prop.val[index] is not None:
                        entry = prop.val[index]
                        if cell not in entry.data:
                            lines.append(f"Cell '{cell}' does not exist at index {index}.")
                            lines.append(f"\nController: {entry.controller.path}")
                            lines.append("Available cells at this index:")
                            for c in entry.data.keys():
                                lines.append(f" - {c}: {entry.data[c]}")
                        else:
                            lines.append(f"Cell '{cell}' at index {index}: {entry.data[cell]}")
                            lines.append("\nThis appears to be defined correctly.")
                            lines.append("The error may be in how the symbol is used in C code.")
                    else:
                        lines.append(f"Phandle-array entry at index {index} is unspecified (None).")
                        lines.append("This typically means the property has an empty placeholder.")
            else:
                # General property info
                lines.append(f"Property type: {prop.type}")
                if isinstance(prop.val, list):
                    lines.append(f"Property has {len(prop.val)} element(s)")
                else:
                    lines.append(f"Property value: {prop.val}")
    else:
        # Node reference without property
        lines.append(f"Node '{format_node(node)}' exists in the devicetree.")
        lines.append(f"\nPath: {node.path}")
        lines.append(f"Status: {node.status}")
        if node.matching_compat:
            lines.append(f"Compatible: {node.matching_compat}")
    
    return lines


def main() -> int:
    args = parse_args()

    # Try to match __device_dts_ord_XXX pattern
    m = re.search(r"__device_dts_ord_(\d+)", args.symbol)
    if m:
        # Original behavior for device ordinal symbols
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
    
    # Try to parse as a DT property symbol (e.g., DT_N_NODELABEL_xxx_P_yyy_IDX_z_VAL_w)
    edt = load_edt(args.edt_pickle)
    symbol_info = parse_dt_symbol(args.symbol, edt)
    
    if symbol_info['type'] != 'unknown':
        lines = handle_property_error(symbol_info, edt)
        print(tabulate([["\n".join(lines)]], headers=["DT Doctor"], tablefmt="grid"))
        return 0
    
    # Unknown symbol format
    print(f"Unable to parse symbol: {args.symbol}", file=sys.stderr)
    print("Supported symbol formats:", file=sys.stderr)
    print(" - __device_dts_ord_<ordinal>", file=sys.stderr)
    print(" - DT_N_NODELABEL_<label>[_P_<property>[_IDX_<index>][_VAL_<cell>]]", file=sys.stderr)
    print(" - DT_N_ALIAS_<alias>[_P_<property>[_IDX_<index>][_VAL_<cell>]]", file=sys.stderr)
    print(" - DT_N_INST_<inst>_<compat>[_P_<property>[_IDX_<index>][_VAL_<cell>]]", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
