#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Check the dependencies in a Zephyr module SBOM against the NVD for known CVEs.

Reads the ``modules-deps`` document produced by ``west spdx --modules-only`` and
reports, as SARIF for the code scanning dashboard, every CVE that applies to a
module the west manifest pulls in.

Why only CPEs are matched
-------------------------
Module metadata may carry both a CPE and a purl (see
:ref:`modules-vulnerability-monitoring`), but only the CPE can be resolved to
vulnerabilities in practice, so only the CPE is queried here:

* OSV does not index ``pkg:github`` at all, and returns nothing for Zephyr's
  pinned revisions -- those are commits on Zephyr's *forks*, which are absent
  from the upstream history OSV's commit ranges are built from.
* Resolving an upstream tag to its upstream commit and querying that does not
  help either; OSV's C/C++ git-range coverage does not extend to the projects
  Zephyr vendors.

So a purl here is provenance, not a lookup key. Please do not "fix" this by
adding an OSV query: it returns nothing, and an empty result is
indistinguishable from a clean scan.

Coverage states
---------------
A green run must not be readable as "no module is vulnerable", because most
modules cannot be matched at all. Every module is therefore reported in one of
four states, and the summary shows the counts:

``scanned``
    A curated CPE carrying a version. Queried against the NVD.
``missing-cpe``
    No curated CPE, and not declared otherwise by the coverage policy. This is
    the actionable backlog, and the only state that raises an alert of its own.
``vendor-sdk``
    An NVD product exists, but the fork carries no upstream version tags, so no
    version can be derived from the pinned revision. Matching without a version
    would report every CVE the vendor ever had. Reported, not matched.
``no-nvd-identity``
    No NVD product exists for the upstream at all. Nothing to curate.

The last two are declared in the coverage policy file, so "triaged, nothing
exists" stays distinguishable from "nobody has looked yet".
"""

import argparse
import collections
import json
import os
import pathlib
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import yaml

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(__file__)), "pylib"))

import sarif_utils  # noqa: E402
from zspdx.model import ExternalReference, ExternalReferenceType  # noqa: E402

DRIVER_NAME = "Zephyr module CVE scan"
DRIVER_URI = "https://docs.zephyrproject.org/latest/develop/modules.html"

NVD_CVE_API = "https://services.nvd.nist.gov/rest/json/cves/2.0"
NVD_CPE_API = "https://services.nvd.nist.gov/rest/json/cpes/2.0"
NVD_CVE_DETAIL = "https://nvd.nist.gov/vuln/detail/{}"

# NVD allows 5 requests per rolling 30 seconds without an API key and 50 with
# one. Stay a little under both so a burst never trips the limiter.
NVD_DELAY_NO_KEY = 6.5
NVD_DELAY_WITH_KEY = 0.7

DEFAULT_MAX_RESULTS = 1000

MISSING_CPE_RULE = "zephyr/module-missing-cpe"

# CVSS metric keys in preference order, newest scoring system first.
CVSS_KEYS = ("cvssMetricV40", "cvssMetricV31", "cvssMetricV30", "cvssMetricV2")

SEVERITY_TO_LEVEL = {
    "CRITICAL": "error",
    "HIGH": "error",
    "MEDIUM": "warning",
    "LOW": "note",
    "NONE": "note",
}

LEVEL_ORDER = {"error": 0, "warning": 1, "note": 2}

# west records a revision with a suffix when the checkout diverges from the
# manifest; strip those before matching a revision back to its manifest line.
REVISION_SUFFIXES = ("-dirty", "-off")


def split_cpe(cpe):
    """Split a CPE 2.3 string into its fields, honouring backslash escapes.

    A plain ``str.split(':')`` breaks on values containing an escaped colon,
    which is legal in a CPE and would silently shift every later field.
    """
    fields = []
    current = []
    escaped = False
    for char in cpe:
        if escaped:
            current.append(char)
            escaped = False
        elif char == "\\":
            current.append(char)
            escaped = True
        elif char == ":":
            fields.append("".join(current))
            current = []
        else:
            current.append(char)
    fields.append("".join(current))
    return fields


def cpe_version(cpe):
    """Return the version field of *cpe*, or "" when it carries none.

    ``*`` (any) and ``-`` (not applicable) are CPE's two ways of saying there is
    no concrete version, and neither can be matched against a version range.
    """
    fields = split_cpe(cpe)
    if len(fields) < 6:
        return ""
    version = fields[5]
    return "" if version in ("*", "-") else version


def cpe_product(cpe):
    """Return the ``<part>:<vendor>:<product>` prefix of *cpe*, or ""."""
    fields = split_cpe(cpe)
    if len(fields) < 5:
        return ""
    return ":".join(fields[2:5])


class NvdError(RuntimeError):
    """A request to the NVD could not be completed."""


class NvdClient:
    """Rate-limited NVD REST client.

    The public API is heavily throttled and intermittently returns 403/503 under
    load, so every request is spaced and retried. A transient failure raises
    rather than returning an empty result: an empty CVE list is
    indistinguishable from "this module is clean", and quietly reporting the
    latter would defeat the point of the scan.
    """

    def __init__(self, api_key=None, delay=None, retries=4, opener=None):
        self.api_key = api_key
        self.delay = (
            delay if delay is not None else (NVD_DELAY_WITH_KEY if api_key else NVD_DELAY_NO_KEY)
        )
        self.retries = retries
        self.opener = opener or urllib.request.urlopen
        self._last_request = 0.0

    def _throttle(self):
        elapsed = time.monotonic() - self._last_request
        if self._last_request and elapsed < self.delay:
            time.sleep(self.delay - elapsed)
        self._last_request = time.monotonic()

    def _get(self, url, params):
        query = urllib.parse.urlencode(params)
        request = urllib.request.Request(
            f"{url}?{query}",
            headers={
                # NVD rejects the default urllib agent often enough to be worth
                # identifying ourselves.
                "User-Agent": "zephyr-cve-scan",
                **({"apiKey": self.api_key} if self.api_key else {}),
            },
        )

        last_error = None
        for attempt in range(self.retries):
            self._throttle()
            try:
                with self.opener(request, timeout=60) as response:
                    return json.load(response)
            except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as e:
                last_error = e
                # 404 is a real answer, not congestion; everything else on this
                # API is worth another try after backing off.
                if isinstance(e, urllib.error.HTTPError) and e.code == 404:
                    break
                backoff = self.delay * (2**attempt)
                print(
                    f"NVD request failed ({e}); retrying in {backoff:.0f}s "
                    f"[{attempt + 1}/{self.retries}]",
                    file=sys.stderr,
                )
                time.sleep(backoff)

        raise NvdError(f"{url} failed after {self.retries} attempts: {last_error}")

    def _paged(self, url, params, collection):
        """Yield every item of *collection* across NVD's paginated responses."""
        start = 0
        while True:
            page = self._get(url, {**params, "startIndex": start})
            items = page.get(collection, [])
            yield from items

            total = page.get("totalResults", 0)
            start += page.get("resultsPerPage", len(items))
            if start >= total or not items:
                return

    def product_known(self, product_prefix):
        """Whether the NVD CPE dictionary holds any entry for this product.

        Distinguishes "no CVEs" from "this CPE does not name anything", which a
        CVE query alone cannot: both come back empty. The module.yml schema does
        not validate locator syntax, so a typo reaches this point intact.
        """
        page = self._get(NVD_CPE_API, {"cpeMatchString": f"cpe:2.3:{product_prefix}"})
        return page.get("totalResults", 0) > 0

    def cves_for_cpe(self, cpe):
        """Return the CVE records matching *cpe*, version ranges included.

        ``virtualMatchString`` is what makes ranges work: a CVE recorded against
        "before 3.6.1" matches a module pinned at 3.5.2, which an exact
        ``cpeName`` lookup would miss.
        """
        return list(self._paged(NVD_CVE_API, {"virtualMatchString": cpe}, "vulnerabilities"))


def cve_severity(cve):
    """Return (base_score, severity) from the best CVSS metric present."""
    metrics = cve.get("metrics", {})
    for key in CVSS_KEYS:
        entries = metrics.get(key)
        if not entries:
            continue
        data = entries[0].get("cvssData", {})
        score = data.get("baseScore")
        severity = data.get("baseSeverity") or entries[0].get("baseSeverity")
        if score is not None:
            return float(score), (severity or "").upper()
    return None, ""


def cve_description(cve):
    for description in cve.get("descriptions", []):
        if description.get("lang") == "en":
            return " ".join(description.get("value", "").split())
    return ""


def cve_cwes(cve):
    """Return the CWE ids recorded against *cve*."""
    cwes = set()
    for weakness in cve.get("weaknesses", []):
        for description in weakness.get("description", []):
            value = description.get("value", "")
            if re.fullmatch(r"CWE-\d+", value):
                cwes.add(value)
    return cwes


class ManifestIndex:
    """Maps a west project to the manifest line pinning its revision.

    Anchoring an alert to that line is what makes it actionable: the fix for a
    vulnerable dependency is to move the revision, and that is the line to move.

    Lookup is by revision rather than by name because a module's name in the
    SBOM comes from its ``zephyr/module.yml``, which need not match the west
    project name it is checked out as.
    """

    _NAME_RE = re.compile(r"^\s*-\s+name:\s*(?P<name>\S+)\s*$")
    _REVISION_RE = re.compile(r"^\s*revision:\s*(?P<revision>\S+)\s*$")

    def __init__(self, manifests, source_root):
        self.by_revision = {}
        self.by_name = {}
        self.default_location = None

        for manifest in manifests:
            path = pathlib.Path(manifest)
            relative = self._relative(path, source_root)
            if self.default_location is None:
                self.default_location = (relative, 1)

            name = None
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                name_match = self._NAME_RE.match(line)
                if name_match:
                    name = name_match.group("name")
                    continue

                revision_match = self._REVISION_RE.match(line)
                # Entries without a revision (the `remotes:` list, `self:`) never
                # reach here, so they cannot shadow a real project.
                if revision_match and name:
                    entry = (name, relative, number)
                    self.by_name.setdefault(name, entry)
                    # SPDX ids cannot contain '_', so a name recovered from one
                    # comes back hyphenated; index both spellings to match it.
                    self.by_name.setdefault(name.replace("_", "-"), entry)
                    self.by_revision.setdefault(revision_match.group("revision"), entry)
                    name = None

    @staticmethod
    def _relative(path, source_root):
        try:
            return path.resolve().relative_to(pathlib.Path(source_root).resolve()).as_posix()
        except ValueError:
            return path.name

    def lookup(self, revision, name):
        """Resolve to (project_name, path, line), falling back progressively."""
        for suffix in REVISION_SUFFIXES:
            if revision and revision.endswith(suffix):
                revision = revision[: -len(suffix)]

        entry = self.by_revision.get(revision) or self.by_name.get(name)
        if entry:
            return entry
        path, line = self.default_location or ("west.yml", 1)
        return name, path, line


Module = collections.namedtuple("Module", "name spdx_id version cpes purls path line")


def parse_sbom(sbom_path, index, source_root):
    """Read the modules-deps SBOM into Module records.

    Keyed on SPDXID rather than PackageName: the SPDX 2 serializer overwrites
    PackageName with the CPE *product* when one is curated, so a package named
    ``mbed_tls`` is the ``mbedtls`` module.
    """
    from spdx_tools.spdx.parser.parse_anything import parse_file

    document = parse_file(str(sbom_path))
    modules = []

    for package in document.packages:
        cpes, purls = [], []
        for reference in package.external_references:
            locator = reference.locator
            classified = ExternalReference.from_locator(locator)
            if classified.reference_type == ExternalReferenceType.CPE23:
                cpes.append(locator)
            elif classified.reference_type == ExternalReferenceType.PURL:
                purls.append(locator)

        revision = ""
        download = package.download_location or ""
        if isinstance(download, str) and "@" in download:
            revision = download.rsplit("@", 1)[1]

        # SPDXRef-<name>-deps, with '_' normalised to '-' on the way in.
        fallback = re.sub(r"^SPDXRef-", "", package.spdx_id)
        fallback = re.sub(r"-deps$", "", fallback)

        name, path, line = index.lookup(revision, fallback)

        # Zephyr itself is not a manifest project; its version comes from VERSION.
        if package.spdx_id == "SPDXRef-zephyr-deps":
            name = "zephyr"
            version_file = pathlib.Path(source_root) / "VERSION"
            if version_file.is_file():
                path, line = "VERSION", 1

        modules.append(
            Module(
                name=name,
                spdx_id=package.spdx_id,
                version=package.version or "",
                cpes=cpes,
                purls=purls,
                path=path,
                line=line,
            )
        )

    return sorted(modules, key=lambda m: m.name)


def load_coverage_policy(path):
    """Load the declared coverage states, or empty ones when no policy is given."""
    if not path:
        return {}, {}

    policy = yaml.safe_load(pathlib.Path(path).read_text(encoding="utf-8")) or {}
    vendor_sdk = policy.get("vendor-sdk") or {}
    no_identity = {name: {} for name in (policy.get("no-nvd-identity") or [])}
    return vendor_sdk, no_identity


def classify(module, vendor_sdk, no_identity):
    """Return (state, detail) for a module carrying no matchable CPE."""
    if module.name in vendor_sdk:
        entry = vendor_sdk[module.name] or {}
        return "vendor-sdk", entry.get("note") or entry.get("cpe-product", "")
    if module.name in no_identity:
        return "no-nvd-identity", "no NVD product exists for this upstream"
    return "missing-cpe", ""


def location(module):
    return {
        "physicalLocation": {
            "artifactLocation": {"uri": module.path},
            "region": {"startLine": module.line},
        }
    }


def cve_rule(cve_id, description, score, severity, cwes):
    """Build the SARIF rule for one CVE."""
    tags = ["security"] + [f"external/cwe/{cwe.lower()}" for cwe in sorted(cwes)]

    rule = {
        "id": cve_id,
        "name": cve_id.replace("-", ""),
        "shortDescription": {"text": f"{cve_id}: known vulnerability in a Zephyr module"},
        "fullDescription": {"text": description or cve_id},
        "helpUri": NVD_CVE_DETAIL.format(cve_id),
        "help": {
            "text": description or cve_id,
            "markdown": f"[{cve_id}]({NVD_CVE_DETAIL.format(cve_id)})\n\n{description}",
        },
        "defaultConfiguration": {"level": SEVERITY_TO_LEVEL.get(severity, "warning")},
        "properties": {"tags": tags},
    }
    if score is not None:
        # GitHub reads this to place the alert in a severity bucket; without it
        # every finding lands in the same undifferentiated pile.
        rule["properties"]["security-severity"] = str(score)
    return rule


def missing_cpe_rule():
    return {
        "id": MISSING_CPE_RULE,
        "name": "ModuleMissingCpe",
        "shortDescription": {"text": "Module has no CPE, so it is never checked for CVEs"},
        "fullDescription": {
            "text": (
                "This module declares no CPE in its zephyr/module.yml security "
                "metadata, so no vulnerability database can be queried for it and "
                "the weekly CVE scan skips it entirely."
            )
        },
        "helpUri": (
            "https://docs.zephyrproject.org/latest/develop/modules.html#vulnerability-monitoring"
        ),
        "help": {
            "text": (
                "Add a versioned CPE 2.3 identifier under security: "
                "external-references: in the module's zephyr/module.yml. If the "
                "upstream has no NVD entry, record the module in "
                "scripts/ci/cve_scan_coverage.yml instead."
            ),
            "markdown": (
                "Add a versioned CPE 2.3 identifier under `security: "
                "external-references:` in the module's `zephyr/module.yml`.\n\n"
                "If the upstream has no NVD entry, record the module under "
                "`no-nvd-identity` in `scripts/ci/cve_scan_coverage.yml` instead, "
                "so the gap reads as triaged rather than overlooked."
            ),
        },
        "defaultConfiguration": {"level": "note"},
        "properties": {"tags": ["security", "coverage"], "security-severity": "0.0"},
    }


def scan(modules, client, vendor_sdk, no_identity):
    """Query the NVD for every matchable module. Returns (results, rules, coverage)."""
    results = []
    rules = {}
    cwe_ids = set()
    coverage = []

    for module in modules:
        matchable = [cpe for cpe in module.cpes if cpe_version(cpe)]

        if not matchable:
            state, detail = classify(module, vendor_sdk, no_identity)
            if module.cpes:
                # A CPE that names no version cannot be matched to a range.
                detail = detail or "curated CPE carries no version"
            coverage.append((module, state, detail, 0))

            if state == "missing-cpe":
                results.append(
                    {
                        "ruleId": MISSING_CPE_RULE,
                        "level": "note",
                        "message": {
                            "text": (
                                f"Module '{module.name}' has no CPE, so it is never "
                                f"checked for known vulnerabilities. Add one to its "
                                f"zephyr/module.yml, or record it in "
                                f"scripts/ci/cve_scan_coverage.yml if its upstream "
                                f"has no NVD entry."
                            )
                        },
                        "locations": [location(module)],
                        "partialFingerprints": {"zephyrCveScan/v1": f"{module.name}:missing-cpe"},
                    }
                )
            continue

        found = 0
        queried = []
        unknown = []
        for cpe in matchable:
            product = cpe_product(cpe)
            if product and not client.product_known(product):
                # Report loudly: silently treating this as "no CVEs" would turn a
                # typo into a permanently clean-looking module.
                print(
                    f"::warning file={module.path},line={module.line},"
                    f"title=Unknown CPE::'{cpe}' on module '{module.name}' names no "
                    f"product in the NVD CPE dictionary; it can never match a CVE",
                    file=sys.stderr,
                )
                unknown.append(product)
                continue
            queried.append(cpe)

            for record in client.cves_for_cpe(cpe):
                cve = record.get("cve", {})
                cve_id = cve.get("id")
                if not cve_id or cve.get("vulnStatus") == "Rejected":
                    continue

                score, severity = cve_severity(cve)
                description = cve_description(cve)
                cwes = cve_cwes(cve)
                cwe_ids |= cwes

                rules.setdefault(cve_id, cve_rule(cve_id, description, score, severity, cwes))

                results.append(
                    {
                        "ruleId": cve_id,
                        "level": SEVERITY_TO_LEVEL.get(severity, "warning"),
                        "message": {
                            "text": (
                                f"{cve_id} ({severity or 'unrated'}) affects module "
                                f"'{module.name}', pinned here and identified as "
                                f"{cpe}. {description}"
                            )
                        },
                        "locations": [location(module)],
                        # Keyed on identity rather than position so the alert
                        # survives the manifest being reordered.
                        "partialFingerprints": {"zephyrCveScan/v1": f"{module.name}:{cve_id}"},
                    }
                )
                found += 1

        if queried:
            detail = ", ".join(queried)
            if unknown:
                detail += f" (ignored unknown product(s): {', '.join(unknown)})"
            coverage.append((module, "scanned", detail, found))
        else:
            # Every CPE named a product the NVD does not know, so nothing was
            # actually checked. Saying "scanned, 0 CVEs" here would be a lie.
            coverage.append((module, "invalid-cpe", f"unknown product(s): {', '.join(unknown)}", 0))

    return results, rules, cwe_ids, coverage


def write_coverage(coverage, output):
    """Write the coverage table that keeps a green run honest."""
    counts = collections.Counter(state for _, state, _, _ in coverage)
    total = len(coverage)
    scanned = counts.get("scanned", 0)

    lines = [
        "## Module CVE coverage",
        "",
        f"**{scanned} of {total} modules** carry an identity that can be matched against the NVD.",
        "",
        "| state | count |",
        "| --- | --- |",
    ]
    for state, count in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
        lines.append(f"| `{state}` | {count} |")

    lines += ["", "| module | state | CVEs | detail |", "| --- | --- | --- | --- |"]
    order = {"invalid-cpe": 0, "scanned": 1, "missing-cpe": 2, "vendor-sdk": 3}
    for module, state, detail, found in sorted(
        coverage, key=lambda row: (order.get(row[1], 4), -row[3], row[0].name)
    ):
        cves = str(found) if state == "scanned" else "—"
        lines.append(f"| `{module.name}` | `{state}` | {cves} | {detail} |")

    text = "\n".join(lines) + "\n"
    if output:
        pathlib.Path(output).write_text(text, encoding="utf-8")
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument(
        "--sbom", required=True, help="modules-deps SPDX document to read identities from"
    )
    parser.add_argument(
        "--manifest",
        action="append",
        default=[],
        help="west manifest to anchor findings in; repeatable",
    )
    parser.add_argument("--coverage-policy", help="YAML declaring triaged coverage states")
    parser.add_argument("-o", "--output", default="results.sarif")
    parser.add_argument("--coverage-out", help="write the coverage table here")
    parser.add_argument(
        "--source-root",
        default=os.environ.get("ZEPHYR_BASE", os.getcwd()),
        help="repository root that result paths are made relative to",
    )
    parser.add_argument("--max-results", type=int, default=DEFAULT_MAX_RESULTS)
    args = parser.parse_args(argv)

    manifests = args.manifest or [os.path.join(args.source_root, "west.yml")]
    index = ManifestIndex(manifests, args.source_root)
    modules = parse_sbom(args.sbom, index, args.source_root)

    vendor_sdk, no_identity = load_coverage_policy(args.coverage_policy)
    client = NvdClient(api_key=os.environ.get("NVD_API_KEY"))

    results, rules, cwe_ids, coverage = scan(modules, client, vendor_sdk, no_identity)

    if any(result["ruleId"] == MISSING_CPE_RULE for result in results):
        rules[MISSING_CPE_RULE] = missing_cpe_rule()

    # Worst first, so truncation keeps what matters most.
    results.sort(
        key=lambda r: (
            LEVEL_ORDER.get(r.get("level"), 3),
            sarif_utils.stable_key(r.get("ruleId"), r["locations"][0]["physicalLocation"]),
        )
    )
    results, dropped = sarif_utils.cap_results(results, args.max_results)

    document = sarif_utils.build_document(
        DRIVER_NAME,
        DRIVER_URI,
        [rules[key] for key in sorted(rules)],
        results,
        sarif_utils.make_cwe_taxonomy(cwe_ids),
    )

    counts = collections.Counter(state for _, state, _, _ in coverage)
    sarif_utils.write_report(
        document,
        args.output,
        dropped,
        args.max_results,
        f"checked {counts.get('scanned', 0)} of {len(modules)} modules against the NVD "
        f"({counts.get('missing-cpe', 0)} without a CPE, "
        f"{counts.get('vendor-sdk', 0)} vendor SDK forks, "
        f"{counts.get('no-nvd-identity', 0)} with no NVD identity)",
    )

    summary = write_coverage(coverage, args.coverage_out)
    step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if step_summary:
        with open(step_summary, "a", encoding="utf-8") as handle:
            handle.write(summary)

    return 0


if __name__ == "__main__":
    sys.exit(main())
