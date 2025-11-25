# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""SPDX 3.0 JSON-LD format writer."""

import json
import re
from datetime import datetime, timezone

from west import log

from zspdx.util import generate_download_url, normalize_spdx_name

try:
    from spdx_python_model import v3_0_1 as spdx
except ImportError:
    spdx = None


def _get_full_uri(spdx_id, doc, ext_doc_map):
    """
    Resolve an SPDX ID to a full URI.

    Arguments:
        spdx_id: the SPDX ID to resolve
        doc: the document object containing namespace info
        ext_doc_map: map of external document references to namespaces
    Returns: full URI for the SPDX ID
    """

    if ":" in spdx_id and spdx_id.startswith("DocumentRef-"):
        doc_ref, local_id = spdx_id.split(":", 1)
        if doc_ref in ext_doc_map:
            ns = ext_doc_map[doc_ref]
            return f"{ns}#{local_id}"
        else:
            log.wrn(f"Unknown external document reference: {doc_ref}")
            return spdx_id

    if spdx_id == "SPDXRef-DOCUMENT":
        return f"{doc.cfg.namespace}/{doc.cfg.name}"

    return f"{doc.cfg.namespace}#{spdx_id}"


class LicenseCache:
    """Cache for license expression objects to avoid duplicates."""

    def __init__(self, namespace, creation_info_id, elements, spdx_doc):
        self.namespace = namespace
        self.creation_info_id = creation_info_id
        self.elements = elements
        self.spdx_doc = spdx_doc
        self.cache = {}

    def get_license_id(self, license_str):
        """Get or create a license expression object for the given license string."""
        if license_str in self.cache:
            return self.cache[license_str]

        # Create new license expression object
        # Sanitize license_str for ID
        safe_id = re.sub(r'[^a-zA-Z0-9.-]', '-', license_str)
        lic_id = f"{self.namespace}#License-{safe_id}"

        lic = spdx.simplelicensing_LicenseExpression()
        lic._id = lic_id
        lic.simplelicensing_licenseExpression = license_str
        lic.creationInfo = self.creation_info_id

        self.elements.append(lic)
        self.spdx_doc.element.append(lic._id)

        self.cache[license_str] = lic_id
        return lic_id


def _add_license_relationship(element_id, license_str, relationship_type,
                               license_cache, namespace, creation_info_id,
                               elements, spdx_doc):
    """
    Add a license relationship to an element.

    Arguments:
        element_id: the ID of the element to add the relationship to
        license_str: the license string
        relationship_type: the type of license relationship
        license_cache: cache for license expressions
        namespace: document namespace
        creation_info_id: creation info ID
        elements: list to append new elements to
        spdx_doc: the SPDX document object
    """
    if not license_str or license_str == "NOASSERTION" or license_str == "NONE":
        return

    lic_id = license_cache.get_license_id(license_str)

    rel = spdx.Relationship()
    # Create deterministic ID
    rel_hash = abs(hash(element_id + lic_id + str(relationship_type)))
    rel._id = f"{namespace}#Rel-License-{rel_hash}"
    rel.creationInfo = creation_info_id
    rel.relationshipType = relationship_type
    rel.from_ = element_id
    rel.to = [lic_id]

    elements.append(rel)
    spdx_doc.element.append(rel._id)


def _map_relationship_type(rln_type):
    """
    Map SPDX 2.x relationship type to SPDX 3.x.

    Arguments:
        rln_type: SPDX 2.x relationship type string
    Returns: tuple of (spdx3_type, swap) where swap indicates if from/to should be swapped
    """
    swap = False
    spdx_type = None

    if rln_type == "DESCRIBES":
        spdx_type = spdx.RelationshipType.describes
    elif rln_type == "CONTAINS":
        spdx_type = spdx.RelationshipType.contains
    elif rln_type == "HAS_PREREQUISITE":
        spdx_type = spdx.RelationshipType.hasPrerequisite
    elif rln_type == "STATIC_LINK":
        spdx_type = spdx.RelationshipType.hasStaticLink
    elif rln_type == "GENERATED_FROM":
        spdx_type = spdx.RelationshipType.generates
        swap = True
    else:
        spdx_type = spdx.RelationshipType.other

    return spdx_type, swap


def _map_purpose(purpose_str):
    """
    Map SPDX 2.x purpose string to SPDX 3.x software purpose.

    Arguments:
        purpose_str: SPDX 2.x purpose string
    Returns: SPDX 3.x software purpose or None
    """
    purpose_map = {
        "APPLICATION": spdx.software_SoftwarePurpose.application,
        "LIBRARY": spdx.software_SoftwarePurpose.library,
        "SOURCE": spdx.software_SoftwarePurpose.source,
        "CONTAINER": spdx.software_SoftwarePurpose.container,
        "OPERATING-SYSTEM": spdx.software_SoftwarePurpose.operatingSystem,
        "FIRMWARE": spdx.software_SoftwarePurpose.firmware,
        "FILE": spdx.software_SoftwarePurpose.file
    }
    return purpose_map.get(purpose_str)


def _create_agent_and_creation_info(namespace, elements):
    """
    Create the Agent and CreationInfo objects for an SPDX 3.0 document.

    Arguments:
        namespace: document namespace
        elements: list to append new elements to
    Returns: tuple of (agent, creation_info)
    """
    agent = spdx.Agent()
    agent._id = f"{namespace}#Agent-Zephyr-SPDX-Builder"
    agent.name = "Zephyr SPDX Builder"
    agent.creationInfo = f"{namespace}#CreationInfo"
    elements.append(agent)

    creation_info = spdx.CreationInfo()
    creation_info._id = f"{namespace}#CreationInfo"
    creation_info.created = datetime.now(timezone.utc)
    creation_info.createdBy = [agent._id]
    creation_info.specVersion = "3.0.1"
    elements.append(creation_info)

    return agent, creation_info


def _create_spdx_document(doc, creation_info, namespace, elements):
    """
    Create the SpdxDocument object.

    Arguments:
        doc: the internal document object
        creation_info: the creation info object
        namespace: document namespace
        elements: list to append new elements to
    Returns: tuple of (spdx_doc, lic_expr)
    """
    spdx_doc = spdx.SpdxDocument()
    spdx_doc._id = f"{namespace}/{doc.cfg.name}"
    spdx_doc.name = doc.cfg.name
    spdx_doc.creationInfo = creation_info._id

    lic_expr = spdx.simplelicensing_LicenseExpression()
    lic_expr._id = f"{namespace}#License-CC0-1.0"
    lic_expr.simplelicensing_licenseExpression = "CC0-1.0"
    lic_expr.creationInfo = creation_info._id
    elements.append(lic_expr)
    spdx_doc.dataLicense = lic_expr

    return spdx_doc, lic_expr


def _add_file(f_obj, pkg, spdx_pkg, doc, creation_info, license_cache,
              elements, spdx_doc, ext_doc_map, add_relationship_func):
    """
    Add a file element to the SPDX 3.0 document.

    Arguments:
        f_obj: the file object
        pkg: the package object
        spdx_pkg: the SPDX 3.0 package object
        doc: the document object
        creation_info: creation info object
        license_cache: cache for license expressions
        elements: list to append new elements to
        spdx_doc: the SPDX document object
        ext_doc_map: map of external document references
        add_relationship_func: function to add relationships
    """
    namespace = doc.cfg.namespace

    spdx_file = spdx.software_File()
    spdx_file._id = _get_full_uri(f_obj.spdxID, doc, ext_doc_map)
    spdx_file.name = f"./{f_obj.relpath}"
    spdx_file.creationInfo = creation_info._id
    spdx_file.software_copyrightText = f_obj.copyrightText

    _add_license_relationship(
        spdx_file._id, f_obj.concludedLicense,
        spdx.RelationshipType.hasConcludedLicense,
        license_cache, namespace, creation_info._id, elements, spdx_doc
    )

    if f_obj.sha1:
        checksum = spdx.Hash()
        checksum.algorithm = spdx.HashAlgorithm.sha1
        checksum.hashValue = f_obj.sha1
        spdx_file.verifiedUsing.append(checksum)
    if f_obj.sha256:
        checksum = spdx.Hash()
        checksum.algorithm = spdx.HashAlgorithm.sha256
        checksum.hashValue = f_obj.sha256
        spdx_file.verifiedUsing.append(checksum)
    if f_obj.md5:
        checksum = spdx.Hash()
        checksum.algorithm = spdx.HashAlgorithm.md5
        checksum.hashValue = f_obj.md5
        spdx_file.verifiedUsing.append(checksum)

    elements.append(spdx_file)
    spdx_doc.element.append(spdx_file._id)

    # Explicit CONTAINS relationship
    rel = spdx.Relationship()
    pkg_id_clean = pkg.cfg.spdxID.replace('SPDXRef-', '')
    file_id_clean = f_obj.spdxID.replace('SPDXRef-', '')
    rel._id = f"{namespace}#Rel-Contains-{pkg_id_clean}-{file_id_clean}"
    rel.creationInfo = creation_info._id
    rel.relationshipType = spdx.RelationshipType.contains
    rel.from_ = spdx_pkg._id
    rel.to = [spdx_file._id]
    elements.append(rel)
    spdx_doc.element.append(rel._id)

    for rln in f_obj.rlns:
        add_relationship_func(rln)


def _add_package(pkg, doc, creation_info, license_cache, elements, spdx_doc,
                 ext_doc_map, add_relationship_func):
    """
    Add a package element to the SPDX 3.0 document.

    Arguments:
        pkg: the package object
        doc: the document object
        creation_info: creation info object
        license_cache: cache for license expressions
        elements: list to append new elements to
        spdx_doc: the SPDX document object
        ext_doc_map: map of external document references
        add_relationship_func: function to add relationships
    """
    namespace = doc.cfg.namespace

    spdx_pkg = spdx.software_Package()
    spdx_pkg._id = _get_full_uri(pkg.cfg.spdxID, doc, ext_doc_map)
    spdx_pkg.name = pkg.cfg.name
    spdx_pkg.creationInfo = creation_info._id
    spdx_pkg.software_copyrightText = pkg.cfg.copyrightText

    _add_license_relationship(
        spdx_pkg._id, pkg.cfg.declaredLicense,
        spdx.RelationshipType.hasDeclaredLicense,
        license_cache, namespace, creation_info._id, elements, spdx_doc
    )
    _add_license_relationship(
        spdx_pkg._id, pkg.concludedLicense,
        spdx.RelationshipType.hasConcludedLicense,
        license_cache, namespace, creation_info._id, elements, spdx_doc
    )

    if pkg.cfg.version:
        spdx_pkg.software_packageVersion = pkg.cfg.version
    elif pkg.cfg.revision:
        spdx_pkg.software_packageVersion = pkg.cfg.revision

    if pkg.cfg.url:
        spdx_pkg.software_downloadLocation = generate_download_url(pkg.cfg.url, pkg.cfg.revision)
    else:
        spdx_pkg.software_downloadLocation = "NOASSERTION"

    if pkg.cfg.primaryPurpose:
        purpose = _map_purpose(pkg.cfg.primaryPurpose)
        if purpose:
            spdx_pkg.software_primaryPurpose = purpose

    for ref in pkg.cfg.externalReferences:
        if ref.startswith("cpe:"):
            ext_id = spdx.ExternalIdentifier()
            ext_id.externalIdentifierType = spdx.ExternalIdentifierType.cpe23
            ext_id.identifier = ref
            spdx_pkg.externalIdentifier.append(ext_id)
        elif ref.startswith("pkg:"):
            ext_id = spdx.ExternalIdentifier()
            ext_id.externalIdentifierType = spdx.ExternalIdentifierType.packageUrl
            ext_id.identifier = ref
            spdx_pkg.externalIdentifier.append(ext_id)

    elements.append(spdx_pkg)
    spdx_doc.element.append(spdx_pkg._id)
    spdx_doc.rootElement.append(spdx_pkg._id)

    # Files
    for f_obj in pkg.files.values():
        _add_file(f_obj, pkg, spdx_pkg, doc, creation_info, license_cache,
                  elements, spdx_doc, ext_doc_map, add_relationship_func)

    for rln in pkg.rlns:
        add_relationship_func(rln)


def write_spdx(spdx_path, doc):
    """
    Write an SPDX 3.0 document in JSON-LD format.

    Arguments:
        spdx_path: path to write SPDX document
        doc: SPDX Document object to write
    Returns: True on success, False on failure
    """
    if not spdx:
        log.err("Error: spdx-python-model not installed, cannot generate SPDX 3.0 document")
        return False

    log.inf(f"Writing SPDX 3.0 document {doc.cfg.name} to {spdx_path}")

    elements = []
    namespace = doc.cfg.namespace

    # Map for external document namespaces
    ext_doc_map = {ed.cfg.docRefID: ed.cfg.namespace for ed in doc.externalDocuments}

    # Creation Info & Tool
    agent, creation_info = _create_agent_and_creation_info(namespace, elements)

    # Document
    spdx_doc, _ = _create_spdx_document(doc, creation_info, namespace, elements)

    # License cache
    license_cache = LicenseCache(namespace, creation_info._id, elements, spdx_doc)

    # Helper to add relationship
    def add_relationship(rln):
        refA = rln.refA_spdxID if rln.refA_spdxID else normalize_spdx_name(str(rln.refA))
        refB = rln.refB_spdxID if rln.refB_spdxID else normalize_spdx_name(str(rln.refB))

        spdx_type, swap = _map_relationship_type(rln.rlnType)

        from_id = _get_full_uri(refA, doc, ext_doc_map)
        to_id = _get_full_uri(refB, doc, ext_doc_map)

        if swap:
            from_id, to_id = to_id, from_id

        # Create unique ID for relationship
        rel_hash = abs(hash(from_id + to_id + str(spdx_type)))
        rel_id = f"{namespace}#Rel-{rel_hash}"

        rel = spdx.Relationship()
        rel._id = rel_id
        rel.creationInfo = creation_info._id
        rel.relationshipType = spdx_type
        rel.from_ = from_id
        rel.to = [to_id]

        elements.append(rel)
        spdx_doc.element.append(rel._id)

    # Packages
    for pkg in doc.pkgs.values():
        _add_package(pkg, doc, creation_info, license_cache, elements, spdx_doc,
                     ext_doc_map, add_relationship)

    for rln in doc.relationships:
        add_relationship(rln)

    elements.append(spdx_doc)

    # Serialize
    elements_data = []
    for el in elements:
        encoder = spdx.JSONLDEncoder()
        state = spdx.EncodeState()
        el.encode(encoder, state)
        if encoder.data:
            elements_data.append(encoder.data)

    final_data = {
        "@context": "https://spdx.org/rdf/3.0.1/spdx-context.jsonld",
        "@graph": elements_data
    }

    try:
        with open(spdx_path, "w") as f:
            json.dump(final_data, f, indent=2)
    except OSError as e:
        log.err(f"Error: Unable to write to {spdx_path}: {str(e)}")
        return False

    return True
