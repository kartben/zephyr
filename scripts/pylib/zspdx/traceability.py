# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Load the documentation traceability graph for the SBOM.

The Zephyr documentation build resolves every ``@satisfies`` / ``@verifies``
Doxygen command and every StrictDoc requirement relation into a single
``traceability.json`` file (published under ``doc/_build/html``). Each entry is a
node with an ``id`` and a ``targets`` map of typed, directed edges. This module
turns that flat list into a small typed graph the SPDX 3.1 FunctionalSafety
serializer can walk:

* :class:`Requirement`  -- a StrictDoc requirement (``ZEP-SRS-*`` software or
  ``ZEP-SYRS-*`` system level), with the design/implementation/test artifacts it
  is linked to and the higher-level requirements it refines.
* :class:`Design`       -- a design description (``DESIGN-*``) and the
  requirements it fulfills.
* :class:`Test`         -- a ``<suite>__<function>`` ztest case and the
  requirements it validates.

Only the node kinds and edges the serializer consumes are modelled; unrelated
nodes (raw C symbols referenced only as implementation targets, Kconfig symbols,
...) are ignored as standalone nodes but are still reachable through the
``implemented_by`` edge on a requirement.
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass, field

_logger = logging.getLogger(__name__)


@dataclass
class Requirement:
    """A StrictDoc requirement and everything the graph links it to."""

    uid: str
    title: str = ""
    component: str = ""
    rtype: str = ""
    status: str = ""
    # Higher-level requirements this one refines (``trace`` edge, e.g. an
    # SRS pointing at the SYRS it derives from).
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
    suite: str
    function: str
    title: str = ""
    validates: list[str] = field(default_factory=list)


@dataclass
class TraceabilityGraph:
    """Parsed view of ``traceability.json``."""

    requirements: dict[str, Requirement] = field(default_factory=dict)
    designs: dict[str, Design] = field(default_factory=dict)
    tests: dict[str, Test] = field(default_factory=dict)

    def implementation_symbols(self) -> set[str]:
        """All distinct implementation symbol names referenced by requirements."""
        symbols: set[str] = set()
        for req in self.requirements.values():
            symbols.update(req.implemented_by)
        return symbols

    def test_functions(self) -> set[str]:
        """All distinct test function names referenced by requirements."""
        return {test.function for test in self.tests.values()}


def _targets(node: dict, name: str) -> list[str]:
    """Return the ``targets[name]`` edge list of a node, or ``[]``."""
    value = node.get("targets", {}).get(name)
    return list(value) if isinstance(value, list) else []


def _classify(node_id: str) -> str:
    """Classify a node id as ``requirement`` / ``design`` / ``test`` / ``other``."""
    if node_id.startswith(("ZEP-SRS", "ZEP-SYRS")):
        return "requirement"
    if node_id.startswith("DESIGN-"):
        return "design"
    # ztest ids are "<suite>__<function>"; a lone "__" is the ztest separator.
    if "__" in node_id:
        return "test"
    return "other"


def parse_traceability(data: list[dict]) -> TraceabilityGraph:
    """Build a :class:`TraceabilityGraph` from parsed ``traceability.json`` data."""
    graph = TraceabilityGraph()
    for node in data:
        node_id = node.get("id")
        if not node_id:
            continue
        kind = _classify(node_id)
        if kind == "requirement":
            attrs = node.get("attributes", {})
            graph.requirements[node_id] = Requirement(
                uid=node_id,
                title=node.get("caption", ""),
                component=attrs.get("component", ""),
                rtype=attrs.get("rtype", ""),
                status=attrs.get("status", ""),
                traces_to=_targets(node, "trace"),
                fulfilled_by=_targets(node, "fulfilled_by"),
                implemented_by=_targets(node, "implemented_by"),
                validated_by=_targets(node, "validated_by"),
            )
        elif kind == "design":
            graph.designs[node_id] = Design(
                uid=node_id,
                title=node.get("caption", ""),
                document=node.get("document", ""),
                fulfills=_targets(node, "fulfills"),
            )
        elif kind == "test":
            suite, _, function = node_id.partition("__")
            graph.tests[node_id] = Test(
                node_id=node_id,
                suite=suite,
                function=function,
                title=node.get("caption", ""),
                validates=_targets(node, "validates"),
            )

    _logger.info(
        "traceability: %d requirement(s), %d design(s), %d test(s)",
        len(graph.requirements),
        len(graph.designs),
        len(graph.tests),
    )
    return graph


def load_traceability(path: str) -> TraceabilityGraph | None:
    """Load and parse a ``traceability.json`` file.

    Returns ``None`` (with a warning) if the file is missing or unreadable, so
    the caller can degrade gracefully to a plain SBOM.
    """
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        _logger.warning("traceability: could not read %s: %s", path, e)
        return None
    if not isinstance(data, list):
        _logger.warning("traceability: %s is not a JSON list; ignoring", path)
        return None
    return parse_traceability(data)
