# Copyright (c) 2025 The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for the SPDX 3.1 FunctionalSafety generation in the zspdx package.

These cover the standalone input loaders (traceability graph, twister results,
per-test coverage matrix, StrictDoc catalog and implementation-body resolution)
and the end-to-end serializer pass that turns them into a FunctionalSafety SPDX
document, including the coverage-backed evidence linking a verifying test to the
implementation snippets it actually exercises, and the ExternalMap referencing of
source files defined in a sibling document.
"""

import json
import os
import sys
from pathlib import Path

import pytest

ZEPHYR_BASE = os.environ.get("ZEPHYR_BASE", str(Path(__file__).parents[3]))
sys.path.insert(0, os.path.join(ZEPHYR_BASE, "scripts", "pylib"))

from zspdx.adequacy import requirement_adequacy  # noqa: E402
from zspdx.coverage import load_coverage  # noqa: E402
from zspdx.requirements import _parse_sdoc  # noqa: E402
from zspdx.sources import Source, content_byte_range, resolve_impl_symbols  # noqa: E402
from zspdx.traceability import parse_traceability  # noqa: E402
from zspdx.twister import load_results, verdict_from_rollup  # noqa: E402

pytest.importorskip("spdx_python_model")
try:
    # v3_1 is exposed as a lazy attribute of the package, not a real submodule,
    # so it cannot be imported via a dotted importorskip.
    from spdx_python_model import v3_1  # noqa: F401
except ImportError:
    pytest.skip("spdx_python_model v3_1 bindings unavailable", allow_module_level=True)


# ---- traceability ----------------------------------------------------------


def _sample_traceability():
    return [
        {"id": "ZEP-SYRS-1", "caption": "Threading", "targets": {}},
        {
            "id": "ZEP-SRS-1-1",
            "caption": "Creating threads",
            "attributes": {"component": "Threads", "rtype": "Functional"},
            "targets": {
                "trace": ["ZEP-SYRS-1"],
                "fulfilled_by": ["DESIGN-THREADS"],
                "implemented_by": ["k_thread_create"],
                "validated_by": ["threads__test_thread_start"],
            },
        },
        {"id": "DESIGN-THREADS", "caption": "Thread design",
         "targets": {"fulfills": ["ZEP-SRS-1-1"]}},
        {"id": "threads__test_thread_start", "caption": "start",
         "attributes": {"details": "Spawns a thread and checks it runs."},
         "targets": {"validates": ["ZEP-SRS-1-1"]}},
    ]


def test_traceability_parse_classifies_nodes():
    graph = parse_traceability(_sample_traceability())
    assert set(graph.requirements) == {"ZEP-SYRS-1", "ZEP-SRS-1-1"}
    assert set(graph.designs) == {"DESIGN-THREADS"}
    assert set(graph.tests) == {"threads__test_thread_start"}
    srs = graph.requirements["ZEP-SRS-1-1"]
    assert srs.traces_to == ["ZEP-SYRS-1"] and srs.implemented_by == ["k_thread_create"]
    assert not srs.is_system and graph.requirements["ZEP-SYRS-1"].is_system
    assert graph.implementation_symbols() == {"k_thread_create"}
    # the @details of the test (a traceability attribute) is captured
    assert graph.tests["threads__test_thread_start"].details == (
        "Spawns a thread and checks it runs."
    )


# ---- twister results -------------------------------------------------------


def test_verdict_from_rollup():
    assert verdict_from_rollup("passing") == "pass"
    assert verdict_from_rollup("failing") == "fail"
    assert verdict_from_rollup("skipped") == "inconclusive"
    assert verdict_from_rollup("no-run") == "inconclusive"


def _write_twister(path, testcases, platform="native_sim"):
    path.write_text(json.dumps({
        "environment": {"zephyr_version": "v4.4.0-1-gdeadbeef"},
        "testsuites": [{
            "name": "kernel.threads", "platform": platform, "path": "tests/kernel/threads",
            "testcases": testcases,
        }],
    }))


def test_load_results_qualifies_and_rolls_up(tmp_path):
    report = tmp_path / "twister.json"
    # twister reports the ztest name without the "test_" prefix; the qualified id
    # rebuilds it and keeps the ztest suite (last two identifier segments).
    _write_twister(
        report, [{"identifier": "kernel.threads.threads.thread_start", "status": "passed"}]
    )
    env, results, qual_map = load_results(str(report))
    assert env["zephyr_version"] == "v4.4.0-1-gdeadbeef"
    assert "threads__test_thread_start" in results
    assert results["threads__test_thread_start"]["rollup"] == "passing"
    # qual_map lets the coverage matrix (scenario-slug keyed) recover the id.
    assert qual_map["kernel.threads"]["test_thread_start"] == "threads__test_thread_start"


def test_load_results_rollup_precedence(tmp_path):
    report = tmp_path / "twister.json"
    report.write_text(json.dumps({"environment": {}, "testsuites": [
        {"name": "s", "platform": "p1",
         "testcases": [{"identifier": "s.suite.f", "status": "passed"}]},
        {"name": "s", "platform": "p2",
         "testcases": [{"identifier": "s.suite.f", "status": "failed"}]},
    ]}))
    _env, results, _q = load_results(str(report))
    # any failure dominates the rollup
    assert results["suite__test_f"]["rollup"] == "failing"


# ---- coverage matrix -------------------------------------------------------


def test_load_coverage_joins_and_unions(tmp_path):
    matrix = tmp_path / "test_matrix.json"
    # matrix keys are <scenario slug>_<test fn>, no ztest suite name
    matrix.write_text(json.dumps({
        "by_test": {
            "kernel_threads_test_thread_start":
                {"kernel/thread.c": [800, 805], "../modules/x.c": [1]},
        },
        "by_line": {"kernel/thread.c": {"800": ["a"], "805": ["a"], "812": ["b"]},
                    "../modules/x.c": {"1": ["a"]}},
    }))
    qual_map = {"kernel.threads": {"test_thread_start": "threads__test_thread_start"}}
    cov, all_covered = load_coverage(
        str(matrix), qual_map, wanted_tests={"threads__test_thread_start"}
    )
    assert cov["threads__test_thread_start"]["kernel/thread.c"] == {800, 805}
    # out-of-tree files are dropped from both indexes
    assert "../modules/x.c" not in cov["threads__test_thread_start"]
    assert "../modules/x.c" not in all_covered
    # all_covered is the whole-run union from by_line (includes other tests' 812)
    assert all_covered["kernel/thread.c"] == {800, 805, 812}


# ---- adequacy --------------------------------------------------------------


def test_requirement_adequacy_verdicts():
    from zspdx.sources import ImplBody
    bodies = {"k_foo": [ImplBody("kernel/foo.c", 10, 20, "impl")]}
    body_lines = {"kernel/foo.c": {12, 15}}

    def adq(symbols, tests, cov_by_test, all_covered):
        return requirement_adequacy(symbols, tests, bodies, cov_by_test, all_covered)

    assert adq([], ["t"], {}, {}) == "no-impl"
    # impl symbol is a macro with no resolvable body
    assert adq(["k_macro"], ["t"], {"t": {}}, body_lines) == "unresolved"
    # verifying test has no coverage data at all
    assert adq(["k_foo"], ["t"], {}, body_lines) == "no-cov"
    # nobody covers the body
    assert adq(["k_foo"], ["t"], {"t": {"kernel/foo.c": {99}}}, {}) == "unattributed"
    # the verifying test exercises the body
    assert adq(["k_foo"], ["t"], {"t": {"kernel/foo.c": {12}}}, body_lines) == "true"
    # only OTHER tests reach the body -> looks verified, isn't
    assert adq(["k_foo"], ["t"], {"t": {"kernel/foo.c": {99}}}, body_lines) == "broken"


# ---- requirements catalog --------------------------------------------------


def test_parse_sdoc_multiline_statement(tmp_path):
    sdoc = tmp_path / "req.sdoc"
    sdoc.write_text(
        "[REQUIREMENT]\nUID: ZEP-SRS-1-1\nTITLE: Creating threads\n"
        "STATEMENT: >>>\nThe RTOS shall create threads.\n<<<\n"
    )
    catalog = _parse_sdoc(str(sdoc))
    assert catalog["ZEP-SRS-1-1"].statement == "The RTOS shall create threads."


# ---- implementation-body resolution ----------------------------------------

_FOO_C = (
    "#include <x.h>\n"
    "int z_impl_k_foo(int a)\n"
    "{\n"
    "\treturn a + 1;\n"
    "}\n"
    "int z_vrfy_k_foo(int a)\n"
    "{\n"
    "\treturn z_impl_k_foo(a);\n"
    "}\n"
)

# A multi-line @details, to check the structure survives (is not flattened).
_FOO_DETAILS = "Calls k_foo.\nTest steps:\n- Call k_foo(1)\n- Check it returns 2"


def _make_tree(tmp_path):
    (tmp_path / "kernel").mkdir(exist_ok=True)
    (tmp_path / "kernel" / "foo.c").write_text(_FOO_C)


def test_resolve_impl_symbols_finds_impl_and_vrfy(tmp_path):
    _make_tree(tmp_path)
    bodies = resolve_impl_symbols(Source(str(tmp_path)), {"k_foo"})
    got = {(b.variant, b.file, b.start, b.end) for b in bodies["k_foo"]}
    assert ("impl", "kernel/foo.c", 2, 5) in got
    assert ("vrfy", "kernel/foo.c", 6, 9) in got


def test_content_byte_range():
    text = "line1\nline2\nline3\n"  # 6 bytes per line
    assert content_byte_range(text, 2, 3) == (7, 18)
    assert content_byte_range(text, 9, 9) is None


# ---- end-to-end serializer -------------------------------------------------


def _fs_metadata(tmp_path, *, covered=True):
    """Build sbom_graph.metadata for the FunctionalSafety pass."""
    from zspdx.requirements import RequirementInfo
    from zspdx.sources import ImplBody

    _make_tree(tmp_path)
    traceability = parse_traceability([
        {"id": "ZEP-SYRS-1", "caption": "Sys", "targets": {}},
        {
            "id": "ZEP-SRS-1-1", "caption": "Foo",
            "attributes": {"status": "Draft", "rtype": "Functional"},
            "targets": {"trace": ["ZEP-SYRS-1"], "implemented_by": ["k_foo"],
                        "validated_by": ["suite__test_foo"]},
        },
        {"id": "DESIGN-X", "caption": "Design", "document": "kernel/foo",
         "targets": {"fulfills": ["ZEP-SRS-1-1"]}},
        {"id": "suite__test_foo", "caption": "exercises foo",
         "attributes": {"details": _FOO_DETAILS},
         "targets": {"validates": ["ZEP-SRS-1-1"]}},
    ])
    return {
        "traceability": traceability,
        "requirements_catalog": {"ZEP-SRS-1-1": RequirementInfo(uid="ZEP-SRS-1-1",
                                                                statement="The system shall foo.")},
        "impl_bodies": {"k_foo": [ImplBody("kernel/foo.c", 2, 5, "impl")]},
        "test_results": {"suite__test_foo": {
            "rollup": "passing",
            "instances": [{"platform": "native_sim", "status": "passed"}],
        }},
        "test_environment": {"zephyr_version": "v4.4.0-1-gdeadbeef",
                             "options": {"platform": ["native_sim"], "coverage_tool": "lcov"}},
        # covered: the verifying test executes lines 2 and 4 of the body (2..5) —
        # two non-contiguous paths. Some test always reaches it (all_covered), so
        # uncovered -> broken, not unattributed.
        "coverage": {"suite__test_foo": {"kernel/foo.c": {2, 4} if covered else {99}}},
        "all_covered": {"kernel/foo.c": {2, 4}},
        "source": None,
        "zephyr_base": str(tmp_path),
    }


def _safety_graph(tmp_path, *, covered=True, host_in_sbom=False):
    from zspdx.model import SBOMComponent, SBOMDocument, SBOMFile, SBOMGraph
    from zspdx.serializers.spdx3 import SPDX3Serializer
    from zspdx.version import SPDX_VERSION_3_1

    graph = SBOMGraph()
    graph.namespace_prefix = "http://spdx.org/spdxdocs/zephyr-ut"
    graph.metadata.update(_fs_metadata(tmp_path, covered=covered))
    if host_in_sbom:
        abs_path = os.path.normpath(str(tmp_path / "kernel" / "foo.c"))
        sbom_file = SBOMFile(path=abs_path, relative_path="kernel/foo.c", hashes={"SHA1": "abc"})
        component = SBOMComponent(name="zephyr")
        component.files[abs_path] = sbom_file
        document = SBOMDocument(name="zephyr")
        document.components["zephyr"] = component
        graph.files[abs_path] = sbom_file
        graph.components["zephyr"] = component
        graph.documents["zephyr"] = document

    out = tmp_path / "out"
    out.mkdir(exist_ok=True)
    assert SPDX3Serializer(graph, SPDX_VERSION_3_1).serialize(str(out))
    return json.loads((out / "safety.jsonld").read_text())["@graph"]


def _by_type(graph, spdx_type):
    return [e for e in graph if e.get("type") == spdx_type]


def test_contiguous_ranges():
    from zspdx.serializers.spdx3.serializer import SPDX3Serializer
    assert SPDX3Serializer._contiguous_ranges([]) == []
    assert SPDX3Serializer._contiguous_ranges([5]) == [(5, 5)]
    assert SPDX3Serializer._contiguous_ranges([1, 2, 4, 5, 7]) == [(1, 2), (4, 5), (7, 7)]


def _line_span(snippet):
    lr = snippet["software_lineRange"]
    return (lr["beginIntegerRange"], lr["endIntegerRange"])


def test_fs_implemented_by_is_the_full_body(tmp_path):
    graph = _safety_graph(tmp_path)
    byid = {e["spdxId"]: e for e in graph if "spdxId" in e}
    req = next(e for e in _by_type(graph, "Requirement") if e["name"].startswith("ZEP-SRS-1-1"))
    impl = [e for e in _by_type(graph, "LifecycleScopedRelationship")
            if e["relationshipType"] == "implementedBy" and e["from"] == req["spdxId"]]
    assert len(impl) == 1
    assert impl[0]["scope"].endswith("development")
    snippet = byid[impl[0]["to"][0]]
    # implementedBy is the whole z_impl_ function body (lines 2..5)
    assert snippet["name"].startswith("z_impl_k_foo")
    assert _line_span(snippet) == (2, 5)


def test_fs_evidence_is_the_covered_ranges_not_the_body(tmp_path):
    graph = _safety_graph(tmp_path, covered=True)
    byid = {e["spdxId"]: e for e in graph if "spdxId" in e}
    results = _by_type(graph, "functionalsafety_EvaluationResult")
    evidence = _by_type(graph, "functionalsafety_EvidenceRelationship")
    assert len(results) == 1 and len(evidence) == 1
    assert results[0]["functionalsafety_evaluation"].endswith("pass")
    assert evidence[0]["from"] == results[0]["spdxId"]
    assert evidence[0]["functionalsafety_evidenceCategory"][0].endswith("recording")
    # coverage of lines {2, 4} splits into two single-line ranges, and the whole
    # body span (2, 5) is NOT the evidence
    spans = {_line_span(byid[t]) for t in evidence[0]["to"]}
    assert spans == {(2, 2), (4, 4)}


def test_fs_no_evidence_without_coverage(tmp_path):
    # the test runs (verification + result exist) but its coverage never reaches
    # the implementation body, so there is no coverage-backed evidence link.
    graph = _safety_graph(tmp_path, covered=False)
    assert len(_by_type(graph, "functionalsafety_RequirementVerification")) == 1
    assert len(_by_type(graph, "functionalsafety_EvidenceRelationship")) == 0


def test_fs_snippet_imports_source_file_via_external_map(tmp_path):
    graph = _safety_graph(tmp_path, host_in_sbom=True)
    document = _by_type(graph, "SpdxDocument")[0]
    imports = document.get("import", [])
    assert len(imports) == 1 and imports[0]["locationHint"] == "./zephyr.jsonld"
    imported_id = imports[0]["externalSpdxId"]
    snippet = _by_type(graph, "software_Snippet")[0]
    assert snippet["software_snippetFromFile"] == imported_id
    # the imported file is not redefined in the safety document
    assert imported_id not in {f["spdxId"] for f in _by_type(graph, "software_File")}


def _ext_ids(element):
    return [x["identifier"] for x in element.get("externalIdentifier", [])]


def test_fs_adequacy_true_and_broken(tmp_path):
    # verifying test exercises the implementation -> true
    graph = _safety_graph(tmp_path, covered=True)
    req = next(e for e in _by_type(graph, "Requirement") if e["name"].startswith("ZEP-SRS-1-1"))
    assert "adequacy:true" in _ext_ids(req)
    assert "adequacy" in req.get("comment", "")

    # only other tests reach the implementation -> broken (looks verified, isn't)
    graph = _safety_graph(tmp_path, covered=False)
    req = next(e for e in _by_type(graph, "Requirement") if e["name"].startswith("ZEP-SRS-1-1"))
    assert "adequacy:broken" in _ext_ids(req)
    # system requirements are not adjudicated for adequacy
    syrs = next(e for e in _by_type(graph, "Requirement") if e["name"].startswith("ZEP-SYRS-1"))
    assert not any(i.startswith("adequacy:") for i in _ext_ids(syrs))


def test_fs_twister_tool_provenance_and_uses_tool(tmp_path):
    graph = _safety_graph(tmp_path, covered=True)
    tool = next(e for e in _by_type(graph, "Tool") if e.get("name") == "Twister")
    ids = _ext_ids(tool)
    assert "zephyr-version:v4.4.0-1-gdeadbeef" in ids
    assert "coverage-tool:lcov" in ids
    # the evaluation result records the tool that produced it
    result = _by_type(graph, "functionalsafety_EvaluationResult")[0]
    assert any(
        e["relationshipType"] == "usesTool" and e["from"] == result["spdxId"]
        and tool["spdxId"] in e["to"] and e["scope"].endswith("test")
        for e in _by_type(graph, "LifecycleScopedRelationship")
    )


def test_fs_relationships_are_lifecycle_scoped(tmp_path):
    # every FunctionalSafety relationship is anchored to a phase of the safety
    # lifecycle (the V-model): refinement/realization at design, code at
    # development, verification at test.
    graph = _safety_graph(tmp_path, covered=True)
    scoped = _by_type(graph, "LifecycleScopedRelationship")
    by_kind = {}
    for rel in scoped:
        by_kind.setdefault(rel["relationshipType"], set()).add(rel["scope"].split("/")[-1])
    assert by_kind["tracedToDetail"] == {"design"}
    assert by_kind["hasRequirement"] == {"design"}
    assert by_kind["implementedBy"] == {"development"}
    assert by_kind["verifiedBy"] == {"test"}
    assert by_kind["usesTool"] == {"test"}


def test_fs_verification_carries_test_details(tmp_path):
    graph = _safety_graph(tmp_path, covered=True)
    verif = _by_type(graph, "functionalsafety_RequirementVerification")[0]
    assert verif["summary"] == "exercises foo"
    # the multi-line @details structure is preserved (not flattened)
    assert verif["description"] == _FOO_DETAILS
    assert "\n" in verif["description"]


def test_fs_requirement_and_spec_metadata(tmp_path):
    graph = _safety_graph(tmp_path, covered=True)
    req = next(e for e in _by_type(graph, "Requirement") if e["name"].startswith("ZEP-SRS-1-1"))
    assert "status:Draft" in _ext_ids(req)
    assert "requirement-type:Functional" in _ext_ids(req)
    spec = _by_type(graph, "Specification")[0]
    assert "source-document:kernel/foo" in _ext_ids(spec)


def test_fs_noop_without_traceability(tmp_path):
    from zspdx.model import SBOMGraph
    from zspdx.serializers.spdx3 import SPDX3Serializer
    from zspdx.version import SPDX_VERSION_3_1

    graph = SBOMGraph()
    graph.namespace_prefix = "http://spdx.org/spdxdocs/zephyr-empty"
    out = tmp_path / "out"
    out.mkdir()
    assert SPDX3Serializer(graph, SPDX_VERSION_3_1).serialize(str(out))
    assert not (out / "safety.jsonld").exists()
