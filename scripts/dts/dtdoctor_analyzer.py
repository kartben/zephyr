#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors

"""
A script to help diagnose build errors related to Devicetree.

To use this script as a standalone tool, provide the path to an edt.pickle file
(e.g ./build/zephyr/edt.pickle) and a symbol that appeared in the build error
message (e.g. __device_dts_ord_123, or an unexpanded DT_N_* macro).

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
from collections.abc import Iterable
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parent / "python-devicetree" / "src"))
sys.path.insert(0, str(Path(__file__).parents[1] / "kconfig"))

import gen_defines
import kconfiglib
from devicetree import edtlib
from tabulate import tabulate

# A device symbol whose node identifier resolved: the ordinal is all that is left of it.
DEVICE_ORDINAL_RE = re.compile(r"__device_dts_ord_(\d+)\b")

# A devicetree macro that leaked into the build error because something in it does not
# exist, either on its own or pasted onto a device symbol by DEVICE_DT_GET().
DT_MACRO_RE = re.compile(r"(?:DT_N_|DT_CHOSEN_)\w+")

# The alternate spellings gen_defines.py emits for a node, besides its path. A node
# identifier written in one of these namespaces can never be confused with a path.
ALT_ID_PREFIXES = ("DT_N_NODELABEL_", "DT_N_ALIAS_", "DT_N_INST_", "DT_CHOSEN_")

# What <devicetree.h> appends to a node identifier always starts with an upper case
# component ('_ORD', '_P_', '_REG_IDX_0', ...), while gen_defines.py lower-cases the names
# it builds identifiers from. So the first upper case component is where the name ends,
# which is how a name gets recovered from a macro that has no generated counterpart.
NODE_ID_SUFFIX_RE = re.compile(r"_[A-Z].*$")

# A specifier accessor: an entry of a phandle-array, 'interrupts' or 'reg' picked by index
# or by name, and optionally a cell within it. Relies on the same upper/lower case split as
# NODE_ID_SUFFIX_RE, which is what keeps '_VAL_' from being read as part of a name.
SPECIFIER_RE = re.compile(
    r"^_(?:IDX_(?P<idx>\d+)|NAME_(?P<name>[a-z0-9_]+))"
    r"(?:_VAL_(?P<cell>[a-z0-9_]+))?"
    r"(?:_(?P<extra>[A-Z][A-Z0-9_]*))?$"
)


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


def setup_kconfig() -> kconfiglib.Kconfig | None:
    zephyr_base = os.environ.get("ZEPHYR_BASE")
    if not zephyr_base:
        return None
    return kconfiglib.Kconfig(os.path.join(zephyr_base, "Kconfig"), warn=False)


def format_node(node: edtlib.Node) -> str:
    return f"{node.labels[0]}: {node.path}" if node.labels else node.path


def status_location(node: edtlib.Node) -> str:
    prop = node._node.props.get('status')
    return f"{prop.filename}:{prop.lineno}"


def disabled_ancestor_lines(node: edtlib.Node) -> list[str]:
    """One line per disabled ancestor of the node, nearest first."""
    lines = []
    anc = node.parent
    while anc is not None:
        if anc.status != "okay":
            rel = "parent" if anc is node.parent else "ancestor"
            lines.append(f"Its {rel} '{format_node(anc)}' is disabled in {status_location(anc)}.")
        anc = anc.parent
    return lines


def as_c_token(name: str) -> str:
    """
    Spell out how a DTS name has to be written in C, when the two differ.
    """
    ident = gen_defines.str2ident(name)
    return f"   (in C: {ident})" if ident != name else ""


def close_matches(key: str, candidates: Iterable[str]) -> list[str]:
    """
    Fuzzy-match 'key' against 'candidates', which are spelled as they are in DTS.

    Matching happens on the str2ident()-mangled spelling, since that is what ends up in
    macro names, but the DTS spelling is what gets suggested back to the user.
    """
    mangled = {gen_defines.str2ident(candidate): candidate for candidate in candidates}
    return [mangled[m] for m in difflib.get_close_matches(key, list(mangled), n=3, cutoff=0.6)]


def name_and_matches(tail: str, candidates: Iterable[str]) -> tuple[str, list[str]]:
    """
    Recover the name from the tail of a node identifier that names nothing, along with
    whatever known names resemble it.

    The name is reported alongside the suggestions so that a devicetree name containing
    an upper case component (which gen_defines.py cannot produce, but a typo in C can) is
    visible in the output rather than silently misleading.
    """
    name = NODE_ID_SUFFIX_RE.sub("", tail)
    return name, close_matches(name, candidates)


def suggestion_lines(matches: list[str], show_c_token: bool = False) -> list[str]:
    """
    Format "did you mean" suggestions, or nothing at all if there are none.
    """
    if not matches:
        return []
    return [
        "Did you mean one of these?\n",
        *(f" - {m}{as_c_token(m) if show_c_token else ''}" for m in matches),
        "",
    ]


def build_node_id_map(edt: edtlib.EDT) -> dict[str, edtlib.Node]:
    """
    Map every node identifier macro gen_defines.py generates to the node it names.

    This is what lets an unexpanded macro from a build error be traced back to a node, so
    the name mangling is taken from gen_defines.py rather than reimplemented here: the two
    drifting apart would silently resolve macros to the wrong node.
    """
    node_ids: dict[str, edtlib.Node] = {}

    for node in edt.nodes:
        node_ids[f"DT_{gen_defines.node_z_path_id(node)}"] = node

        for label in node.labels:
            node_ids[f"DT_N_NODELABEL_{gen_defines.str2ident(label)}"] = node

        for alias in node.aliases:
            node_ids[f"DT_N_ALIAS_{gen_defines.str2ident(alias)}"] = node

        for compat in node.compats:
            instance_no = edt.compat2nodes[compat].index(node)
            node_ids[f"DT_N_INST_{instance_no}_{gen_defines.str2ident(compat)}"] = node

    for name, node in edt.chosen_nodes.items():
        node_ids[f"DT_CHOSEN_{gen_defines.str2ident(name)}"] = node

    return node_ids


def node_id_namespace(macro: str) -> str:
    """
    Return the namespace a node identifier is written in.

    Only identifiers from the same namespace may be matched against each other: a path
    identifier is a prefix of every path identifier below it, and 'DT_N_' alone is the
    root node, so without this every DT_N_* macro would resolve to some ancestor.
    """
    return next((prefix for prefix in ALT_ID_PREFIXES if macro.startswith(prefix)), "path")


def split_node_id(macro: str, node_ids: dict[str, edtlib.Node]) -> tuple[str | None, str]:
    """
    Split 'macro' into the longest node identifier it starts with, and the rest.

    Returns (None, macro) when no node identifier matches, which means the node itself is
    what does not exist.
    """
    namespace = node_id_namespace(macro)
    match = max(
        (
            node_id
            for node_id in node_ids
            if node_id_namespace(node_id) == namespace
            and (macro == node_id or macro.startswith(f"{node_id}_"))
        ),
        key=len,
        default=None,
    )
    if match is None:
        return None, macro

    return match, macro[len(match) :]


def all_aliases(edt: edtlib.EDT) -> list[str]:
    return sorted({alias for node in edt.nodes for alias in node.aliases})


def find_kconfig_deps(kconf: kconfiglib.Kconfig, dt_has_symbol: str) -> set[str]:
    """
    Find all Kconfig symbols that depend on the provided DT_HAS symbol.
    """
    prefix = os.environ.get("CONFIG_", "CONFIG_")
    target = f"{prefix}{dt_has_symbol}"
    # Word-boundary match so e.g. DT_HAS_FOO_ENABLED doesn't match DT_HAS_FOO_ENABLED_EXT
    target_re = re.compile(rf"(?<!\w){re.escape(target)}(?!\w)")
    deps = set()

    def expr_to_str(expr):
        return kconfiglib.expr_str(
            expr,
            lambda sc: f"{prefix}{sc.name}" if hasattr(sc, 'name') and sc.name else str(sc),
        )

    def collect_syms(expr):
        # Recursively collect all symbol names in the expression tree except the target
        for item in kconfiglib.expr_items(expr):
            if not isinstance(item, kconfiglib.Symbol):
                continue
            sym_name = f"{prefix}{item.name}"
            if sym_name != target:
                deps.add(sym_name)

    for sym in kconf.unique_defined_syms:
        for node in sym.nodes:
            # Check dependencies
            if node.dep is not None and target_re.search(expr_to_str(node.dep)):
                collect_syms(node.dep)

            # A symbol whose select/imply is conditioned on the DT_HAS symbol is itself
            # an option worth enabling
            for attr in ["orig_selects", "orig_implies"]:
                for _, cond in getattr(node, attr, []) or []:
                    if cond is not None and target_re.search(expr_to_str(cond)):
                        deps.add(f"{prefix}{sym.name}")
                        collect_syms(cond)

    return deps


def handle_enabled_node(node: edtlib.Node) -> list[str]:
    """
    Handle diagnosis for an enabled DT node (linker error, one or more Kconfigs might be gating
    the device driver).
    """
    lines = [f"'{format_node(node)}' is enabled but no driver appears to be available for it.\n"]

    # A driver alone will not help if the node sits below a disabled parent
    ancestors = disabled_ancestor_lines(node)
    if ancestors:
        lines.extend(ancestors)
        lines.append("The device cannot be used until every node above it is enabled.\n")

    compats = list(getattr(node, "compats", []))
    kconf = setup_kconfig() if compats else None
    if not compats:
        lines.append("Could not determine compatible; check driver Kconfig manually.")
    elif not kconf:
        lines.append("ZEPHYR_BASE is not set; check driver Kconfig manually.")
    else:
        deps = set()
        for compat in compats:
            dt_has = f"DT_HAS_{edtlib.str_as_token(compat.upper())}_ENABLED"
            deps.update(find_kconfig_deps(kconf, dt_has))

        if deps:
            lines.append("Try enabling these Kconfig options:\n")
            lines.extend(f" - {dep}=y" for dep in sorted(deps))

    return lines


def handle_disabled_node(node: edtlib.Node) -> list[str]:
    """
    Handle diagnosis for a disabled DT node.
    """
    edt = node.edt
    lines = [f"'{format_node(node)}' is disabled in {status_location(node)}"]

    # Show dependency
    users = getattr(node, "required_by", [])
    if users:
        lines.append("The following nodes depend on it:")
        lines.extend(f" - {u.path}" for u in users)

    # Show chosen/alias references
    chosen_refs = [name for name, n in edt.chosen_nodes.items() if n is node]
    alias_refs = node.aliases

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

    ancestors = disabled_ancestor_lines(node)
    if ancestors:
        lines.append("")
        lines.extend(ancestors)

    lines.append("\nTry enabling the node by setting its 'status' property to 'okay'.")
    if ancestors:
        lines.append("Its disabled ancestors need to be enabled the same way.")

    return lines


def diagnose_node(node: edtlib.Node) -> list[str]:
    """
    Diagnose a node identifier that does resolve: whatever is missing is the device.
    """
    if node.status == "okay":
        return handle_enabled_node(node)
    return handle_disabled_node(node)


def handle_unknown_nodelabel(edt: edtlib.EDT, tail: str) -> list[str]:
    """
    Handle diagnosis for DT_NODELABEL() on a node label that does not exist.
    """
    label, matches = name_and_matches(tail, edt.label2node)

    return [
        f"No node label '{label}' exists in this build's devicetree.\n",
        *suggestion_lines(matches),
        "Node labels are the 'name:' part written in front of a node in a DTS file, e.g.",
        "'my_serial: uart@40002000'. In C they are lowercased, so DT_NODELABEL(my_serial).\n",
        "Node labels can be added to an existing node from a devicetree overlay:\n",
        "    my_serial: &uart0 {};\n",
        "See <build>/zephyr/zephyr.dts for the devicetree this build actually used.",
    ]


def handle_unknown_alias(edt: edtlib.EDT, tail: str) -> list[str]:
    """
    Handle diagnosis for DT_ALIAS() on an alias that is not defined.
    """
    aliases = all_aliases(edt)
    alias, matches = name_and_matches(tail, aliases)

    lines = [
        f"No alias '{alias}' is defined in this build's devicetree.\n",
        *suggestion_lines(matches, show_c_token=True),
    ]

    if not matches and aliases:
        lines.append("Aliases defined in this build:\n")
        lines.extend(f" - {name}{as_c_token(name)}" for name in aliases)
        lines.append("")

    lines.extend(
        [
            "Aliases live in the /aliases node. To add one, put this in a devicetree overlay:\n",
            "    / {",
            "            aliases {",
            f"                    {alias.replace('_', '-')} = &<node label>;",
            "            };",
            "    };\n",
            "In C, aliases are lowercased and '-' becomes '_', so an alias written 'my-dev' in",
            "DTS is DT_ALIAS(my_dev).",
        ]
    )

    return lines


def handle_unknown_chosen(edt: edtlib.EDT, tail: str) -> list[str]:
    """
    Handle diagnosis for DT_CHOSEN() on a /chosen entry that is not set.
    """
    chosen = sorted(edt.chosen_nodes)
    name, matches = name_and_matches(tail, chosen)

    lines = [
        f"No /chosen entry matching '{name}' is set in this build's devicetree.\n",
        *suggestion_lines(matches, show_c_token=True),
    ]

    if not matches and chosen:
        lines.append("/chosen entries set in this build:\n")
        lines.extend(f" - {entry}{as_c_token(entry)}" for entry in chosen)
        lines.append("")

    lines.extend(
        [
            "To set one, put this in a devicetree overlay:\n",
            "    / {",
            "            chosen {",
            f"                    {name.replace('_', ',', 1)} = &<node label>;",
            "            };",
            "    };\n",
            "In C, /chosen names are lowercased and ',' and '-' become '_', so an entry written",
            "'zephyr,console' in DTS is DT_CHOSEN(zephyr_console).",
        ]
    )

    return lines


def handle_unknown_path(edt: edtlib.EDT, macro: str) -> list[str]:
    """
    Handle diagnosis for a node path identifier with no node behind it.
    """
    path_id = macro.split("_P_")[0]
    paths = {f"DT_{gen_defines.node_z_path_id(node)}": node.path for node in edt.nodes}
    matches = difflib.get_close_matches(path_id, list(paths), n=3, cutoff=0.6)

    lines = [f"'{path_id}' does not name any node in this build's devicetree.\n"]

    if matches:
        lines.append("Did you mean one of these nodes?\n")
        lines.extend(f" - {paths[m]}   ({m})" for m in matches)
        lines.append("")

    lines.extend(
        [
            "DT_PATH() takes each path component lowercased and with '-', '@', ',', '.' and '+'",
            "replaced by '_', so /soc/i2c@12340000 is DT_PATH(soc, i2c_12340000).\n",
            "See <build>/zephyr/zephyr.dts for the devicetree this build actually used.",
        ]
    )

    return lines


def handle_unknown_instance(edt: edtlib.EDT, tail: str) -> list[str]:
    """
    Handle diagnosis for DT_INST()/DT_DRV_INST() on an instance that does not exist.
    """
    m = re.match(r"(\d+)_(.+)", tail)
    if not m:
        return []

    instance_no, compat_tail = int(m.group(1)), m.group(2)
    compat, matches = name_and_matches(compat_tail, edt.compat2nodes)

    # An index past the end of a compatible that does exist is a different problem from a
    # compatible nothing in the devicetree declares.
    known = next((c for c in edt.compat2nodes if gen_defines.str2ident(c) == compat), None)
    if not known:
        return [
            f"No node with compatible '{compat}' exists in this build's devicetree.\n",
            *suggestion_lines(matches, show_c_token=True),
            "DT_INST() and DT_DRV_INST() only see compatibles that at least one node declares.",
            "DT_DRV_COMPAT must be the compatible lowercased with '-' and ',' replaced by '_':\n",
            "    #define DT_DRV_COMPAT vnd_foo_device   /* for compatible \"vnd,foo-device\" */",
        ]

    nodes = edt.compat2nodes[known]
    # Both are defaultdicts, and a compatible with nothing enabled has no compat2okay key
    okay = edt.compat2okay.get(known, [])

    lines = [
        f"'{known}' has {len(nodes)} instance(s) in this build's devicetree,\n"
        f"so instance {instance_no} does not exist.\n",
        "Instances of this compatible, in DT_INST() index order:\n",
    ]
    lines.extend(
        f" - DT_INST({i}, ...)   {node.path}   status = \"{node.status}\""
        for i, node in enumerate(nodes)
    )
    lines.append("")

    if not okay:
        lines.append(
            "None of them is enabled. Most drivers only instantiate nodes with\n"
            "'status = \"okay\"', so enabling one in a devicetree overlay is usually\n"
            "what is needed here.\n"
        )

    lines.append(
        "Note that DT_INST() indexes list enabled nodes first, so enabling or disabling a\n"
        "node renumbers them. Iterate with DT_INST_FOREACH_STATUS_OKAY() rather than\n"
        "hardcoding an index."
    )

    return lines


def handle_unresolved_node_id(edt: edtlib.EDT, macro: str) -> list[str]:
    """
    Handle diagnosis for a node identifier that names no node at all.
    """
    handlers = [
        ("DT_N_NODELABEL_", handle_unknown_nodelabel),
        ("DT_N_ALIAS_", handle_unknown_alias),
        ("DT_N_INST_", handle_unknown_instance),
        ("DT_CHOSEN_", handle_unknown_chosen),
    ]
    for prefix, handler in handlers:
        if macro.startswith(prefix):
            return handler(edt, macro[len(prefix) :])

    if macro.startswith("DT_N_S_"):
        return handle_unknown_path(edt, macro)

    return []


def cell_lines(entry) -> list[str]:
    """
    List the cells a specifier entry defines, with the values it gives them.
    """
    cells = getattr(entry, "data", None) or {}
    width = max((len(cell) for cell in cells), default=0)
    return [f" - {cell.ljust(width)}   (currently {value})" for cell, value in cells.items()]


def entry_lines(entries: list) -> list[str]:
    """
    List the entries of a specifier, saying whatever identifies each one.
    """
    lines = []
    for i, entry in enumerate(entries):
        controller = getattr(entry, "controller", None)
        name = getattr(entry, "name", None)
        described = [format_node(controller) if controller else ""]
        # Registers have no controller, so their address is what tells them apart
        if getattr(entry, "addr", None) is not None:
            described.append(f"at {hex(entry.addr)}")
        if name:
            described.append(f"named '{name}'")
        lines.append("   ".join([f" - index {i}", *filter(None, described)]))

    return lines


def handle_bad_cell(subject: str, space: str, entry, index: int, cell: str) -> list[str]:
    """
    Handle diagnosis for a cell the specifier entry's controller does not define.
    """
    controller = getattr(entry, "controller", None)
    controlled_by = (
        f"Entry {index} is controlled by '{format_node(controller)}', which defines"
        if controller
        else f"Entry {index} defines"
    )
    lines = [
        f"'{subject}' has no '{cell}' cell in entry {index} of {space}.\n",
        f"{controlled_by} these cells:\n",
        *cell_lines(entry),
        "",
        *suggestion_lines(close_matches(cell, getattr(entry, "data", None) or {})),
    ]

    if controller and controller.binding_path:
        lines.append(
            "Cell names come from the controller's binding, not this node's. See its\n"
            "'*-cells:' list in\n"
            f"{controller.binding_path}"
        )

    return lines


def handle_bad_entry_index(subject: str, space: str, entries: list, index: int) -> list[str]:
    """
    Handle diagnosis for a specifier index past the end of the property.
    """
    count = len(entries)
    return [
        f"'{subject}' has no entry {index} in {space}: "
        + (f"there is only {count}.\n" if count == 1 else f"there are only {count}.\n"),
        *entry_lines(entries),
    ]


def handle_bad_entry_name(
    subject: str, space: str, entries: list, name: str, names_prop: str
) -> list[str]:
    """
    Handle diagnosis for a specifier name the property does not have.
    """
    names = [entry.name for entry in entries if getattr(entry, "name", None)]

    if not names:
        return [
            f"'{subject}' does not name the entries of {space}, so '{name}' cannot be\n"
            "looked up.\n",
            f"Entry names come from {'an' if names_prop[0] in 'aeiou' else 'a'} "
            f"'{names_prop}' property on this node. Add one, or\n"
            "select the entry by index instead.\n",
            *entry_lines(entries),
        ]

    return [
        f"'{subject}' has no entry named '{name}' in {space}.\n",
        *suggestion_lines(close_matches(name, names)),
        f"Entry names come from its '{names_prop}' property:\n",
        *(f" - {n}" for n in names),
    ]


def handle_specifier(
    subject: str, space: str, entries: list, suffix: str, names_prop: str
) -> list[str] | None:
    """
    Handle diagnosis for a specifier accessor: DT_PHA_BY_IDX(), DT_IRQ_BY_NAME(),
    DT_REG_ADDR_BY_IDX() and everything built on them.

    All three specifier spaces reduce to a list of entries selected by index or by name,
    each defining a set of cells, so one handler explains all of them. Returns None when
    the suffix is not a specifier accessor, leaving the caller's own diagnosis in place.
    """
    m = SPECIFIER_RE.match(suffix)
    if not m or not entries:
        return None

    if m["idx"] is not None:
        index = int(m["idx"])
        if index >= len(entries):
            return handle_bad_entry_index(subject, space, entries, index)
    else:
        index = next(
            (i for i, e in enumerate(entries) if getattr(e, "name", None) == m["name"]), None
        )
        if index is None:
            return handle_bad_entry_name(subject, space, entries, m["name"], names_prop)

    entry = entries[index]

    # A phandle-array can hold null specifiers, written '<0>', which have no controller
    if entry is None:
        return [
            f"Entry {index} of {space} on '{subject}' is a null specifier, written\n"
            "'<0>' in the devicetree, so it has no controller and no cells.\n",
            "Point the entry at a real controller, or guard the access with\n"
            "DT_PHA_HAS_CELL_AT_IDX().",
        ]

    if m["cell"]:
        return handle_bad_cell(subject, space, entry, index, m["cell"])

    # The entry is there and no cell was asked for, so nothing here explains the failure
    return None


def handle_property_macro(node: edtlib.Node, prop_name: str, suffix: str) -> list[str]:
    """
    Handle diagnosis for a property that is there, but not in the shape the devicetree API
    asked for: an out-of-range index, a name the property does not have, or a type that
    has no plain value macro of its own.
    """
    prop = node.props[prop_name]
    described = f"'{prop_name}' is of type '{prop.spec.type}'"
    if isinstance(prop.val, list):
        described += f", with {len(prop.val)} element(s)"

    if not suffix:
        return [
            f"'{format_node(node)}' has a '{prop_name}' property, but no macro holding a\n"
            "plain value for it.\n",
            f"{described}, which DT_PROP() cannot return as a single value.\n",
            "Read it with the API meant for its type instead, e.g. DT_PROP_BY_IDX(),\n"
            "DT_PROP_LEN(), DT_PHANDLE_BY_IDX() or DT_PHA_BY_IDX().",
        ]

    # A phandle-array is a specifier space, so the suffix may be selecting an entry or a
    # cell within one rather than asking for something the property simply does not have
    if prop.spec.type == "phandle-array":
        # Names are keyed by the specifier space rather than by the property name, so
        # 'cs-gpios' entries are named through 'gpio-names', not 'cs-gpio-names'
        base = next((e.basename for e in prop.val if e is not None), prop_name.removesuffix("s"))
        lines = handle_specifier(
            format_node(node), f"the '{prop_name}' property", prop.val, suffix, f"{base}-names"
        )
        if lines:
            return lines

    prop_macro = f"DT_{gen_defines.node_z_path_id(node)}_P_{gen_defines.str2ident(prop_name)}"

    return [
        f"'{format_node(node)}' has a '{prop_name}' property, but '{suffix.lstrip('_')}' was\n"
        "not generated for it.\n",
        f"{described}.\n",
        "This is usually an index past the end of the property, or a name or cell it\n"
        "does not define.\n",
        f"Search for '{prop_macro}' in\n"
        "<build>/zephyr/include/generated/zephyr/devicetree_generated.h to see which\n"
        "macros do exist for this property.",
    ]


def handle_missing_property(node: edtlib.Node, prop_id: str) -> list[str]:
    """
    Handle diagnosis for a property macro the node does not have.
    """
    # The property may well be there, with only the part the API appended to it missing.
    props = {gen_defines.str2ident(name): name for name in node.props}
    found = max(
        (p for p in props if prop_id == p or prop_id.startswith(f"{p}_")), key=len, default=None
    )
    if found:
        return handle_property_macro(node, props[found], prop_id[len(found) :])

    # Nothing matched, so whatever the API appended is not part of a property name either
    prop_id, matches = name_and_matches(prop_id, node.props)
    lines = [
        f"'{format_node(node)}' has no '{prop_id}' property.\n",
        *suggestion_lines(matches, show_c_token=True),
    ]

    # A property the binding knows about but the node leaves unset is a different problem
    # from a property nothing has ever heard of.
    specs = node.binding.prop2specs if node.binding else {}
    declared = next((name for name in specs if gen_defines.str2ident(name) == prop_id), None)

    if declared:
        lines.append(
            f"The node's binding declares '{declared}', but the node does not set it and the\n"
            "binding gives it no default value. Set it in a devicetree overlay, read it with\n"
            "DT_PROP_OR(), or guard the access with DT_NODE_HAS_PROP()."
        )
    elif node.props and not matches:
        lines.append("Properties this node does have:\n")
        lines.extend(f" - {name}{as_c_token(name)}" for name in sorted(node.props))

    if node.binding_path:
        lines.append(f"\nBinding: {node.binding_path}")
    else:
        lines.append(
            "\nThis node has no matching binding, so hardly any macros are generated for it.\n"
            "Check its 'compatible' property, and see <build>/zephyr/zephyr.dts."
        )

    lines.append(
        "\nIn C, property names are lowercased and '-', ',', '.', '@', '/' and '+' become '_',\n"
        "so a 'clock-frequency' property is DT_PROP(node_id, clock_frequency)."
    )

    return lines


# The specifier spaces that hang off a node rather than off one of its properties
NODE_SPECIFIERS = {
    "_IRQ": ("interrupts", "its interrupts", "interrupts", "interrupt-names"),
    "_REG": ("regs", "its registers", "reg", "reg-names"),
}


def handle_unknown_node_macro(node: edtlib.Node, macro: str, suffix: str) -> list[str]:
    """
    Handle diagnosis for a node that exists but for which the requested macro was not
    generated, e.g. an out-of-range index into a property, register or interrupt.
    """
    for prefix, (attr, space, prop_name, names_prop) in NODE_SPECIFIERS.items():
        if not suffix.startswith(prefix):
            continue

        entries = getattr(node, attr)
        if not entries:
            return [
                f"'{format_node(node)}' has no '{prop_name}' property, so '{macro}' was\n"
                "not generated for it.\n",
                f"Give the node a '{prop_name}' property in a devicetree overlay, or guard\n"
                "the access with DT_NODE_HAS_PROP().",
            ]

        lines = handle_specifier(
            format_node(node), space, entries, suffix[len(prefix) :], names_prop
        )
        if lines:
            return lines

    return [
        f"'{format_node(node)}' exists, but '{macro}' was not generated for it.\n",
        f"The devicetree API asked for '{suffix.lstrip('_')}' on this node, and nothing in the",
        "devicetree provides it. This is usually an out-of-range index, or a register,",
        "interrupt or phandle cell the node does not define.\n",
        f"Search for 'DT_{gen_defines.node_z_path_id(node)}_' in",
        "<build>/zephyr/include/generated/zephyr/devicetree_generated.h to see which macros do",
        "exist for this node.",
    ]


def dt_macro(symbol: str) -> str | None:
    """
    Extract the unexpanded devicetree macro from a build error symbol, if there is one.

    DEVICE_DT_GET() pastes the node identifier onto '__device_dts_ord_', so when the node
    identifier never expanded the macro is the tail of the symbol rather than all of it.
    """
    m = DT_MACRO_RE.search(symbol)
    return m.group(0) if m else None


def diagnose_macro(edt: edtlib.EDT, macro: str) -> list[str]:
    """
    Diagnose an unexpanded devicetree macro by finding the longest part of it that does
    name a node: whatever is left over is what the devicetree does not provide.
    """
    node_ids = build_node_id_map(edt)
    node_id, suffix = split_node_id(macro, node_ids)

    # '_S_' only ever introduces a child path component, so a leftover one means the node
    # the full path names does not exist, rather than the ancestor that matched being at
    # fault.
    if node_id is None or suffix.startswith("_S_"):
        return handle_unresolved_node_id(edt, macro)

    node = node_ids[node_id]

    # DEVICE_DT_GET() on a node identifier that does resolve: same story as an ordinal
    if suffix in ("", "_ORD"):
        return diagnose_node(node)

    if suffix.startswith("_P_"):
        return handle_missing_property(node, suffix[len("_P_") :])

    return handle_unknown_node_macro(node, macro, suffix)


def report(lines: list[str]) -> int:
    # Each section of a diagnosis brings its own blank line separator, so collapse the
    # runs that show up where two of them meet.
    text = re.sub(r"\n{3,}", "\n\n", "\n".join(lines))
    print(tabulate([[text]], headers=["DT Doctor"], tablefmt="grid"))
    return 0


def main() -> int:
    args = parse_args()

    # A device symbol whose node identifier expanded: the ordinal identifies the node.
    m = DEVICE_ORDINAL_RE.search(args.symbol)
    if m:
        edt = load_edt(args.edt_pickle)
        node = edt.dep_ord2node.get(int(m.group(1)))
        if not node:
            print(f"Ordinal {m.group(1)} not found in edt.pickle", file=sys.stderr)
            return 1
        return report(diagnose_node(node))

    # Otherwise, an unexpanded devicetree macro leaked into the error message, which means
    # some part of it does not exist. Reverse-engineer which part.
    macro = dt_macro(args.symbol)
    if not macro:
        return 1

    lines = diagnose_macro(load_edt(args.edt_pickle), macro)
    if not lines:
        return 1

    return report(lines)


if __name__ == "__main__":
    sys.exit(main())
