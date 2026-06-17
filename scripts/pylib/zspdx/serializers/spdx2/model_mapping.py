# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Pure conversions from the internal SBOM model to spdx-tools 2.x model objects.

These helpers are deliberately free of any serializer state (IDs, namespaces,
two-pass hashing): they only translate a single SBOM element or value into the
equivalent ``spdx_tools.spdx.model`` object. The serializer is responsible for
IDs, document assembly and output.
"""

import hashlib
import logging

from license_expression import get_spdx_licensing
from spdx_tools.spdx.model import (
    Actor,
    ActorType,
    Checksum,
    ChecksumAlgorithm,
    ExternalPackageRef,
    ExternalPackageRefCategory,
    PackagePurpose,
    PackageVerificationCode,
)
from spdx_tools.spdx.model.spdx_no_assertion import SpdxNoAssertion
from spdx_tools.spdx.model.spdx_none import SpdxNone

from zspdx.model import NOASSERTION, ExternalReferenceType

_logger = logging.getLogger(__name__)

# Shared SPDX license expression parser.
_LICENSING = get_spdx_licensing()

# Hash algorithm names (as stored in SBOMFile.hashes) emitted in a fixed order
# so the generated output is deterministic across the two-pass write.
_HASH_ALGORITHMS = (
    ("SHA1", ChecksumAlgorithm.SHA1),
    ("SHA256", ChecksumAlgorithm.SHA256),
    ("MD5", ChecksumAlgorithm.MD5),
)


def to_license(expression: str):
    """Convert a license string into an spdx-tools license value.

    ``NOASSERTION`` maps to :class:`SpdxNoAssertion`, ``NONE`` to :class:`SpdxNone`,
    and anything else is parsed as an SPDX license expression (without validation,
    so custom identifiers are preserved as-is).
    """
    if not expression or expression == NOASSERTION:
        return SpdxNoAssertion()
    if expression == "NONE":
        return SpdxNone()
    return _LICENSING.parse(expression, validate=False)


def to_copyright(text: str):
    """Convert a copyright string into text or an spdx-tools sentinel."""
    if not text or text == NOASSERTION:
        return SpdxNoAssertion()
    if text == "NONE":
        return SpdxNone()
    return text


def license_info_from_files(values: list[str]) -> list:
    """Map a package's ``license_info_from_files``; empty defaults to NOASSERTION."""
    if not values:
        return [SpdxNoAssertion()]
    return [to_license(value) for value in values]


def license_info_in_file(values: list[str]) -> list:
    """Map a file's ``license_info_in_file``; empty defaults to NONE."""
    if not values:
        return [SpdxNone()]
    return [to_license(value) for value in values]


def to_purpose(purpose) -> PackagePurpose | None:
    """Convert a ComponentPurpose enum to a PackagePurpose, or None if unset."""
    if purpose is None:
        return None
    return PackagePurpose.__members__.get(purpose.name)


def to_supplier(supplier: str) -> Actor | None:
    """Convert a supplier name to an Organization Actor, or None if unset."""
    if not supplier:
        return None
    return Actor(ActorType.ORGANIZATION, supplier)


def to_external_refs(references) -> list[ExternalPackageRef]:
    """Convert SBOM external references to spdx-tools ExternalPackageRefs."""
    refs = []
    for ref in references:
        if ref.reference_type == ExternalReferenceType.CPE23:
            refs.append(
                ExternalPackageRef(ExternalPackageRefCategory.SECURITY, "cpe23Type", ref.locator)
            )
        elif ref.reference_type == ExternalReferenceType.PURL:
            refs.append(
                ExternalPackageRef(
                    ExternalPackageRefCategory.PACKAGE_MANAGER, "purl", ref.locator
                )
            )
        else:
            _logger.warning("Unknown external reference (%s)", ref.locator)
    return refs


def to_checksums(hashes: dict[str, str]) -> list[Checksum]:
    """Convert a file hash dict to a list of spdx-tools Checksums (fixed order)."""
    return [
        Checksum(algorithm, hashes[name])
        for name, algorithm in _HASH_ALGORITHMS
        if hashes.get(name)
    ]


def verification_code(component) -> PackageVerificationCode:
    """Compute the SPDX package verification code over a component's files.

    The code is the SHA1 of the concatenation of the (sorted) SHA1 hashes of all
    files in the package, matching the SPDX 2.x specification.
    """
    file_sha1s = sorted(
        file_obj.hashes["SHA1"]
        for file_obj in component.files.values()
        if "SHA1" in file_obj.hashes
    )
    digest = hashlib.sha1(usedforsecurity=False)
    digest.update("".join(file_sha1s).encode("utf-8"))
    return PackageVerificationCode(digest.hexdigest())
