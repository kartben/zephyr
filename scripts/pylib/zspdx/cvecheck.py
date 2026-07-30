# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Run the external ``sbom-cve-check`` tool against generated SPDX 3.0 documents.

``sbom-cve-check`` (https://pypi.org/project/sbom-cve-check/) parses an SPDX SBOM,
matches its components against public vulnerability databases (NVD, CVE List) using
their CPE/PURL identifiers, and honors the VEX assessments embedded in the SBOM --
including the ones ``west spdx`` generates from the ``security: vex:`` section of
module.yml files.

The check runs on the ``modules-deps`` document, which carries the reference-only
dependency packages holding the security identifiers and VEX statements.
"""

import logging
import os
import shutil
import subprocess

_logger = logging.getLogger(__name__)

# name of the SBOM document the CVE check runs on
CVE_CHECK_DOCUMENT = "modules-deps"

# name of the plain-text report written next to the SPDX documents
CVE_CHECK_SUMMARY_FILE = "cve-check-summary.txt"


def run_cve_check(spdx_dir: str, databases_dir: str = "") -> bool:
    """Check the generated SPDX 3.0 SBOM for known, unpatched vulnerabilities.

    Invokes ``sbom-cve-check`` on ``<spdx_dir>/modules-deps.jsonld`` and writes its
    summary report to ``<spdx_dir>/cve-check-summary.txt``. Vulnerabilities addressed
    by a VEX statement (``fixed`` or ``not_affected``) are not reported as unpatched.

    Args:
        spdx_dir: Directory containing the generated SPDX 3.0 JSON-LD documents.
        databases_dir: Optional directory for the tool's vulnerability databases;
                       when empty the tool uses ``SBOM_CVE_CHECK_DATABASES_DIR`` or
                       its cache-directory default.

    Returns:
        True if the check ran and found no unpatched vulnerabilities, False when
        unpatched vulnerabilities were found or the check could not run.
    """
    tool = shutil.which("sbom-cve-check")
    if not tool:
        _logger.error("sbom-cve-check not found on PATH")
        _logger.error("install it with: pip install sbom-cve-check")
        return False

    sbom_path = os.path.join(spdx_dir, f"{CVE_CHECK_DOCUMENT}.jsonld")
    if not os.path.isfile(sbom_path):
        _logger.error(f"SPDX 3.0 document {sbom_path} not found; cannot run CVE check")
        return False

    summary_path = os.path.join(spdx_dir, CVE_CHECK_SUMMARY_FILE)
    cmd = [
        tool,
        "--sbom-type",
        "spdx3",
        "--sbom-path",
        sbom_path,
        "--export-type",
        "summary",
        "--export-path",
        summary_path,
    ]
    if databases_dir:
        cmd.extend(["--databases-dir", databases_dir])

    # let the tool stream its progress (e.g. vulnerability database downloads) to the user
    _logger.info(f"running CVE check: {' '.join(cmd)}")
    try:
        proc = subprocess.run(cmd, check=False)
    except OSError as e:
        _logger.error(f"failed to run sbom-cve-check: {e}")
        return False
    if proc.returncode != 0:
        _logger.error(f"sbom-cve-check failed with exit code {proc.returncode}")
        return False

    return _report_summary(summary_path)


def _report_summary(summary_path: str) -> bool:
    """Log the summary report and return True when it lists no unpatched vulnerabilities."""
    try:
        with open(summary_path, encoding="utf-8") as f:
            summary_lines = [line.rstrip() for line in f if line.strip()]
    except OSError as e:
        _logger.error(f"cannot read CVE check summary {summary_path}: {e}")
        return False

    if summary_lines:
        _logger.error("CVE check found unpatched vulnerabilities:")
        for line in summary_lines:
            _logger.error(f"  {line}")
        _logger.error(f"full report: {summary_path}")
        _logger.error(
            "address findings upstream, or record their status in the module's "
            "module.yml 'security: vex:' section (e.g. for cherry-picked fixes)"
        )
        return False

    _logger.info("CVE check passed: no unpatched vulnerabilities found")
    return True
