# Copyright (c) 2020, 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import json
import re
from datetime import datetime, timezone

from west import log

from zspdx.util import getHashes
from zspdx.version import SPDX_VERSION_2_3, SPDX_VERSION_3_0

try:
    from spdx_python_model import v3_0_1 as spdx
except ImportError:
    spdx = None

CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"

def _normalize_spdx_name(name):
    # Replace "_" by "-" since it's not allowed in spdx ID
    return name.replace("_", "-")

# Output tag-value SPDX 2.3 content for the given Relationship object.
# Arguments:
#   1) f: file handle for SPDX document
#   2) rln: Relationship object being described
def writeRelationshipSPDX(f, rln):
    refA = rln.refA_spdxID if rln.refA_spdxID else _normalize_spdx_name(str(rln.refA))
    refB = rln.refB_spdxID if rln.refB_spdxID else _normalize_spdx_name(str(rln.refB))

    f.write(
        f"Relationship: {refA} {rln.rlnType} {refB}\n"
    )

# Output tag-value SPDX 2.3 content for the given File object.
# Arguments:
#   1) f: file handle for SPDX document
#   2) bf: File object being described
def writeFileSPDX(f, bf):
    spdx_normalize_spdx_id = _normalize_spdx_name(bf.spdxID)

    f.write(f"""FileName: ./{bf.relpath}
SPDXID: {spdx_normalize_spdx_id}
FileChecksum: SHA1: {bf.sha1}
""")
    if bf.sha256 != "":
        f.write(f"FileChecksum: SHA256: {bf.sha256}\n")
    if bf.md5 != "":
        f.write(f"FileChecksum: MD5: {bf.md5}\n")
    f.write(f"LicenseConcluded: {bf.concludedLicense}\n")
    if len(bf.licenseInfoInFile) == 0:
        f.write("LicenseInfoInFile: NONE\n")
    else:
        for licInfoInFile in bf.licenseInfoInFile:
            f.write(f"LicenseInfoInFile: {licInfoInFile}\n")
    f.write(f"FileCopyrightText: {bf.copyrightText}\n\n")

    # write file relationships
    if len(bf.rlns) > 0:
        for rln in bf.rlns:
            writeRelationshipSPDX(f, rln)
        f.write("\n")

def generateDowloadUrl(url, revision):
    # Only git is supported
    # walker.py only parse revision if it's from git repositiory
    if len(revision) == 0:
        return url

    return f'git+{url}@{revision}'

# Output tag-value SPDX content for the given Package object.
# Arguments:
#   1) f: file handle for SPDX document
#   2) pkg: Package object being described
#   3) spdx_version: SPDX specification version
def writePackageSPDX(f, pkg, spdx_version=SPDX_VERSION_2_3):
    #update package meta data based on provided CPE reference
    for ref in pkg.cfg.externalReferences:
        if re.fullmatch(CPE23TYPE_REGEX, ref):
            metadata = ref.split(':',6)
            #metadata should now be array like:
            #[cpe,2.3,a,arm,mbed_tls,3.5.1,*:*:*:*:*:*:*]
            pkg.cfg.supplier = metadata[3]
            pkg.cfg.name = metadata[4]
            pkg.cfg.version = metadata[5]

    spdx_normalized_name = _normalize_spdx_name(pkg.cfg.name)
    spdx_normalize_spdx_id = _normalize_spdx_name(pkg.cfg.spdxID)

    f.write(f"""##### Package: {spdx_normalized_name}

PackageName: {pkg.cfg.name}
SPDXID: {spdx_normalize_spdx_id}
PackageLicenseConcluded: {pkg.concludedLicense}
""")
    f.write(f"""PackageLicenseDeclared: {pkg.cfg.declaredLicense}
PackageCopyrightText: {pkg.cfg.copyrightText}
""")

    # PrimaryPackagePurpose is only available in SPDX 2.3 and later
    if spdx_version >= SPDX_VERSION_2_3 and pkg.cfg.primaryPurpose != "":
        f.write(f"PrimaryPackagePurpose: {pkg.cfg.primaryPurpose}\n")

    if len(pkg.cfg.url) > 0:
        downloadUrl = generateDowloadUrl(pkg.cfg.url, pkg.cfg.revision)
        f.write(f"PackageDownloadLocation: {downloadUrl}\n")
    else:
        f.write("PackageDownloadLocation: NOASSERTION\n")

    if len(pkg.cfg.version) > 0:
        f.write(f"PackageVersion: {pkg.cfg.version}\n")
    elif len(pkg.cfg.revision) > 0:
        f.write(f"PackageVersion: {pkg.cfg.revision}\n")

    if len(pkg.cfg.supplier) > 0:
        f.write(f"PackageSupplier: Organization: {pkg.cfg.supplier}\n")

    for ref in pkg.cfg.externalReferences:
        if re.fullmatch(CPE23TYPE_REGEX, ref):
            f.write(f"ExternalRef: SECURITY cpe23Type {ref}\n")
        elif re.fullmatch(PURL_REGEX, ref):
            f.write(f"ExternalRef: PACKAGE-MANAGER purl {ref}\n")
        else:
            log.wrn(f"Unknown external reference ({ref})")

    # flag whether files analyzed / any files present
    if len(pkg.files) > 0:
        if len(pkg.licenseInfoFromFiles) > 0:
            for licFromFiles in pkg.licenseInfoFromFiles:
                f.write(f"PackageLicenseInfoFromFiles: {licFromFiles}\n")
        else:
            f.write("PackageLicenseInfoFromFiles: NOASSERTION\n")
        f.write(f"FilesAnalyzed: true\nPackageVerificationCode: {pkg.verificationCode}\n\n")
    else:
        f.write("FilesAnalyzed: false\nPackageComment: Utility target; no files\n\n")

    # write package relationships
    if len(pkg.rlns) > 0:
        for rln in pkg.rlns:
            writeRelationshipSPDX(f, rln)
        f.write("\n")

    # write package files, if any
    if len(pkg.files) > 0:
        bfs = list(pkg.files.values())
        bfs.sort(key = lambda x: x.relpath)
        for bf in bfs:
            writeFileSPDX(f, bf)

# Output tag-value SPDX 2.3 content for a custom license.
# Arguments:
#   1) f: file handle for SPDX document
#   2) lic: custom license ID being described
def writeOtherLicenseSPDX(f, lic):
    f.write(f"""LicenseID: {lic}
ExtractedText: {lic}
LicenseName: {lic}
LicenseComment: Corresponds to the license ID `{lic}` detected in an SPDX-License-Identifier: tag.
""")

# Output tag-value SPDX content for the given Document object.
# Arguments:
#   1) f: file handle for SPDX document
#   2) doc: Document object being described
#   3) spdx_version: SPDX specification version
def writeDocumentSPDX(f, doc, spdx_version=SPDX_VERSION_2_3):
    spdx_normalized_name = _normalize_spdx_name(doc.cfg.name)

    f.write(f"""SPDXVersion: SPDX-{spdx_version}
DataLicense: CC0-1.0
SPDXID: SPDXRef-DOCUMENT
DocumentName: {spdx_normalized_name}
DocumentNamespace: {doc.cfg.namespace}
Creator: Tool: Zephyr SPDX builder
Created: {datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")}

""")

    # write any external document references
    if len(doc.externalDocuments) > 0:
        extDocs = list(doc.externalDocuments)
        extDocs.sort(key = lambda x: x.cfg.docRefID)
        for extDoc in extDocs:
            f.write(
                f"ExternalDocumentRef: {extDoc.cfg.docRefID} {extDoc.cfg.namespace} "
                f"SHA1: {extDoc.myDocSHA1}\n"
            )
        f.write("\n")

    # write relationships owned by this Document (not by its Packages, etc.), if any
    if len(doc.relationships) > 0:
        for rln in doc.relationships:
            writeRelationshipSPDX(f, rln)
        f.write("\n")

    # write packages
    for pkg in doc.pkgs.values():
        writePackageSPDX(f, pkg, spdx_version)

    # write other license info, if any
    if len(doc.customLicenseIDs) > 0:
        for lic in sorted(list(doc.customLicenseIDs)):
            writeOtherLicenseSPDX(f, lic)

# Open SPDX document file for writing, write the document, and calculate
# its hash for other referring documents to use.
# Arguments:
#   1) spdxPath: path to write SPDX document
#   2) doc: SPDX Document object to write
#   3) spdx_version: SPDX specification version
def writeSPDX(spdxPath, doc, spdx_version=SPDX_VERSION_2_3):
    if spdx_version >= SPDX_VERSION_3_0:
        return writeSPDX3(spdxPath, doc)

    # create and write document to disk
    try:
        log.inf(f"Writing SPDX {spdx_version} document {doc.cfg.name} to {spdxPath}")
        with open(spdxPath, "w") as f:
            writeDocumentSPDX(f, doc, spdx_version)
    except OSError as e:
        log.err(f"Error: Unable to write to {spdxPath}: {str(e)}")
        return False

    # calculate hash of the document we just wrote
    hashes = getHashes(spdxPath)
    if not hashes:
        log.err("Error: created document but unable to calculate hash values")
        return False
    doc.myDocSHA1 = hashes[0]

    return True

def writeSPDX3(spdxPath, doc):
    if not spdx:
        log.err("Error: spdx-python-model not installed, cannot generate SPDX 3.0 document")
        return False

    log.inf(f"Writing SPDX 3.0 document {doc.cfg.name} to {spdxPath}")

    elements = []

    # Map for external document namespaces
    ext_doc_map = {ed.cfg.docRefID: ed.cfg.namespace for ed in doc.externalDocuments}

    def get_full_uri(spdx_id):
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

    # Creation Info & Tool
    agent = spdx.Agent()
    agent._id = f"{doc.cfg.namespace}#Agent-Zephyr-SPDX-Builder"
    agent.name = "Zephyr SPDX Builder"
    agent.creationInfo = f"{doc.cfg.namespace}#CreationInfo"
    elements.append(agent)

    creation_info = spdx.CreationInfo()
    creation_info._id = f"{doc.cfg.namespace}#CreationInfo"
    creation_info.created = datetime.now(timezone.utc)
    creation_info.createdBy = [agent._id]
    creation_info.specVersion = "3.0.1"
    elements.append(creation_info)

    # Document
    spdx_doc = spdx.SpdxDocument()
    spdx_doc._id = get_full_uri("SPDXRef-DOCUMENT")
    spdx_doc.name = doc.cfg.name
    spdx_doc.creationInfo = creation_info._id

    lic_expr = spdx.simplelicensing_LicenseExpression()
    lic_expr._id = f"{doc.cfg.namespace}#License-CC0-1.0"
    lic_expr.simplelicensing_licenseExpression = "CC0-1.0"
    lic_expr.creationInfo = creation_info._id
    elements.append(lic_expr)
    spdx_doc.dataLicense = lic_expr

    license_cache = {}

    def get_license_id(license_str):
        if license_str in license_cache:
            return license_cache[license_str]

        # Create new license expression object
        # Sanitize license_str for ID
        safe_id = re.sub(r'[^a-zA-Z0-9.-]', '-', license_str)
        lic_id = f"{doc.cfg.namespace}#License-{safe_id}"

        lic = spdx.simplelicensing_LicenseExpression()
        lic._id = lic_id
        lic.simplelicensing_licenseExpression = license_str
        lic.creationInfo = creation_info._id

        elements.append(lic)
        spdx_doc.element.append(lic._id)

        license_cache[license_str] = lic._id
        return lic._id

    def add_license_relationship(element_id, license_str, relationship_type):
        if not license_str or license_str == "NOASSERTION" or license_str == "NONE":
            return

        lic_id = get_license_id(license_str)

        rel = spdx.Relationship()
        # Create deterministic ID
        rel_hash = abs(hash(element_id + lic_id + str(relationship_type)))
        rel._id = f"{doc.cfg.namespace}#Rel-License-{rel_hash}"
        rel.creationInfo = creation_info._id
        rel.relationshipType = relationship_type
        rel.from_ = element_id
        rel.to = [lic_id]

        elements.append(rel)
        spdx_doc.element.append(rel._id)

    # Helper to add relationship
    def add_relationship(rln):
        refA = rln.refA_spdxID if rln.refA_spdxID else _normalize_spdx_name(str(rln.refA))
        refB = rln.refB_spdxID if rln.refB_spdxID else _normalize_spdx_name(str(rln.refB))

        r_type = rln.rlnType
        spdx_type = None
        swap = False

        if r_type == "DESCRIBES":
            spdx_type = spdx.RelationshipType.describes
        elif r_type == "CONTAINS":
            spdx_type = spdx.RelationshipType.contains
        elif r_type == "HAS_PREREQUISITE":
            spdx_type = spdx.RelationshipType.hasPrerequisite
        elif r_type == "STATIC_LINK":
            spdx_type = spdx.RelationshipType.hasStaticLink
        elif r_type == "GENERATED_FROM":
            spdx_type = spdx.RelationshipType.generates
            swap = True
        else:
            spdx_type = spdx.RelationshipType.other

        from_id = get_full_uri(refA)
        to_id = get_full_uri(refB)

        if swap:
            from_id, to_id = to_id, from_id

        # Create unique ID for relationship
        rel_hash = abs(hash(from_id + to_id + str(spdx_type)))
        rel_id = f"{doc.cfg.namespace}#Rel-{rel_hash}"

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
        spdx_pkg = spdx.software_Package()
        spdx_pkg._id = get_full_uri(pkg.cfg.spdxID)
        spdx_pkg.name = pkg.cfg.name
        spdx_pkg.creationInfo = creation_info._id
        spdx_pkg.software_copyrightText = pkg.cfg.copyrightText

        add_license_relationship(spdx_pkg._id, pkg.cfg.declaredLicense, spdx.RelationshipType.hasDeclaredLicense)
        add_license_relationship(spdx_pkg._id, pkg.concludedLicense, spdx.RelationshipType.hasConcludedLicense)

        if pkg.cfg.version:
            spdx_pkg.software_packageVersion = pkg.cfg.version
        elif pkg.cfg.revision:
            spdx_pkg.software_packageVersion = pkg.cfg.revision

        if pkg.cfg.url:
            spdx_pkg.software_downloadLocation = generateDowloadUrl(pkg.cfg.url, pkg.cfg.revision)
        else:
            spdx_pkg.software_downloadLocation = "NOASSERTION"

        if pkg.cfg.primaryPurpose:
            purpose_map = {
                "APPLICATION": spdx.software_SoftwarePurpose.application,
                "LIBRARY": spdx.software_SoftwarePurpose.library,
                "SOURCE": spdx.software_SoftwarePurpose.source,
                "CONTAINER": spdx.software_SoftwarePurpose.container,
                "OPERATING-SYSTEM": spdx.software_SoftwarePurpose.operatingSystem,
                "FIRMWARE": spdx.software_SoftwarePurpose.firmware,
                "FILE": spdx.software_SoftwarePurpose.file
            }
            if pkg.cfg.primaryPurpose in purpose_map:
                spdx_pkg.software_primaryPurpose = purpose_map[pkg.cfg.primaryPurpose]

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
            spdx_file = spdx.software_File()
            spdx_file._id = get_full_uri(f_obj.spdxID)
            spdx_file.name = f"./{f_obj.relpath}"
            spdx_file.creationInfo = creation_info._id
            spdx_file.software_copyrightText = f_obj.copyrightText

            add_license_relationship(spdx_file._id, f_obj.concludedLicense, spdx.RelationshipType.hasConcludedLicense)

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
            rel._id = f"{doc.cfg.namespace}#Rel-Contains-{pkg_id_clean}-{file_id_clean}"
            rel.creationInfo = creation_info._id
            rel.relationshipType = spdx.RelationshipType.contains
            rel.from_ = spdx_pkg._id
            rel.to = [spdx_file._id]
            elements.append(rel)
            spdx_doc.element.append(rel._id)

            for rln in f_obj.rlns:
                add_relationship(rln)

        for rln in pkg.rlns:
            add_relationship(rln)

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
        with open(spdxPath, "w") as f:
            json.dump(final_data, f, indent=2)
    except OSError as e:
        log.err(f"Error: Unable to write to {spdxPath}: {str(e)}")
        return False

    return True
