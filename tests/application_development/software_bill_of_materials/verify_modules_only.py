#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Check that ``west spdx --modules-only`` agrees with a full build.

The build-free mode exists so the dependency SBOM can be produced without
compiling anything, which is what the weekly CVE scan consumes. That is only
safe while it stays a true subset of the build-based walk rather than a second
implementation that can drift.

The two documents are not identical: the build-based one also relates each
dependency package to the ``-sources`` package describing the checked-out code,
and those packages do not exist without a build. What must match is the
identity data -- the SPDX ids, versions, download locations and the CPE/PURL
external references -- because that is everything a vulnerability scan reads.
"""

import argparse
import pathlib
import sys

# Fields that identify a package. PackageName is deliberately absent: the
# serializer overwrites it with the CPE product when one is curated, so it says
# less about identity than the SPDXID does.
IDENTITY_TAGS = ("PackageVersion", "PackageDownloadLocation", "PackageSupplier")


def parse_packages(path):
    """Return {SPDXID: {tag: value or sorted list of values}} from a tag-value file."""
    packages = {}
    current = None

    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        tag, separator, value = line.partition(": ")
        if not separator:
            continue
        tag, value = tag.strip(), value.strip()

        if tag == "SPDXID" and value != "SPDXRef-DOCUMENT":
            current = {}
            packages[value] = current
        elif current is not None:
            if tag == "ExternalRef":
                current.setdefault("ExternalRef", []).append(value)
            elif tag in IDENTITY_TAGS:
                current[tag] = value

    for package in packages.values():
        package["ExternalRef"] = sorted(package.get("ExternalRef", []))

    return packages


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--built", required=True, help="modules-deps.spdx from a full build")
    parser.add_argument(
        "--modules-only", required=True, help="modules-deps.spdx from --modules-only"
    )
    args = parser.parse_args()

    built = parse_packages(args.built)
    standalone = parse_packages(args.modules_only)

    problems = []

    missing = sorted(set(built) - set(standalone))
    if missing:
        problems.append(f"packages missing from --modules-only output: {', '.join(missing)}")

    extra = sorted(set(standalone) - set(built))
    if extra:
        problems.append(f"packages only in --modules-only output: {', '.join(extra)}")

    for spdx_id in sorted(set(built) & set(standalone)):
        for tag in (*IDENTITY_TAGS, "ExternalRef"):
            expected, actual = built[spdx_id].get(tag), standalone[spdx_id].get(tag)
            if expected != actual:
                problems.append(f"{spdx_id}: {tag} is {actual!r}, build produced {expected!r}")

    if problems:
        print("--modules-only output does not match the build:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print(f"--modules-only matches the build for all {len(built)} dependency packages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
