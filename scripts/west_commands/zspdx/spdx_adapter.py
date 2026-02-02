# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
SPDX Adapter - Converts generic SBOM data structures to SPDX-specific format.
This module provides the bridge between format-agnostic walker output and
SPDX 2.x serialization requirements.
"""

from zspdx.datatypes import (
    Document,
    DocumentConfig,
    File,
    Package,
    PackageConfig,
    Relationship,
    RelationshipData,
    RelationshipDataElementType,
)
from zspdx.generic_datatypes import (
    GenericDocument,
    GenericFile,
    GenericPackage,
    GenericRelationship,
    GenericRelationshipData,
    GenericRelationshipDataElementType,
)
import zspdx.spdxids


class SPDXAdapter:
    """
    Adapter that converts generic SBOM data structures to SPDX format.
    This handles all SPDX-specific concerns like ID prefixing, namespace generation, etc.
    """

    def __init__(self, namespacePrefix=""):
        """
        Initialize the SPDX adapter.
        
        Args:
            namespacePrefix: Prefix for SPDX Document namespaces
        """
        self.namespacePrefix = namespacePrefix

    def _generate_spdx_id(self, genericID):
        """
        Convert a generic ID to an SPDX ID with proper prefix.
        
        Args:
            genericID: Generic identifier
            
        Returns:
            SPDX ID with SPDXRef- prefix
        """
        if genericID.startswith("SPDXRef-"):
            return genericID
        return f"SPDXRef-{zspdx.spdxids.convertToSPDXIDSafe(genericID)}"

    def _generate_doc_ref_id(self, genericRefID):
        """
        Convert a generic document reference ID to SPDX format.
        
        Args:
            genericRefID: Generic document reference ID
            
        Returns:
            SPDX document reference with DocumentRef- prefix
        """
        if genericRefID.startswith("DocumentRef-"):
            return genericRefID
        return f"DocumentRef-{genericRefID}"

    def convert_document_config(self, genericCfg):
        """
        Convert GenericDocumentConfig to SPDX DocumentConfig.
        
        Args:
            genericCfg: GenericDocumentConfig instance
            
        Returns:
            DocumentConfig instance with SPDX-specific fields
        """
        cfg = DocumentConfig()
        cfg.name = genericCfg.name
        cfg.namespace = f"{self.namespacePrefix}/{genericCfg.name}" if self.namespacePrefix else genericCfg.name
        cfg.docRefID = self._generate_doc_ref_id(genericCfg.docRefID)
        return cfg

    def convert_package_config(self, genericCfg):
        """
        Convert GenericPackageConfig to SPDX PackageConfig.
        
        Args:
            genericCfg: GenericPackageConfig instance
            
        Returns:
            PackageConfig instance with SPDX-specific fields
        """
        cfg = PackageConfig()
        cfg.name = genericCfg.name
        cfg.spdxID = self._generate_spdx_id(genericCfg.pkgID)
        cfg.primaryPurpose = genericCfg.primaryPurpose
        cfg.url = genericCfg.url
        cfg.version = genericCfg.version
        cfg.revision = genericCfg.revision
        cfg.supplier = genericCfg.supplier
        cfg.externalReferences = genericCfg.externalReferences.copy()
        cfg.declaredLicense = genericCfg.declaredLicense
        cfg.copyrightText = genericCfg.copyrightText
        cfg.relativeBaseDir = genericCfg.relativeBaseDir
        return cfg

    def convert_relationship_data_element_type(self, genericType):
        """
        Convert GenericRelationshipDataElementType to SPDX RelationshipDataElementType.
        
        Args:
            genericType: GenericRelationshipDataElementType value
            
        Returns:
            RelationshipDataElementType value
        """
        mapping = {
            GenericRelationshipDataElementType.UNKNOWN: RelationshipDataElementType.UNKNOWN,
            GenericRelationshipDataElementType.FILENAME: RelationshipDataElementType.FILENAME,
            GenericRelationshipDataElementType.TARGETNAME: RelationshipDataElementType.TARGETNAME,
            GenericRelationshipDataElementType.PACKAGEID: RelationshipDataElementType.PACKAGEID,
            GenericRelationshipDataElementType.DOCUMENT: RelationshipDataElementType.DOCUMENT,
        }
        return mapping.get(genericType, RelationshipDataElementType.UNKNOWN)

    def convert_relationship_data(self, genericRd, docMapping):
        """
        Convert GenericRelationshipData to SPDX RelationshipData.
        
        Args:
            genericRd: GenericRelationshipData instance
            docMapping: Dictionary mapping GenericDocument to Document
            
        Returns:
            RelationshipData instance
        """
        rd = RelationshipData()
        rd.ownerType = self.convert_relationship_data_element_type(genericRd.ownerType)
        rd.ownerFileAbspath = genericRd.ownerFileAbspath
        rd.ownerTargetName = genericRd.ownerTargetName
        rd.ownerDocument = docMapping.get(genericRd.ownerDocument)
        rd.otherType = self.convert_relationship_data_element_type(genericRd.otherType)
        rd.otherFileAbspath = genericRd.otherFileAbspath
        rd.otherTargetName = genericRd.otherTargetName
        rd.otherPackageID = self._generate_spdx_id(genericRd.otherPackageID) if genericRd.otherPackageID else ""
        rd.rlnType = genericRd.rlnType
        return rd

    def convert_relationship(self, genericRln):
        """
        Convert GenericRelationship to SPDX Relationship.
        
        Args:
            genericRln: GenericRelationship instance
            
        Returns:
            Relationship instance
        """
        rln = Relationship()
        rln.refA = genericRln.refA
        rln.refB = genericRln.refB
        rln.rlnType = genericRln.rlnType
        return rln

    def convert_document(self, genericDoc):
        """
        Convert GenericDocument to SPDX Document.
        
        Args:
            genericDoc: GenericDocument instance
            
        Returns:
            Document instance with SPDX-specific format
        """
        # Convert document config
        spdxCfg = self.convert_document_config(genericDoc.cfg)
        doc = Document(spdxCfg)

        # Copy over basic fields
        doc.timesSeen = genericDoc.timesSeen.copy()
        doc.myDocSHA1 = genericDoc.myDocHash
        doc.customLicenseIDs = genericDoc.customLicenseIDs if hasattr(genericDoc, 'customLicenseIDs') else set()

        # We'll convert packages, files, and relationships after setting up the mapping
        return doc

    def convert_package(self, genericPkg, spdxDoc):
        """
        Convert GenericPackage to SPDX Package.
        
        Args:
            genericPkg: GenericPackage instance
            spdxDoc: SPDX Document that owns this package
            
        Returns:
            Package instance
        """
        # Convert package config
        spdxCfg = self.convert_package_config(genericPkg.cfg)
        pkg = Package(spdxCfg, spdxDoc)

        # Copy over fields
        pkg.verificationCode = genericPkg.verificationCode
        pkg.concludedLicense = genericPkg.concludedLicense
        pkg.licenseInfoFromFiles = genericPkg.licenseInfoFromFiles.copy()

        # Files and relationships will be converted separately
        return pkg

    def convert_file(self, genericFile, spdxDoc, spdxPkg):
        """
        Convert GenericFile to SPDX File.
        
        Args:
            genericFile: GenericFile instance
            spdxDoc: SPDX Document that owns this file
            spdxPkg: SPDX Package that owns this file
            
        Returns:
            File instance
        """
        f = File(spdxDoc, spdxPkg)
        f.abspath = genericFile.abspath
        f.relpath = genericFile.relpath
        f.spdxID = self._generate_spdx_id(genericFile.fileID)
        f.sha1 = genericFile.sha1
        f.sha256 = genericFile.sha256
        f.md5 = genericFile.md5
        f.concludedLicense = genericFile.concludedLicense
        f.licenseInfoInFile = genericFile.licenseInfoInFile.copy()
        f.copyrightText = genericFile.copyrightText

        return f

    def convert_all_documents(self, genericDocs):
        """
        Convert a list of GenericDocuments to SPDX Documents with all their contents.
        
        Args:
            genericDocs: List of GenericDocument instances
            
        Returns:
            List of Document instances
        """
        # First pass: create all documents and build mapping
        docMapping = {}
        spdxDocs = []
        
        for genericDoc in genericDocs:
            spdxDoc = self.convert_document(genericDoc)
            docMapping[genericDoc] = spdxDoc
            spdxDocs.append(spdxDoc)

        # Second pass: convert packages and build mapping
        pkgMapping = {}
        for genericDoc, spdxDoc in docMapping.items():
            for genericPkgID, genericPkg in genericDoc.pkgs.items():
                spdxPkg = self.convert_package(genericPkg, spdxDoc)
                pkgMapping[genericPkg] = spdxPkg
                # Store in document with SPDX ID
                spdxDoc.pkgs[spdxPkg.cfg.spdxID] = spdxPkg

        # Third pass: convert files and build mapping
        fileMapping = {}
        for genericDoc, spdxDoc in docMapping.items():
            for genericPkg in genericDoc.pkgs.values():
                spdxPkg = pkgMapping[genericPkg]
                for genericFileID, genericFile in genericPkg.files.items():
                    spdxFile = self.convert_file(genericFile, spdxDoc, spdxPkg)
                    fileMapping[genericFile] = spdxFile
                    # Store in package with SPDX ID
                    spdxPkg.files[spdxFile.spdxID] = spdxFile
                    # Store in document file links
                    spdxDoc.fileLinks[spdxFile.abspath] = spdxFile

        # Fourth pass: convert relationships
        for genericDoc, spdxDoc in docMapping.items():
            # Convert document-level relationships
            for genericRln in genericDoc.relationships:
                spdxRln = self.convert_relationship(genericRln)
                spdxDoc.relationships.append(spdxRln)

            # Convert package-level relationships
            for genericPkg in genericDoc.pkgs.values():
                spdxPkg = pkgMapping[genericPkg]
                for genericRln in genericPkg.rlns:
                    spdxRln = self.convert_relationship(genericRln)
                    spdxPkg.rlns.append(spdxRln)
                # Link target build file if present
                if genericPkg.targetBuildFile:
                    spdxPkg.targetBuildFile = fileMapping.get(genericPkg.targetBuildFile)

            # Convert file-level relationships
            for genericPkg in genericDoc.pkgs.values():
                for genericFile in genericPkg.files.values():
                    spdxFile = fileMapping[genericFile]
                    for genericRln in genericFile.rlns:
                        spdxRln = self.convert_relationship(genericRln)
                        spdxFile.rlns.append(spdxRln)

            # Convert external document references
            for genericExtDoc in genericDoc.externalDocuments:
                if genericExtDoc in docMapping:
                    spdxDoc.externalDocuments.add(docMapping[genericExtDoc])

        return spdxDocs
