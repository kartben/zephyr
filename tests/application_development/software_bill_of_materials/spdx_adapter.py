# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
Format-agnostic adapter for SPDX 2.x (tag-value) and SPDX 3.0 (JSON-LD) documents.

Provides common dataclasses and parser functions so tests can validate
SPDX content regardless of the underlying format.
"""

import json
import os
from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional


# ---------------------------------------------------------------------------
# Common enums
# ---------------------------------------------------------------------------

class ChecksumAlgorithm(Enum):
    SHA1 = "SHA1"
    SHA256 = "SHA256"
    MD5 = "MD5"


class PackagePurpose(Enum):
    SOURCE = "SOURCE"
    APPLICATION = "APPLICATION"
    LIBRARY = "LIBRARY"
    FILE = "FILE"


class RelationshipType(Enum):
    DESCRIBES = "DESCRIBES"
    CONTAINS = "CONTAINS"
    GENERATED_FROM = "GENERATED_FROM"
    STATIC_LINK = "STATIC_LINK"
    DEPENDS_ON = "DEPENDS_ON"
    DYNAMIC_LINK = "DYNAMIC_LINK"
    HAS_PREREQUISITE = "HAS_PREREQUISITE"
    OTHER = "OTHER"


# ---------------------------------------------------------------------------
# Common dataclasses
# ---------------------------------------------------------------------------

@dataclass
class Checksum:
    algorithm: ChecksumAlgorithm
    value: str


@dataclass
class ExternalDocRef:
    document_ref_id: str
    document_uri: str
    checksum: Optional[Checksum] = None


@dataclass
class CreationInfo:
    spdx_version: str          # Normalized: "SPDX-2.2", "SPDX-2.3", "SPDX-3.0"
    data_license: str          # e.g. "CC0-1.0"
    document_namespace: str
    name: str
    spdx_id: str
    creators: List[str] = field(default_factory=list)
    external_document_refs: List[ExternalDocRef] = field(default_factory=list)


@dataclass
class SpdxFile:
    name: str                  # Relative path, normalized with "./" prefix
    spdx_id: str
    checksums: List[Checksum] = field(default_factory=list)
    license_info_in_file: List[str] = field(default_factory=list)
    copyright_text: str = ""


@dataclass
class Package:
    name: str
    spdx_id: str
    primary_package_purpose: Optional[PackagePurpose] = None
    files_analyzed: bool = True
    verification_code: Optional[str] = None


@dataclass
class Relationship:
    spdx_element_id: str       # "from" side
    relationship_type: RelationshipType
    related_spdx_element_id: str  # "to" side


@dataclass
class SpdxDocument:
    creation_info: CreationInfo
    packages: List[Package] = field(default_factory=list)
    files: List[SpdxFile] = field(default_factory=list)
    relationships: List[Relationship] = field(default_factory=list)


# ---------------------------------------------------------------------------
# SPDX 2.x parser (wraps spdx_tools)
# ---------------------------------------------------------------------------

def parse_spdx2(file_path: str) -> SpdxDocument:
    """Parse an SPDX 2.x tag-value file into adapter types."""
    from spdx_tools.spdx.model.checksum import ChecksumAlgorithm as SpdxChecksum
    from spdx_tools.spdx.model.package import PackagePurpose as SpdxPurpose
    from spdx_tools.spdx.model.relationship import RelationshipType as SpdxRelType
    from spdx_tools.spdx.parser.parse_anything import parse_file

    doc = parse_file(file_path)

    # --- Checksum mapping ---
    _checksum_map = {
        SpdxChecksum.SHA1: ChecksumAlgorithm.SHA1,
        SpdxChecksum.SHA256: ChecksumAlgorithm.SHA256,
        SpdxChecksum.MD5: ChecksumAlgorithm.MD5,
    }

    # --- Purpose mapping ---
    _purpose_map = {
        SpdxPurpose.SOURCE: PackagePurpose.SOURCE,
        SpdxPurpose.APPLICATION: PackagePurpose.APPLICATION,
        SpdxPurpose.LIBRARY: PackagePurpose.LIBRARY,
        SpdxPurpose.FILE: PackagePurpose.FILE,
    }

    # --- Relationship type mapping ---
    _rel_map = {
        SpdxRelType.DESCRIBES: RelationshipType.DESCRIBES,
        SpdxRelType.CONTAINS: RelationshipType.CONTAINS,
        SpdxRelType.GENERATED_FROM: RelationshipType.GENERATED_FROM,
        SpdxRelType.STATIC_LINK: RelationshipType.STATIC_LINK,
        SpdxRelType.DEPENDS_ON: RelationshipType.DEPENDS_ON,
        SpdxRelType.DYNAMIC_LINK: RelationshipType.DYNAMIC_LINK,
        SpdxRelType.HAS_PREREQUISITE: RelationshipType.HAS_PREREQUISITE,
    }

    # --- CreationInfo ---
    ext_refs = []
    for ref in doc.creation_info.external_document_refs:
        cksum = None
        if ref.checksum:
            alg = _checksum_map.get(ref.checksum.algorithm)
            if alg:
                cksum = Checksum(algorithm=alg, value=ref.checksum.value)
        ext_refs.append(ExternalDocRef(
            document_ref_id=ref.document_ref_id,
            document_uri=ref.document_uri,
            checksum=cksum,
        ))

    creation_info = CreationInfo(
        spdx_version=doc.creation_info.spdx_version,
        data_license=doc.creation_info.data_license,
        document_namespace=doc.creation_info.document_namespace,
        name=doc.creation_info.name,
        spdx_id=doc.creation_info.spdx_id,
        creators=[str(c) for c in doc.creation_info.creators],
        external_document_refs=ext_refs,
    )

    # --- Packages ---
    packages = []
    for pkg in doc.packages:
        purpose = _purpose_map.get(pkg.primary_package_purpose) if pkg.primary_package_purpose else None
        packages.append(Package(
            name=pkg.name,
            spdx_id=pkg.spdx_id,
            primary_package_purpose=purpose,
            files_analyzed=pkg.files_analyzed if pkg.files_analyzed is not None else True,
            verification_code=(
                pkg.verification_code.value if pkg.verification_code else None
            ),
        ))

    # --- Files ---
    files = []
    for f in doc.files:
        checksums = []
        for c in f.checksums:
            alg = _checksum_map.get(c.algorithm)
            if alg:
                checksums.append(Checksum(algorithm=alg, value=c.value))

        license_strs = []
        if f.license_info_in_file:
            license_strs = [str(lic) for lic in f.license_info_in_file]

        files.append(SpdxFile(
            name=f.name,
            spdx_id=f.spdx_id,
            checksums=checksums,
            license_info_in_file=license_strs,
            copyright_text=str(f.copyright_text) if f.copyright_text else "",
        ))

    # --- Relationships ---
    relationships = []
    for r in doc.relationships:
        rel_type = _rel_map.get(r.relationship_type, RelationshipType.OTHER)
        relationships.append(Relationship(
            spdx_element_id=r.spdx_element_id,
            relationship_type=rel_type,
            related_spdx_element_id=str(r.related_spdx_element_id),
        ))

    return SpdxDocument(
        creation_info=creation_info,
        packages=packages,
        files=files,
        relationships=relationships,
    )


# ---------------------------------------------------------------------------
# SPDX 3.0 parser (JSON-LD)
# ---------------------------------------------------------------------------

# Relationship type mapping: SPDX 3.0 type -> (canonical type, is_reversed)
_SPDX3_REL_MAP = {
    "generates": (RelationshipType.GENERATED_FROM, True),
    "hasStaticLink": (RelationshipType.STATIC_LINK, False),
    "contains": (RelationshipType.CONTAINS, False),
    "describes": (RelationshipType.DESCRIBES, False),
    "dependsOn": (RelationshipType.DEPENDS_ON, False),
    "hasDynamicLink": (RelationshipType.DYNAMIC_LINK, False),
    # SPDX 3.0-specific — map to OTHER for generic tests
    "usesTool": (RelationshipType.OTHER, False),
    "configures": (RelationshipType.OTHER, False),
    "hasConcludedLicense": (RelationshipType.OTHER, False),
    "hasDeclaredLicense": (RelationshipType.OTHER, False),
    "other": (RelationshipType.OTHER, False),
}

_SPDX3_HASH_MAP = {
    "sha1": ChecksumAlgorithm.SHA1,
    "sha256": ChecksumAlgorithm.SHA256,
    "md5": ChecksumAlgorithm.MD5,
}

_SPDX3_PURPOSE_MAP = {
    "source": PackagePurpose.SOURCE,
    "application": PackagePurpose.APPLICATION,
    "library": PackagePurpose.LIBRARY,
    "file": PackagePurpose.FILE,
}


def _normalize_file_name(name: str) -> str:
    """Normalize a file name to always start with './' for consistency with SPDX 2.x."""
    if not name.startswith("./"):
        return "./" + name
    return name


def parse_spdx3(file_path: str) -> SpdxDocument:
    """Parse an SPDX 3.0 JSON-LD file into adapter types."""
    with open(file_path) as f:
        data = json.load(f)

    graph = data.get("@graph", [])

    # Build index by spdxId (or @id for CreationInfo)
    by_id = {}
    for elem in graph:
        sid = elem.get("spdxId") or elem.get("@id")
        if sid:
            by_id[sid] = elem

    # Find the SpdxDocument element
    doc_elem = next((e for e in graph if e.get("type") == "SpdxDocument"), {})

    # --- CreationInfo ---
    ci = doc_elem.get("creationInfo", {})
    if isinstance(ci, str):
        ci = by_id.get(ci, {})

    # Resolve creators: createdBy contains agent IDs
    creators = []
    for agent_id in ci.get("createdBy", []):
        agent = by_id.get(agent_id, {})
        name = agent.get("name", agent_id)
        creators.append(f"Tool: {name}")

    # Resolve data license
    data_license_id = doc_elem.get("dataLicense", "")
    data_license = ""
    if data_license_id:
        lic_elem = by_id.get(data_license_id, {})
        data_license = lic_elem.get("simplelicensing_licenseExpression", "")
        if not data_license and "CC0-1.0" in data_license_id:
            data_license = "CC0-1.0"

    # Extract namespace from namespaceMap
    namespace = ""
    ns_maps = doc_elem.get("namespaceMap", [])
    if ns_maps:
        namespace = ns_maps[0].get("namespace", "")

    creation_info = CreationInfo(
        spdx_version="SPDX-3.0",
        data_license=data_license,
        document_namespace=namespace,
        name=doc_elem.get("name", ""),
        spdx_id=doc_elem.get("spdxId", ""),
        creators=creators,
        external_document_refs=[],  # No concept in SPDX 3.0
    )

    # --- Packages ---
    packages = []
    for elem in graph:
        if elem.get("type") != "software_Package":
            continue
        purpose_str = elem.get("software_primaryPurpose", "")
        purpose = _SPDX3_PURPOSE_MAP.get(purpose_str)
        packages.append(Package(
            name=elem.get("name", ""),
            spdx_id=elem.get("spdxId", ""),
            primary_package_purpose=purpose,
            files_analyzed=True,
            verification_code=None,
        ))

    # --- Files ---
    files = []
    for elem in graph:
        if elem.get("type") != "software_File":
            continue
        # Skip build-config elements (they're software_File but represent configs)
        sid = elem.get("spdxId", "")
        if "build-config" in sid:
            continue

        checksums = []
        for h in elem.get("verifiedUsing", []):
            if h.get("type") != "Hash":
                continue
            alg = _SPDX3_HASH_MAP.get(h.get("algorithm", ""))
            if alg:
                checksums.append(Checksum(algorithm=alg, value=h.get("hashValue", "")))

        # Resolve license info from hasDeclaredLicense relationships
        license_info = []
        for rel_elem in graph:
            if rel_elem.get("type") not in ("Relationship", "LifecycleScopedRelationship"):
                continue
            if rel_elem.get("relationshipType") != "hasDeclaredLicense":
                continue
            if rel_elem.get("from") != sid:
                continue
            for to_id in rel_elem.get("to", []):
                lic_elem = by_id.get(to_id, {})
                lic_str = lic_elem.get("simplelicensing_licenseExpression", "")
                if lic_str:
                    license_info.append(lic_str)

        copyright_text = elem.get("software_copyrightText", "")

        files.append(SpdxFile(
            name=_normalize_file_name(elem.get("name", "")),
            spdx_id=sid,
            checksums=checksums,
            license_info_in_file=license_info,
            copyright_text=copyright_text,
        ))

    # --- Relationships ---
    relationships = []

    # Synthesize DESCRIBES relationships from rootElement
    # SPDX 3.0 uses rootElement on SpdxDocument instead of explicit describes relationships
    doc_spdx_id = doc_elem.get("spdxId", "")
    for root_id in doc_elem.get("rootElement", []):
        relationships.append(Relationship(
            spdx_element_id=doc_spdx_id,
            relationship_type=RelationshipType.DESCRIBES,
            related_spdx_element_id=root_id,
        ))

    for elem in graph:
        if elem.get("type") not in ("Relationship", "LifecycleScopedRelationship"):
            continue
        rel_type_str = elem.get("relationshipType", "other")
        canonical_type, is_reversed = _SPDX3_REL_MAP.get(
            rel_type_str, (RelationshipType.OTHER, False)
        )
        from_id = elem.get("from", "")
        to_ids = elem.get("to", [])

        for to_id in to_ids:
            if is_reversed:
                # Swap from/to: "B generates A" → "A GENERATED_FROM B"
                relationships.append(Relationship(
                    spdx_element_id=to_id,
                    relationship_type=canonical_type,
                    related_spdx_element_id=from_id,
                ))
            else:
                relationships.append(Relationship(
                    spdx_element_id=from_id,
                    relationship_type=canonical_type,
                    related_spdx_element_id=to_id,
                ))

    return SpdxDocument(
        creation_info=creation_info,
        packages=packages,
        files=files,
        relationships=relationships,
    )


# ---------------------------------------------------------------------------
# Unified entry point
# ---------------------------------------------------------------------------

def parse_spdx(file_path: str, spdx_version: str) -> SpdxDocument:
    """Parse any SPDX file, dispatching by version."""
    if spdx_version.startswith("3"):
        return parse_spdx3(file_path)
    return parse_spdx2(file_path)
