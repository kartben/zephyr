# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


# GenericDocumentConfig contains settings used to configure how the SBOM Document
# should be built. This is format-agnostic.
@dataclass(eq=True)
class GenericDocumentConfig:
    # name of document
    name: str = ""

    # identifier that other docs will use to refer to this one
    docRefID: str = ""


# GenericDocument contains the data assembled by the SBOM builder, to be used to
# create the actual SBOM Document. This is format-agnostic.
class GenericDocument:
    # initialize with a GenericDocumentConfig
    def __init__(self, cfg):
        super().__init__()

        # configuration - GenericDocumentConfig
        self.cfg = cfg

        # dict of ID => GenericPackage
        self.pkgs = {}

        # relationships "owned" by this Document, _not_ those "owned" by its
        # Packages or Files
        self.relationships = []

        # dict of filename (ignoring its directory) => number of times it has
        # been seen while adding files to this Document; used to calculate
        # unique IDs
        self.timesSeen = {}

        # dict of absolute path on disk => GenericFile
        self.fileLinks = {}

        # set of other Documents that our elements' Relationships refer to
        self.externalDocuments = set()

        # set of custom licenses to be declared
        self.customLicenseIDs = set()

        # this Document's hash, filled in _after_ the Document has been
        # written to disk, so that others can refer to it
        self.myDocHash = ""


# GenericPackageConfig contains settings used to configure how a Package should
# be built. This is format-agnostic.
@dataclass(eq=True)
class GenericPackageConfig:
    # package name
    name: str = ""

    # unique ID for this package
    pkgID: str = ""

    # primary package purpose (ex. "LIBRARY", "APPLICATION", etc.)
    primaryPurpose: str = ""

    # package URL
    url: str = ""

    # package version
    version: str = ""

    # package revision
    revision: str = ""

    # package supplier or vendor
    supplier: str = ""

    # package external references
    externalReferences: list = field(default_factory=list)

    # the Package's declared license
    declaredLicense: str = "NOASSERTION"

    # the Package's copyright text
    copyrightText: str = "NOASSERTION"

    # absolute path of the "root" directory on disk, to be used as the
    # base directory from which this Package's Files will calculate their
    # relative paths
    relativeBaseDir: str = ""


# GenericPackage contains the data assembled by the SBOM builder, to be used to
# create the actual Package. This is format-agnostic.
class GenericPackage:
    # initialize with:
    # 1) GenericPackageConfig
    # 2) the GenericDocument that owns this Package
    def __init__(self, cfg, doc):
        super().__init__()

        # configuration - GenericPackageConfig
        self.cfg = cfg

        # Document that owns this Package
        self.doc = doc

        # verification code, calculated for package integrity
        self.verificationCode = ""

        # concluded license for this Package
        self.concludedLicense = "NOASSERTION"

        # list of licenses found in this Package's Files
        self.licenseInfoFromFiles = []

        # Files in this Package
        # dict of ID => GenericFile
        self.files = {}

        # Relationships "owned" by this Package (e.g., this Package is left
        # side)
        self.rlns = []

        # If this Package was a target, which File was its main build product?
        self.targetBuildFile = None


# GenericRelationshipDataElementType defines whether a RelationshipData element
# (e.g., the "owner" or the "other" element) is a File, a target Package,
# a Package's ID (as other only, and only where owner type is DOCUMENT),
# or the document itself (as owner only).
class GenericRelationshipDataElementType(Enum):
    UNKNOWN = 0
    FILENAME = 1
    TARGETNAME = 2
    PACKAGEID = 3
    DOCUMENT = 4


# GenericRelationshipData contains the pre-analysis data about a relationship between
# Files and/or Packages/targets. It is eventually parsed into a corresponding
# GenericRelationship after we have organized the Package and File data.
@dataclass(eq=True)
class GenericRelationshipData:
    # for the "owner" element (e.g., the left side of the Relationship),
    # is it a filename or a target name (e.g., a Package in the build doc)
    ownerType: GenericRelationshipDataElementType = GenericRelationshipDataElementType.UNKNOWN

    # owner file absolute path (if ownerType is FILENAME)
    ownerFileAbspath: str = ""

    # owner target name (if ownerType is TARGETNAME)
    ownerTargetName: str = ""

    # owner Document (if ownerType is DOCUMENT)
    ownerDocument: Optional['GenericDocument'] = None

    # for the "other" element (e.g., the right side of the Relationship),
    # is it a filename or a target name (e.g., a Package in the build doc)
    otherType: GenericRelationshipDataElementType = GenericRelationshipDataElementType.UNKNOWN

    # other file absolute path (if otherType is FILENAME)
    otherFileAbspath: str = ""

    # other target name (if otherType is TARGETNAME)
    otherTargetName: str = ""

    # other package ID (if ownerType is DOCUMENT and otherType is PACKAGEID)
    otherPackageID: str = ""

    # text string with Relationship type
    rlnType: str = ""


# GenericRelationship contains the post-analysis, processed data about a relationship
# in a form suitable for creating the actual Relationship in a particular
# Document's context.
@dataclass(eq=True)
class GenericRelationship:
    # ID for left side of relationship
    refA: str = ""

    # ID for right side of relationship
    refB: str = ""

    # text string with Relationship type
    rlnType: str = ""


# GenericFile contains the data needed to create a File element in the context of a
# particular Document and Package. This is format-agnostic.
class GenericFile:
    # initialize with:
    # 1) GenericDocument containing this File
    # 2) GenericPackage containing this File
    def __init__(self, doc, pkg):
        super().__init__()

        # absolute path to this file on disk
        self.abspath = ""

        # relative path for this file, measured from the owning Package's
        # cfg.relativeBaseDir
        self.relpath = ""

        # unique ID for this file
        self.fileID = ""

        # SHA1 hash
        self.sha1 = ""

        # SHA256 hash, if needed; empty string otherwise
        self.sha256 = ""

        # MD5 hash, if needed; empty string otherwise
        self.md5 = ""

        # concluded license
        self.concludedLicense = "NOASSERTION"

        # license info in file
        self.licenseInfoInFile = []

        # copyright text
        self.copyrightText = "NOASSERTION"

        # Relationships "owned" by this File (e.g., this File is left side)
        self.rlns = []

        # Package that owns this File
        self.pkg = pkg

        # Document that owns this File
        self.doc = doc
