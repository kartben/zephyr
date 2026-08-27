#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for scripts/ci/cve_scan.py.

The NVD client is stubbed throughout, so the suite never reaches the network:
the live API is rate-limited and intermittently unavailable, which would make
these tests both slow and flaky for reasons unrelated to the code.
"""

import json
import os
import sys
import textwrap

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "ci"))

import cve_scan as sut  # noqa: E402

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


class FakeNvd:
    """Stands in for NvdClient, recording what it was asked."""

    def __init__(self, cves=None, known=True):
        self.cves = cves or {}
        self.known = known
        self.product_queries = []
        self.cve_queries = []

    def product_known(self, product_prefix):
        self.product_queries.append(product_prefix)
        if callable(self.known):
            return self.known(product_prefix)
        return self.known

    def cves_for_cpe(self, cpe):
        self.cve_queries.append(cpe)
        return self.cves.get(cpe, [])


def make_cve(cve_id, score=7.5, severity="HIGH", cwe="CWE-121", status="Analyzed"):
    return {
        "cve": {
            "id": cve_id,
            "vulnStatus": status,
            "descriptions": [
                {"lang": "es", "value": "no debe usarse"},
                {"lang": "en", "value": f"A flaw described by {cve_id}."},
            ],
            "metrics": {
                "cvssMetricV31": [{"cvssData": {"baseScore": score, "baseSeverity": severity}}]
            },
            "weaknesses": [{"description": [{"lang": "en", "value": cwe}]}],
        }
    }


def module(name, cpes=(), purls=(), path="west.yml", line=1, version="1.0"):
    return sut.Module(
        name=name,
        spdx_id=f"SPDXRef-{name}-deps",
        version=version,
        cpes=list(cpes),
        purls=list(purls),
        path=path,
        line=line,
    )


MANIFEST = textwrap.dedent(
    """\
    manifest:
      remotes:
        - name: upstream
          url-base: https://github.com/zephyrproject-rtos
      projects:
        - name: mbedtls
          revision: c43f34b93797e81fd0257c30004dcd0ae332ae51
          path: modules/crypto/mbedtls
        - name: hal_espressif
          revision: aabbccddeeff00112233445566778899aabbccdd
          path: modules/hal/espressif
      self:
        path: zephyr
    """
)


# ---------------------------------------------------------------------------
# CPE parsing
# ---------------------------------------------------------------------------


def test_split_cpe_honours_escaped_colons():
    """An escaped colon is part of a value, not a field separator."""
    cpe = r"cpe:2.3:a:vendor:pro\:duct:1.0:*:*:*:*:*:*:*"
    fields = sut.split_cpe(cpe)
    assert fields[4] == r"pro\:duct"
    assert fields[5] == "1.0"


@pytest.mark.parametrize(
    "cpe, expected",
    [
        ("cpe:2.3:a:trustedfirmware:mbed_tls:3.5.2:*:*:*:*:*:*:*", "3.5.2"),
        ("cpe:2.3:o:google:openthread:2019-12-13:*:*:*:*:*:*:*", "2019-12-13"),
        # '*' (any) and '-' (not applicable) both mean "no concrete version",
        # and neither can be matched against a CVE's version range.
        ("cpe:2.3:a:espressif:esp-idf:*:*:*:*:*:*:*:*", ""),
        ("cpe:2.3:a:vendor:product:-:*:*:*:*:*:*:*", ""),
        ("cpe:2.3:a:vendor", ""),
    ],
)
def test_cpe_version(cpe, expected):
    assert sut.cpe_version(cpe) == expected


def test_cpe_product_keeps_the_part_field():
    """openthread is part 'o', not 'a'; dropping the part would misquery it."""
    assert (
        sut.cpe_product("cpe:2.3:o:google:openthread:2019-12-13:*:*:*:*:*:*:*")
        == "o:google:openthread"
    )
    assert sut.cpe_product("cpe:2.3:a:elm-chan:fatfs:r0.16:*:*:*:*:*:*:*") == "a:elm-chan:fatfs"


# ---------------------------------------------------------------------------
# CVE record extraction
# ---------------------------------------------------------------------------


def test_cve_severity_prefers_the_newest_scoring_system():
    cve = {
        "metrics": {
            "cvssMetricV40": [{"cvssData": {"baseScore": 9.1, "baseSeverity": "critical"}}],
            "cvssMetricV31": [{"cvssData": {"baseScore": 5.0, "baseSeverity": "MEDIUM"}}],
        }
    }
    assert sut.cve_severity(cve) == (9.1, "CRITICAL")


def test_cve_severity_without_metrics():
    assert sut.cve_severity({}) == (None, "")


def test_cve_description_picks_english():
    cve = make_cve("CVE-2024-0001")["cve"]
    assert sut.cve_description(cve) == "A flaw described by CVE-2024-0001."


def test_cve_cwes_ignores_non_cwe_values():
    cve = {
        "weaknesses": [
            {"description": [{"value": "CWE-787"}, {"value": "NVD-CWE-noinfo"}]},
        ]
    }
    assert sut.cve_cwes(cve) == {"CWE-787"}


# ---------------------------------------------------------------------------
# Manifest anchoring
# ---------------------------------------------------------------------------


@pytest.fixture
def manifest(tmp_path):
    path = tmp_path / "west.yml"
    path.write_text(MANIFEST, encoding="utf-8")
    return path


def test_manifest_index_anchors_to_the_revision_line(tmp_path, manifest):
    index = sut.ManifestIndex([str(manifest)], str(tmp_path))
    name, path, line = index.lookup("c43f34b93797e81fd0257c30004dcd0ae332ae51", "ignored")
    assert (name, path) == ("mbedtls", "west.yml")
    # The line the alert points at must be the one a maintainer edits.
    assert MANIFEST.splitlines()[line - 1].strip().startswith("revision: c43f34b9")


def test_manifest_index_ignores_entries_without_a_revision(tmp_path, manifest):
    """The `remotes:` list also has `- name:` entries; they must not shadow projects."""
    index = sut.ManifestIndex([str(manifest)], str(tmp_path))
    assert "upstream" not in index.by_name


def test_manifest_index_matches_hyphenated_spdx_names(tmp_path, manifest):
    """SPDX ids cannot contain '_', so a name recovered from one is hyphenated."""
    index = sut.ManifestIndex([str(manifest)], str(tmp_path))
    name, _, line = index.lookup("no-such-revision", "hal-espressif")
    assert name == "hal_espressif"
    assert MANIFEST.splitlines()[line - 1].strip().startswith("revision: aabbccdd")


def test_manifest_index_strips_west_revision_suffixes(tmp_path, manifest):
    """west appends -dirty/-off when a checkout diverges from the manifest."""
    index = sut.ManifestIndex([str(manifest)], str(tmp_path))
    name, _, _ = index.lookup("c43f34b93797e81fd0257c30004dcd0ae332ae51-dirty", "x")
    assert name == "mbedtls"


def test_manifest_index_falls_back_to_a_real_location(tmp_path, manifest):
    """Every result needs a physicalLocation or code scanning rejects the upload."""
    index = sut.ManifestIndex([str(manifest)], str(tmp_path))
    name, path, line = index.lookup("unknown", "unknown")
    assert (name, path, line) == ("unknown", "west.yml", 1)


# ---------------------------------------------------------------------------
# SBOM parsing
# ---------------------------------------------------------------------------


SBOM = textwrap.dedent(
    """\
    SPDXVersion: SPDX-2.3
    DataLicense: CC0-1.0
    SPDXID: SPDXRef-DOCUMENT
    DocumentName: modules-deps
    DocumentNamespace: http://spdx.org/spdxdocs/zephyr-test/modules-deps
    Creator: Organization: The Zephyr Project
    Created: 2026-01-01T00:00:00Z

    ##### Package: mbed-tls

    PackageName: mbed_tls
    SPDXID: SPDXRef-mbedtls-deps
    PackageLicenseConcluded: NOASSERTION
    PackageLicenseDeclared: NOASSERTION
    PackageCopyrightText: NOASSERTION
    PackageDownloadLocation: git+https://github.com/zephyrproject-rtos/mbedtls@\
c43f34b93797e81fd0257c30004dcd0ae332ae51
    PackageVersion: 3.5.2
    ExternalRef: SECURITY cpe23Type cpe:2.3:a:trustedfirmware:mbed_tls:3.5.2:*:*:*:*:*:*:*
    ExternalRef: PACKAGE-MANAGER purl pkg:github/Mbed-TLS/mbedtls@v3.5.2
    FilesAnalyzed: false
    """
)


def test_parse_sbom_keys_on_spdx_id_not_package_name(tmp_path):
    """The serializer overwrites PackageName with the CPE product.

    ``mbedtls-deps`` is emitted as ``PackageName: mbed_tls``, so identity has to
    come from the SPDXID and the pinned revision instead.
    """
    sbom = tmp_path / "modules-deps.spdx"
    sbom.write_text(SBOM, encoding="utf-8")
    manifest = tmp_path / "west.yml"
    manifest.write_text(MANIFEST, encoding="utf-8")

    index = sut.ManifestIndex([str(manifest)], str(tmp_path))
    modules = sut.parse_sbom(str(sbom), index, str(tmp_path))

    assert len(modules) == 1
    found = modules[0]
    # Not "mbed_tls", which is what the package is called in the document.
    assert found.name == "mbedtls"
    assert found.spdx_id == "SPDXRef-mbedtls-deps"
    assert found.cpes == ["cpe:2.3:a:trustedfirmware:mbed_tls:3.5.2:*:*:*:*:*:*:*"]
    assert found.purls == ["pkg:github/Mbed-TLS/mbedtls@v3.5.2"]
    assert found.path == "west.yml"


# ---------------------------------------------------------------------------
# Coverage policy
# ---------------------------------------------------------------------------


def test_load_coverage_policy(tmp_path):
    policy = tmp_path / "policy.yml"
    policy.write_text(
        textwrap.dedent(
            """\
            vendor-sdk:
              hal_nxp:
                cpe-product: a:nxp:mcuxpresso_software_development_kit
                note: no tags in fork
            no-nvd-identity:
              - mcuboot
              - lvgl
            """
        ),
        encoding="utf-8",
    )
    vendor_sdk, no_identity = sut.load_coverage_policy(str(policy))
    assert "hal_nxp" in vendor_sdk
    assert set(no_identity) == {"mcuboot", "lvgl"}


def test_load_coverage_policy_without_a_file():
    assert sut.load_coverage_policy(None) == ({}, {})


def test_classify_states():
    vendor_sdk = {"hal_nxp": {"note": "no tags in fork"}}
    no_identity = {"mcuboot": {}}

    assert sut.classify(module("hal_nxp"), vendor_sdk, no_identity)[0] == "vendor-sdk"
    assert sut.classify(module("mcuboot"), vendor_sdk, no_identity)[0] == "no-nvd-identity"
    # Anything not declared is an actionable gap, so silence never reads as triaged.
    assert sut.classify(module("openthread"), vendor_sdk, no_identity)[0] == "missing-cpe"


# ---------------------------------------------------------------------------
# Scanning
# ---------------------------------------------------------------------------


CPE = "cpe:2.3:a:trustedfirmware:mbed_tls:3.5.2:*:*:*:*:*:*:*"


def test_scan_reports_cves_anchored_to_the_module():
    client = FakeNvd({CPE: [make_cve("CVE-2024-28960", 8.2, "HIGH")]})
    results, rules, cwes, coverage = sut.scan(
        [module("mbedtls", cpes=[CPE], line=328)], client, {}, {}
    )

    assert len(results) == 1
    result = results[0]
    assert result["ruleId"] == "CVE-2024-28960"
    assert result["level"] == "error"
    location = result["locations"][0]["physicalLocation"]
    assert location["artifactLocation"]["uri"] == "west.yml"
    assert location["region"]["startLine"] == 328
    # Keyed on identity so the alert survives the manifest being reordered.
    assert result["partialFingerprints"] == {"zephyrCveScan/v1": "mbedtls:CVE-2024-28960"}

    assert rules["CVE-2024-28960"]["properties"]["security-severity"] == "8.2"
    assert cwes == {"CWE-121"}
    assert coverage[0][1] == "scanned"
    assert coverage[0][3] == 1


def test_scan_skips_rejected_cves():
    client = FakeNvd({CPE: [make_cve("CVE-2024-0002", status="Rejected")]})
    results, _, _, coverage = sut.scan([module("mbedtls", cpes=[CPE])], client, {}, {})
    assert results == []
    assert coverage[0][1] == "scanned"


def test_scan_skips_versionless_cpes():
    """A CPE with no version cannot be matched against a range, so it is not queried."""
    versionless = "cpe:2.3:a:espressif:esp-idf:*:*:*:*:*:*:*:*"
    client = FakeNvd()
    _, _, _, coverage = sut.scan(
        [module("hal_espressif", cpes=[versionless])],
        client,
        {"hal_espressif": {"note": "no tags"}},
        {},
    )
    assert client.cve_queries == []
    assert coverage[0][1] == "vendor-sdk"


def test_unresolvable_cpe_is_not_reported_as_scanned():
    """A CPE naming nothing must not read as a clean module.

    Both cases come back with zero CVEs, so conflating them would let a typo in
    module.yml make a module look permanently checked and clean.
    """
    client = FakeNvd(known=False)
    results, _, _, coverage = sut.scan(
        [module("mcuboot", cpes=["cpe:2.3:a:nobody:nothing:1.0:*:*:*:*:*:*:*"])],
        client,
        {},
        {},
    )
    assert coverage[0][1] == "invalid-cpe"
    assert not any(r["ruleId"] == sut.MISSING_CPE_RULE for r in results)


def test_partially_valid_cpes_still_scan():
    """One bad CPE alongside a good one must not suppress the good one."""
    client = FakeNvd(
        {CPE: [make_cve("CVE-2024-28960")]},
        known=lambda product: product != "a:nobody:nothing",
    )
    results, _, _, coverage = sut.scan(
        [module("mbedtls", cpes=["cpe:2.3:a:nobody:nothing:1.0:*:*:*:*:*:*:*", CPE])],
        client,
        {},
        {},
    )
    assert len(results) == 1
    assert coverage[0][1] == "scanned"
    assert "ignored unknown product(s)" in coverage[0][2]


def test_missing_cpe_raises_a_note_level_alert():
    results, _, _, coverage = sut.scan([module("openthread", line=42)], FakeNvd(), {}, {})
    assert len(results) == 1
    assert results[0]["ruleId"] == sut.MISSING_CPE_RULE
    assert results[0]["level"] == "note"
    assert results[0]["locations"][0]["physicalLocation"]["region"]["startLine"] == 42
    assert coverage[0][1] == "missing-cpe"


def test_declared_states_raise_no_alert():
    """Triaged modules are reported in the summary, not the dashboard."""
    modules = [module("hal_nxp"), module("mcuboot")]
    results, _, _, coverage = sut.scan(
        modules, FakeNvd(), {"hal_nxp": {"note": "no tags"}}, {"mcuboot": {}}
    )
    assert results == []
    assert [row[1] for row in coverage] == ["vendor-sdk", "no-nvd-identity"]


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def test_missing_cpe_rule_is_note_level_with_zero_severity():
    rule = sut.missing_cpe_rule()
    assert rule["defaultConfiguration"]["level"] == "note"
    # A coverage gap is not itself a vulnerability.
    assert rule["properties"]["security-severity"] == "0.0"


def test_cve_rule_carries_cwe_tags():
    rule = sut.cve_rule("CVE-2024-0003", "desc", 4.3, "MEDIUM", {"CWE-787"})
    assert "security" in rule["properties"]["tags"]
    assert "external/cwe/cwe-787" in rule["properties"]["tags"]
    assert rule["defaultConfiguration"]["level"] == "warning"
    assert rule["helpUri"].endswith("CVE-2024-0003")


def test_write_coverage_states_how_much_was_actually_checked(tmp_path):
    coverage = [
        (module("mbedtls"), "scanned", CPE, 17),
        (module("openthread"), "missing-cpe", "", 0),
        (module("hal_nxp"), "vendor-sdk", "no tags in fork", 0),
    ]
    out = tmp_path / "coverage.md"
    text = sut.write_coverage(coverage, str(out))

    assert out.read_text(encoding="utf-8") == text
    # The headline number is what stops a green run reading as "nothing is vulnerable".
    assert "**1 of 3 modules**" in text
    assert "`missing-cpe`" in text
    assert "| `mbedtls` | `scanned` | 17 |" in text


def test_main_writes_sarif_and_coverage(tmp_path, monkeypatch):
    """End to end through main(), with the network stubbed out."""
    sbom = tmp_path / "modules-deps.spdx"
    sbom.write_text(SBOM, encoding="utf-8")
    manifest = tmp_path / "west.yml"
    manifest.write_text(MANIFEST, encoding="utf-8")
    sarif = tmp_path / "results.sarif"
    coverage = tmp_path / "coverage.md"

    monkeypatch.setattr(
        sut, "NvdClient", lambda **kwargs: FakeNvd({CPE: [make_cve("CVE-2024-28960")]})
    )
    monkeypatch.delenv("GITHUB_STEP_SUMMARY", raising=False)

    assert (
        sut.main(
            [
                "--sbom",
                str(sbom),
                "--manifest",
                str(manifest),
                "-o",
                str(sarif),
                "--coverage-out",
                str(coverage),
                "--source-root",
                str(tmp_path),
            ]
        )
        == 0
    )

    document = json.loads(sarif.read_text(encoding="utf-8"))
    assert document["version"] == "2.1.0"
    run = document["runs"][0]
    assert [r["ruleId"] for r in run["results"]] == ["CVE-2024-28960"]
    assert run["taxonomies"][0]["name"] == "CWE"
    assert "**1 of 1 modules**" in coverage.read_text(encoding="utf-8")
