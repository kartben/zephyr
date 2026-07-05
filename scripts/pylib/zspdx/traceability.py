# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Load the documentation traceability graph for the SBOM.

The Zephyr documentation build resolves every ``@satisfies`` / ``@verifies``
Doxygen command and every StrictDoc requirement into a sphinx-needs graph and
exports it as ``needs.json`` (published under ``doc/_build/html``). This module
turns that export into the small typed graph the SPDX 3.1 FunctionalSafety
serializer walks:

* :class:`Requirement`  -- a StrictDoc requirement (``ZEP-SRS-*`` software or
  ``ZEP-SYRS-*`` system level), with the design/implementation/test artifacts it
  is linked to and the higher-level requirements it refines.
* :class:`Design`       -- a design description (``DESIGN-*``) and the
  requirements it fulfills.
* :class:`Test`         -- a ``<suite>__<function>`` ztest case and the
  requirements it validates.

Needs carry their links in the *forward* direction: a test declares
``validates``, an implementation symbol declares ``implements`` and a design
declares ``fulfills``, all pointing at requirements. sphinx-needs adds the
matching ``*_back`` lists on the requirement. The classification and the
test/implementation split follow ``doc/_scripts/gen_traceability_report.py`` and
``doc/_scripts/traceability_app.py`` so the SBOM adjudicates exactly the graph
the documentation's traceability views show.
"""

from __future__ import annotations

import json
import logging
import re
from dataclasses import dataclass, field

_logger = logging.getLogger(__name__)

_SRS_RE = re.compile(r"^ZEP-SRS-\d+-\d+$")
_SYRS_RE = re.compile(r"^ZEP-SYRS-\d+$")

# Leading line the requirement_traceability extension puts on every symbol need
# to link back to the Doxygen page; not part of the documented behaviour.
_DOXYBRIDGE_RE = re.compile(r"^(Documented at |Internal symbol, defined in ).*$")


@dataclass
class Requirement:
    """A StrictDoc requirement and everything the graph links it to."""

    uid: str
    title: str = ""
    component: str = ""
    rtype: str = ""
    status: str = ""
    # Higher-level requirements this one refines (``trace`` link, e.g. an SRS
    # pointing at the SYRS it derives from).
    traces_to: list[str] = field(default_factory=list)
    # DESIGN node ids that fulfill this requirement.
    fulfilled_by: list[str] = field(default_factory=list)
    # Implementation symbol names (C functions/macros) that implement it.
    implemented_by: list[str] = field(default_factory=list)
    # Test ids (``<suite>__<function>``) that validate it.
    validated_by: list[str] = field(default_factory=list)

    @property
    def is_system(self) -> bool:
        """Whether this is a system-level requirement (``ZEP-SYRS-*``)."""
        return self.uid.startswith("ZEP-SYRS")


@dataclass
class Design:
    """A design description node (``DESIGN-*``)."""

    uid: str
    title: str = ""
    document: str = ""
    fulfills: list[str] = field(default_factory=list)


@dataclass
class Test:
    """A ztest case node, id ``<suite>__<function>``."""

    node_id: str
    title: str = ""
    details: str = ""
    validates: list[str] = field(default_factory=list)


@dataclass
class TraceabilityGraph:
    """Parsed view of the documentation build's ``needs.json`` export."""

    requirements: dict[str, Requirement] = field(default_factory=dict)
    designs: dict[str, Design] = field(default_factory=dict)
    tests: dict[str, Test] = field(default_factory=dict)

    def implementation_symbols(self) -> set[str]:
        """All distinct implementation symbol names referenced by requirements."""
        symbols: set[str] = set()
        for req in self.requirements.values():
            symbols.update(req.implemented_by)
        return symbols


def _links(need: dict, name: str) -> list[str]:
    """Return the ``name`` link list of a need, or ``[]``."""
    value = need.get(name)
    return list(value) if isinstance(value, list) else []


def _classify(need: dict) -> str:
    """Kind of a need -- ``requirement``, ``design`` or ``symbol``.

    The sphinx-needs type decides; a need carrying none falls back to its id
    shape. A ``req`` need whose id is neither an SRS nor a SYRS is not a
    requirement of the catalog and is left to the symbol bucket.
    """
    node_id = need.get("id", "")
    is_req = bool(_SRS_RE.match(node_id) or _SYRS_RE.match(node_id))
    match need.get("type") or "":
        case "req":
            return "requirement" if is_req else "symbol"
        case "design_need":
            return "design"
        case "test" | "impl":
            return "symbol"
    if is_req:
        return "requirement"
    return "design" if node_id.startswith("DESIGN-") else "symbol"


def _details(need: dict) -> str:
    """The need's prose body, without the extension's Doxygen back-reference."""
    lines = (need.get("content") or "").splitlines()
    while lines and (not lines[0].strip() or _DOXYBRIDGE_RE.match(lines[0].strip())):
        lines.pop(0)
    return "\n".join(lines).strip()


def parse_traceability(needs: list[dict]) -> TraceabilityGraph:
    """Build a :class:`TraceabilityGraph` from a list of sphinx-needs needs."""
    graph = TraceabilityGraph()
    # Symbol needs, split into implementations and tests the way the
    # documentation's traceability app does: a symbol that only implements is an
    # implementation, anything that validates is a verifier.
    implements: dict[str, list[str]] = {}
    for need in needs:
        node_id = need.get("id")
        if not node_id:
            continue
        kind = _classify(need)
        if kind == "requirement":
            graph.requirements[node_id] = Requirement(
                uid=node_id,
                title=need.get("title") or "",
                component=need.get("component") or "",
                rtype=need.get("rtype") or "",
                status=need.get("status") or "",
                traces_to=_links(need, "trace"),
                fulfilled_by=_links(need, "fulfills_back"),
                validated_by=_links(need, "validates_back"),
            )
        elif kind == "design":
            graph.designs[node_id] = Design(
                uid=node_id,
                title=need.get("title") or "",
                document=need.get("docname") or "",
                fulfills=_links(need, "fulfills"),
            )
        elif (validates := _links(need, "validates")) or not _links(need, "implements"):
            graph.tests[node_id] = Test(
                node_id=node_id,
                title=need.get("title") or "",
                details=_details(need),
                validates=validates,
            )
        else:
            implements[node_id] = _links(need, "implements")

    for symbol, uids in implements.items():
        for uid in uids:
            if req := graph.requirements.get(uid):
                req.implemented_by.append(symbol)
    for req in graph.requirements.values():
        req.implemented_by.sort()

    _logger.info(
        "traceability: %d requirement(s), %d design(s), %d test(s), %d implementation symbol(s)",
        len(graph.requirements),
        len(graph.designs),
        len(graph.tests),
        len(implements),
    )
    return graph


def load_needs(data: dict | list) -> list[dict]:
    """Return the needs of a sphinx-needs export, tolerating the version envelope."""
    if isinstance(data, list):
        return data
    versions = data.get("versions")
    if isinstance(versions, dict) and versions:
        current = versions.get(data.get("current_version")) or next(iter(versions.values()))
        return list(current.get("needs", {}).values())
    return list(data.get("needs", {}).values())


def load_traceability(path: str) -> TraceabilityGraph | None:
    """Load and parse the documentation build's ``needs.json`` export.

    Returns ``None`` (with a warning) if the file is missing or unreadable, so
    the caller can degrade gracefully to a plain SBOM.
    """
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        _logger.warning("traceability: could not read %s: %s", path, e)
        return None
    if not isinstance(data, (dict, list)):
        _logger.warning("traceability: %s is not a sphinx-needs export; ignoring", path)
        return None
    return parse_traceability(load_needs(data))
