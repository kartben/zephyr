#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
A script to help diagnose build errors related to Devicetree.

To use this script as a standalone tool, provide the path to an edt.pickle file
(e.g ./build/zephyr/edt.pickle) and a symbol that appeared in the build error
message.

Supported symbol forms include:

- __device_dts_ord_<N>            device symbol for a devicetree node
- DT_N_ALIAS_<name>...            nonexistent devicetree alias
- DT_N_NODELABEL_<name>...        nonexistent node label
- DT_N_INST_<n>_<compat>...       nonexistent instance of a compatible
- DT_N_S_<path>...                nonexistent node path or missing property
- DT_CHOSEN_<name>...             missing /chosen property

Example usage:

./scripts/dts/dtdoctor_analyzer.py \\
    --edt-pickle ./build/zephyr/edt.pickle \\
    --symbol __device_dts_ord_123

"""

import argparse
import difflib
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


def str2ident(s: str) -> str:
    """
    Convert 's' to the form gen_defines.py uses in devicetree C identifiers.
    """
    return re.sub("[-,.@/+]", "_", s.lower())


def node_path_ident(node: edtlib.Node) -> str:
    """
    Return the 'DT_N...' identifier corresponding to the node's path, mirroring
    node_z_path_id() in gen_defines.py.
    """
    components = ["DT_N"]
    if node.parent is not None:
        components.extend(f"S_{str2ident(component)}" for component in node.path.split("/")[1:])
    return "_".join(components)


def format_node(node: edtlib.Node) -> str:
    return f"{node.labels[0]}: {node.path}" if node.labels else node.path


def strip_macro_suffix(ident: str) -> str:
    """
    Strip trailing devicetree macro machinery (e.g. '_P_gpios_IDX_0_PH_ORD') from a
    mangled devicetree name. Mangled names are always lowercase (see str2ident) while
    macro suffixes are uppercase, so cut at the first '_' followed by an uppercase
    letter.
    """
    m = re.search(r"_[A-Z]", ident)
    return ident[: m.start()] if m else ident


def longest_ident_match(tail: str, idents) -> str | None:
    """
    Return the longest identifier from 'idents' that 'tail' is, or starts with
    (followed by '_'), or None.
    """
    matches = [ident for ident in idents if tail == ident or tail.startswith(ident + "_")]
    return max(matches, key=len) if matches else None


def suggest(name: str, candidates: dict[str, str], intro: str) -> list[str]:
    """
    Return "did you mean" lines for 'name' against 'candidates', a dict mapping
    identifier forms to display strings.
    """
    close = difflib.get_close_matches(name, candidates.keys(), n=3, cutoff=0.6)
    if not close:
        return []
    return [intro] + [f" - {candidates[c]}" for c in close]


def overlay_ref(node: edtlib.Node) -> str:
    """
    Return an overlay reference for the node: '&label' if it has a label, else
    a path-based reference.
    """
    return f"&{node.labels[0]}" if node.labels else f"&{{{node.path}}}"


def prop_placeholder(spec: edtlib.PropertySpec) -> str:
    """
    Return an example overlay assignment for a property of the given spec.
    """
    placeholders = {
        "boolean": f"{spec.name};",
        "string": f'{spec.name} = "...";',
        "string-array": f'{spec.name} = "...", "...";',
        "uint8-array": f"{spec.name} = [de ad be ef];",
        "phandle": f"{spec.name} = <&some_node>;",
        "phandles": f"{spec.name} = <&some_node &other_node>;",
        "phandle-array": f"{spec.name} = <&some_node 1 2>;",
        "path": f"{spec.name} = &some_node;",
    }
    return placeholders.get(spec.type, f"{spec.name} = <...>;")


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


def handle_enabled_node(node: edtlib.Node, kconf_factory=setup_kconfig) -> list[str]:
    """
    Handle diagnosis for an enabled DT node (linker error, one or more Kconfigs might be gating
    the device driver).
    """
    lines = [f"'{format_node(node)}' is enabled but no driver appears to be available for it.\n"]

    compats = list(getattr(node, "compats", []))
    if not compats:
        lines.append("Could not determine compatible; check driver Kconfig manually.")
        return lines

    kconf = None
    if kconf_factory is not None:
        try:
            kconf = kconf_factory()
        except Exception:
            kconf = None

    deps = set()
    if kconf is not None:
        for compat in compats:
            dt_has = f"DT_HAS_{edtlib.str_as_token(compat.upper())}_ENABLED"
            deps.update(find_kconfig_deps(kconf, dt_has))

    if deps:
        lines.append("Try enabling these Kconfig options:\n")
        lines.extend(f" - {dep}=y" for dep in sorted(deps))
    else:
        compat_list = ", ".join(f"'{c}'" for c in compats)
        lines.append(
            f"Check that a driver for compatible {compat_list} exists and that\n"
            "the Kconfig options gating it are enabled."
        )

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
    alias_refs = [name for name, n in _alias2node(edt).items() if n is node]

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


def _alias2node(edt: edtlib.EDT) -> dict[str, edtlib.Node]:
    return {alias: node for node in edt.nodes for alias in node.aliases}


def diagnose_ordinal(edt: edtlib.EDT, ordinal: int, kconf_factory) -> list[str]:
    node = next((n for n in edt.nodes if n.dep_ordinal == ordinal), None)
    if not node:
        return [
            f"No devicetree node with dependency ordinal {ordinal} was found.\n",
            "The build directory may be out of date; try a pristine build.",
        ]

    if node.status == "okay":
        return handle_enabled_node(node, kconf_factory)
    return handle_disabled_node(node)


def diagnose_missing_alias(edt: edtlib.EDT, rest: str) -> list[str]:
    name = strip_macro_suffix(rest)
    aliases = _alias2node(edt)
    candidates = {
        str2ident(alias): f"{alias} ({format_node(node)})" for alias, node in aliases.items()
    }

    lines = [
        f"Devicetree alias '{name}' does not exist.\n",
        "Note: in this identifier, characters like '-' in the actual alias name",
        "appear as '_'.\n",
    ]
    lines.extend(suggest(name, candidates, "Did you mean one of these existing aliases?"))
    lines.extend(
        [
            "\nTo define the alias, add something like this to your devicetree overlay:\n",
            "    / {",
            "            aliases {",
            f"                    {name.replace('_', '-')} = &some_node_label;",
            "            };",
            "    };",
        ]
    )
    return lines


def diagnose_missing_nodelabel(edt: edtlib.EDT, rest: str) -> list[str]:
    name = strip_macro_suffix(rest)
    candidates = {
        str2ident(label): f"{label} ({node.path})" for label, node in edt.label2node.items()
    }

    lines = [
        f"No devicetree node has the node label '{name}'.\n",
        "Note: DT_NODELABEL() expects the lowercase-and-underscores form of the label.\n",
    ]
    lines.extend(suggest(name, candidates, "Did you mean one of these existing node labels?"))
    if not candidates:
        lines.append("The final devicetree does not contain any labeled nodes.")
    return lines


def diagnose_missing_chosen(edt: edtlib.EDT, rest: str) -> list[str]:
    name = strip_macro_suffix(rest)
    chosen = getattr(edt, "chosen_nodes", {}) or {}
    candidates = {
        str2ident(cname): f"{cname} ({format_node(node)})" for cname, node in chosen.items()
    }

    lines = [
        f"The devicetree has no chosen property named '{name}'.\n",
        "Note: in this identifier, characters like ',' or '-' in the actual chosen",
        "property name appear as '_'.\n",
    ]
    lines.extend(suggest(name, candidates, "Did you mean one of these existing chosen properties?"))
    lines.extend(
        [
            "\nTo define it, add something like this to your devicetree overlay:\n",
            "    / {",
            "            chosen {",
            "                    some,property = &some_node_label;",
            "            };",
            "    };",
        ]
    )
    return lines


def diagnose_instance(edt: edtlib.EDT, rest: str) -> list[str]:
    m = re.match(r"(\d+)_(.*)", rest)
    if not m:
        return [f"Could not parse instance identifier 'DT_N_INST_{rest}'."]

    inst = int(m.group(1))
    tail = m.group(2)

    compat_idents = {str2ident(compat): compat for compat in edt.compat2nodes}
    match = longest_ident_match(tail, compat_idents)

    if match is None:
        name = strip_macro_suffix(tail)
        lines = [f"No devicetree node has a compatible matching '{name}'.\n"]
        lines.extend(suggest(name, compat_idents, "Did you mean one of these compatibles?"))
        return lines

    compat = compat_idents[match]
    okay = edt.compat2okay.get(compat, [])
    lines = [f"There is no instance {inst} of compatible '{compat}'.\n"]
    if okay:
        lines.append(
            f"There are {len(okay)} enabled node(s) with this compatible "
            f"(valid instance numbers: 0 to {len(okay) - 1}):"
        )
        lines.extend(f" - {format_node(n)}" for n in okay)
    else:
        lines.append(f"No node with compatible '{compat}' is enabled.")

    disabled = [n for n in edt.compat2nodes.get(compat, []) if n.status != "okay"]
    if disabled:
        lines.append("\nThese nodes have the right compatible but are not enabled:")
        lines.extend(f" - {format_node(n)} (status: {n.status})" for n in disabled)
        lines.append("\nTry setting their 'status' property to 'okay'.")

    return lines


def diagnose_missing_property(edt: edtlib.EDT, node: edtlib.Node, rest: str) -> list[str]:
    binding_specs = node.binding.prop2specs if node.binding else {}
    candidates = {str2ident(name): name for name in {*binding_specs, *node.props}}
    match = longest_ident_match(rest, candidates)

    if match is None:
        name = strip_macro_suffix(rest)
        lines = [f"Node '{format_node(node)}' has no property '{name}'.\n"]
        lines.extend(suggest(name, candidates, "Did you mean one of these properties of the node?"))
        lines.append(
            "\nNote: devicetree macros are only generated for properties that are"
            "\ndeclared in the node's binding."
        )
        if node.binding_path:
            lines.append(f"Binding: {node.binding_path}")
        elif node.compats:
            lines.append(f"No binding was found for compatible(s): {', '.join(node.compats)}")
        else:
            lines.append("The node has no 'compatible' property, so it has no binding.")
        return lines

    name = candidates[match]
    suffix = rest[len(match) :]

    if name in node.props:
        prop = node.props[name]
        m = re.match(r"_IDX_(\d+)", suffix)
        if m and isinstance(prop.val, list):
            idx = int(m.group(1))
            if idx >= len(prop.val):
                return [
                    f"Index {idx} of property '{name}' on node '{format_node(node)}' "
                    "is out of range.\n",
                    f"The property only has {len(prop.val)} element(s) "
                    f"(valid indexes: 0 to {len(prop.val) - 1}).",
                ]
        return [
            f"Property '{name}' on node '{format_node(node)}' is set, but the",
            "accessed macro does not exist.\n",
            f"Check that the devicetree API used matches the property's type "
            f"('{prop.spec.type if prop.spec else 'unknown'}').",
        ]

    # Property is declared in the binding but not set on the node
    spec = binding_specs[name]
    lines = [f"Node '{format_node(node)}' does not set the '{name}' property.\n"]
    if spec.description:
        lines.append(f"Description: {spec.description.strip().splitlines()[0]}")
    lines.append(f"Type: {spec.type}")
    lines.append(f"Binding: {node.binding_path}\n")
    lines.extend(
        [
            "To set it, add something like this to your devicetree overlay:\n",
            f"    {overlay_ref(node)} {{",
            f"            {prop_placeholder(spec)}",
            "    };",
        ]
    )
    return lines


def diagnose_path(edt: edtlib.EDT, sym: str) -> list[str]:
    id2node = {node_path_ident(node): node for node in edt.nodes}

    ancestor_ident = longest_ident_match(sym, id2node) or "DT_N"
    ancestor = id2node.get(ancestor_ident)
    rest = sym[len(ancestor_ident) :]

    if ancestor is not None and not rest:
        # The node exists; a bare node identifier ending up in an error message is unusual.
        return [
            f"Node '{format_node(ancestor)}' exists, but the symbol '{sym}' was not resolved.\n",
            "Check that the node identifier is passed to a devicetree API macro.",
        ]

    if ancestor is not None and rest.startswith("_P_"):
        return diagnose_missing_property(edt, ancestor, rest[len("_P_") :])

    # Nonexistent node path. Reconstruct the attempted path, cutting off any
    # macro suffix (uppercase markers other than the '_S_' path separators).
    m = re.search(r"_(?!S_)[A-Z]", rest)
    tail = rest[: m.start()] if m else rest
    base = "" if ancestor is None or ancestor.path == "/" else ancestor.path
    attempted = base + "/" + "/".join(seg for seg in tail.split("_S_") if seg)
    attempted_ident = sym[: len(ancestor_ident)] + tail

    lines = [
        f"No node with path '{attempted}' exists in the final devicetree.\n",
        "Note: in this path, characters like '@', '-' or ',' appear as '_'.\n",
    ]

    if ancestor is not None:
        where = "the root node" if ancestor.path == "/" else f"'{format_node(ancestor)}'"
        lines.append(f"The closest existing ancestor is {where}, whose children are:")
        lines.extend(f" - {name}" for name in ancestor.children)
        lines.append("")

    lines.extend(
        suggest(
            attempted_ident,
            {ident: node.path for ident, node in id2node.items()},
            "Did you mean one of these existing nodes?",
        )
    )
    return lines


def diagnose(edt: edtlib.EDT, symbol: str, kconf_factory=setup_kconfig) -> list[str] | None:
    """
    Diagnose the given symbol against the given EDT. Returns a list of lines with
    troubleshooting information, or None if the symbol is not recognized.
    """
    sym = symbol.strip()

    m = re.fullmatch(r"__device_dts_ord_(\d+)", sym)
    if m:
        return diagnose_ordinal(edt, int(m.group(1)), kconf_factory)

    # DEVICE_DT_GET() on an unresolved node identifier, e.g.
    # __device_dts_ord_DT_N_NODELABEL_foo_ORD: diagnose the inner identifier.
    m = re.fullmatch(r"__device_dts_ord_(DT_\w+?)(_ORD)?", sym)
    if m:
        return diagnose(edt, m.group(1), kconf_factory)

    prefix_handlers = {
        "DT_N_ALIAS_": diagnose_missing_alias,
        "DT_N_NODELABEL_": diagnose_missing_nodelabel,
        "DT_N_INST_": diagnose_instance,
        "DT_CHOSEN_": diagnose_missing_chosen,
    }
    for prefix, handler in prefix_handlers.items():
        if sym.startswith(prefix):
            return handler(edt, sym[len(prefix) :])

    if sym.startswith("DT_N_S_"):
        return diagnose_path(edt, sym)

    return None


def main() -> int:
    args = parse_args()

    edt = load_edt(args.edt_pickle)
    lines = diagnose(edt, args.symbol)
    if lines is None:
        print(f"Symbol '{args.symbol}' does not look like a devicetree symbol", file=sys.stderr)
        return 1

    print(tabulate([["\n".join(lines)]], headers=["DT Doctor"], tablefmt="grid"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
