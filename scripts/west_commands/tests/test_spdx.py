# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for the zspdx SPDX SBOM generation modules."""

import hashlib
import io
import os
import textwrap
from unittest.mock import MagicMock, patch

import pytest

from zspdx.cmakecache import parseCMakeCacheFile
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
from zspdx.getincludes import extractIncludes
from zspdx.scanner import (
    calculateVerificationCode,
    checkLicenseValid,
    getPackageLicenses,
    normalizeExpression,
    parseLineForExpression,
    splitExpression,
)
from zspdx.spdxids import convertToSPDXIDSafe, getSPDXIDSafeCharacter, getUniqueFileID
from zspdx.version import SPDX_VERSION_2_2, SPDX_VERSION_2_3, parse
from zspdx.writer import (
    _normalize_spdx_name,
    generateDowloadUrl,
    writeDocumentSPDX,
    writeFileSPDX,
    writeOtherLicenseSPDX,
    writePackageSPDX,
    writeRelationshipSPDX,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_doc(name="test-doc", namespace="http://example.com/test"):
    cfg = DocumentConfig(name=name, namespace=namespace, docRefID=f"DocumentRef-{name}")
    return Document(cfg)


def make_pkg(doc, name="test-pkg", spdx_id="SPDXRef-test-pkg"):
    cfg = PackageConfig(name=name, spdxID=spdx_id)
    pkg = Package(cfg, doc)
    doc.pkgs[spdx_id] = pkg
    return pkg


def make_file(doc, pkg, relpath="src/main.c", sha1="abc123"):
    f = File(doc, pkg)
    f.relpath = relpath
    f.spdxID = "SPDXRef-File-main.c"
    f.sha1 = sha1
    f.sha256 = ""
    f.md5 = ""
    f.concludedLicense = "Apache-2.0"
    f.licenseInfoInFile = ["Apache-2.0"]
    f.copyrightText = "NOASSERTION"
    pkg.files[f.spdxID] = f
    return f


# ===========================================================================
# spdxids.py
# ===========================================================================

class TestSPDXIDSafeCharacter:
    def test_letter(self):
        assert getSPDXIDSafeCharacter("a") == "a"

    def test_digit(self):
        assert getSPDXIDSafeCharacter("5") == "5"

    def test_dash(self):
        assert getSPDXIDSafeCharacter("-") == "-"

    def test_dot(self):
        assert getSPDXIDSafeCharacter(".") == "."

    def test_unsafe_replaced(self):
        for c in "_/\\@#$% ":
            assert getSPDXIDSafeCharacter(c) == "-"


class TestConvertToSPDXIDSafe:
    def test_basic(self):
        assert convertToSPDXIDSafe("hello") == "hello"

    def test_underscores_and_slashes(self):
        assert convertToSPDXIDSafe("my_file/path.c") == "my-file-path.c"

    def test_empty(self):
        assert convertToSPDXIDSafe("") == ""


class TestGetUniqueFileID:
    def test_first_occurrence(self):
        seen = {}
        result = getUniqueFileID("main.c", seen)
        assert result == "SPDXRef-File-main.c"
        assert seen["main.c"] == 1

    def test_second_occurrence_gets_suffix(self):
        seen = {}
        getUniqueFileID("main.c", seen)
        result = getUniqueFileID("main.c", seen)
        assert result == "SPDXRef-File-main.c-2"
        assert seen["main.c"] == 2

    def test_filename_ending_with_number_gets_dash_1(self):
        """Filenames ending in '-<number>' get '-1' to avoid collisions."""
        seen = {}
        result = getUniqueFileID("file-3", seen)
        assert result == "SPDXRef-File-file-3-1"

    def test_unsafe_chars_converted(self):
        seen = {}
        result = getUniqueFileID("my file.c", seen)
        assert result == "SPDXRef-File-my-file.c"


# ===========================================================================
# version.py
# ===========================================================================

class TestVersion:
    def test_parse_2_2(self):
        assert parse("2.2") == SPDX_VERSION_2_2

    def test_parse_2_3(self):
        assert parse("2.3") == SPDX_VERSION_2_3

    def test_parse_unsupported(self):
        with pytest.raises(ValueError, match="Unsupported SPDX version"):
            parse("3.0")


# ===========================================================================
# scanner.py
# ===========================================================================

class TestParseLineForExpression:
    def test_found(self):
        assert parseLineForExpression("// SPDX-License-Identifier: Apache-2.0") == "Apache-2.0"

    def test_with_trailing_comment(self):
        assert parseLineForExpression("/* SPDX-License-Identifier: MIT */") == "MIT"

    def test_not_found(self):
        assert parseLineForExpression("// just a comment") is None

    def test_empty(self):
        assert parseLineForExpression("") is None


class TestSplitExpression:
    def test_single(self):
        assert splitExpression("Apache-2.0") == ["Apache-2.0"]

    def test_and(self):
        assert splitExpression("Apache-2.0 AND MIT") == ["Apache-2.0", "MIT"]

    def test_or(self):
        assert splitExpression("Apache-2.0 OR MIT") == ["Apache-2.0", "MIT"]

    def test_with_parens_and_plus(self):
        result = splitExpression("(GPL-2.0+ OR MIT)")
        assert "GPL-2.0" in result
        assert "MIT" in result

    def test_case_insensitive_operators(self):
        assert splitExpression("A and B") == ["A", "B"]

    def test_with(self):
        result = splitExpression("Apache-2.0 WITH LLVM-exception")
        assert sorted(result) == ["Apache-2.0", "LLVM-exception"]


class TestCalculateVerificationCode:
    def test_basic(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        f1 = make_file(doc, pkg, sha1="da39a3ee5e6b4b0d3255bfef95601890afd80709")
        code = calculateVerificationCode(pkg)
        # verification code = sha1 of sorted concatenated sha1s
        expected = hashlib.sha1(
            "da39a3ee5e6b4b0d3255bfef95601890afd80709".encode("utf-8"),
            usedforsecurity=False,
        ).hexdigest()
        assert code == expected

    def test_multiple_files(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        make_file(doc, pkg, relpath="a.c", sha1="bbbb")
        f2 = File(doc, pkg)
        f2.sha1 = "aaaa"
        f2.spdxID = "SPDXRef-File-b.c"
        pkg.files[f2.spdxID] = f2
        code = calculateVerificationCode(pkg)
        # sorted: aaaa, bbbb => sha1("aaaabbbb")
        expected = hashlib.sha1(b"aaaabbbb", usedforsecurity=False).hexdigest()
        assert code == expected


class TestCheckLicenseValid:
    def test_known_license(self):
        doc = make_doc()
        checkLicenseValid("Apache-2.0", doc)
        assert len(doc.customLicenseIDs) == 0

    def test_custom_license(self):
        doc = make_doc()
        checkLicenseValid("LicenseRef-my-custom", doc)
        assert "LicenseRef-my-custom" in doc.customLicenseIDs


class TestGetPackageLicenses:
    def test_basic(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        f = make_file(doc, pkg)
        f.concludedLicense = "Apache-2.0"
        f.licenseInfoInFile = ["Apache-2.0"]
        concluded, from_files = getPackageLicenses(pkg)
        assert concluded == ["Apache-2.0"]
        assert from_files == ["Apache-2.0"]

    def test_multiple_licenses(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        f1 = make_file(doc, pkg)
        f1.concludedLicense = "MIT"
        f1.licenseInfoInFile = ["MIT"]
        f2 = File(doc, pkg)
        f2.concludedLicense = "Apache-2.0"
        f2.licenseInfoInFile = ["Apache-2.0"]
        f2.spdxID = "SPDXRef-File-other.c"
        pkg.files[f2.spdxID] = f2
        concluded, from_files = getPackageLicenses(pkg)
        assert concluded == ["Apache-2.0", "MIT"]
        assert from_files == ["Apache-2.0", "MIT"]


class TestNormalizeExpression:
    def test_empty(self):
        assert normalizeExpression([]) == "NOASSERTION"

    def test_single(self):
        assert normalizeExpression(["MIT"]) == "MIT"

    def test_multiple_simple(self):
        assert normalizeExpression(["MIT", "Apache-2.0"]) == "MIT AND Apache-2.0"

    def test_complex_gets_parens(self):
        result = normalizeExpression(["MIT OR BSD-2-Clause", "Apache-2.0"])
        assert result == "(MIT OR BSD-2-Clause) AND Apache-2.0"

    def test_noassertion_filtered(self):
        assert normalizeExpression(["MIT", "NOASSERTION"]) == "MIT"

    def test_none_filtered(self):
        assert normalizeExpression(["MIT", "NONE"]) == "MIT"


# ===========================================================================
# writer.py
# ===========================================================================

class TestNormalizeSpdxName:
    def test_underscore_replaced(self):
        assert _normalize_spdx_name("my_package") == "my-package"

    def test_no_change(self):
        assert _normalize_spdx_name("my-package") == "my-package"


class TestGenerateDownloadUrl:
    def test_with_revision(self):
        assert generateDowloadUrl("https://github.com/foo/bar", "abc123") == \
            "git+https://github.com/foo/bar@abc123"

    def test_without_revision(self):
        assert generateDowloadUrl("https://github.com/foo/bar", "") == \
            "https://github.com/foo/bar"


class TestWriteRelationshipSPDX:
    def test_basic(self):
        rln = Relationship(refA="SPDXRef-A", refB="SPDXRef-B", rlnType="DESCRIBES")
        buf = io.StringIO()
        writeRelationshipSPDX(buf, rln)
        assert buf.getvalue() == "Relationship: SPDXRef-A DESCRIBES SPDXRef-B\n"

    def test_underscore_normalization(self):
        rln = Relationship(refA="SPDXRef-my_pkg", refB="SPDXRef-other_pkg", rlnType="DEPENDS_ON")
        buf = io.StringIO()
        writeRelationshipSPDX(buf, rln)
        assert "SPDXRef-my-pkg" in buf.getvalue()
        assert "SPDXRef-other-pkg" in buf.getvalue()


class TestWriteFileSPDX:
    def test_basic_output(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        f = make_file(doc, pkg)
        buf = io.StringIO()
        writeFileSPDX(buf, f)
        output = buf.getvalue()
        assert "FileName: ./src/main.c" in output
        assert "SPDXID: SPDXRef-File-main.c" in output
        assert "FileChecksum: SHA1: abc123" in output
        assert "LicenseConcluded: Apache-2.0" in output
        assert "LicenseInfoInFile: Apache-2.0" in output

    def test_no_license_info(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        f = make_file(doc, pkg)
        f.licenseInfoInFile = []
        buf = io.StringIO()
        writeFileSPDX(buf, f)
        assert "LicenseInfoInFile: NONE" in buf.getvalue()

    def test_sha256_included(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        f = make_file(doc, pkg)
        f.sha256 = "deadbeef"
        buf = io.StringIO()
        writeFileSPDX(buf, f)
        assert "FileChecksum: SHA256: deadbeef" in buf.getvalue()


class TestWritePackageSPDX:
    def test_basic_no_files(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        buf = io.StringIO()
        writePackageSPDX(buf, pkg)
        output = buf.getvalue()
        assert "PackageName: test-pkg" in output
        assert "SPDXID: SPDXRef-test-pkg" in output
        assert "FilesAnalyzed: false" in output

    def test_with_files(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        make_file(doc, pkg)
        pkg.verificationCode = "abc"
        buf = io.StringIO()
        writePackageSPDX(buf, pkg)
        output = buf.getvalue()
        assert "FilesAnalyzed: true" in output
        assert "PackageVerificationCode: abc" in output

    def test_primary_purpose_spdx_2_3(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        pkg.cfg.primaryPurpose = "APPLICATION"
        buf = io.StringIO()
        writePackageSPDX(buf, pkg, spdx_version=SPDX_VERSION_2_3)
        assert "PrimaryPackagePurpose: APPLICATION" in buf.getvalue()

    def test_primary_purpose_omitted_spdx_2_2(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        pkg.cfg.primaryPurpose = "APPLICATION"
        buf = io.StringIO()
        writePackageSPDX(buf, pkg, spdx_version=SPDX_VERSION_2_2)
        assert "PrimaryPackagePurpose" not in buf.getvalue()

    def test_download_location_with_url(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        pkg.cfg.url = "https://github.com/foo/bar"
        pkg.cfg.revision = "abc123"
        buf = io.StringIO()
        writePackageSPDX(buf, pkg)
        assert "PackageDownloadLocation: git+https://github.com/foo/bar@abc123" in buf.getvalue()

    def test_download_location_noassertion(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        buf = io.StringIO()
        writePackageSPDX(buf, pkg)
        assert "PackageDownloadLocation: NOASSERTION" in buf.getvalue()

    def test_cpe_external_ref(self):
        doc = make_doc()
        cfg = PackageConfig(
            name="mbed_tls", spdxID="SPDXRef-mbed-tls",
            externalReferences=["cpe:2.3:a:arm:mbed_tls:3.5.1:*:*:*:*:*:*:*"],
        )
        pkg = Package(cfg, doc)
        doc.pkgs[cfg.spdxID] = pkg
        buf = io.StringIO()
        writePackageSPDX(buf, pkg)
        output = buf.getvalue()
        assert "ExternalRef: SECURITY cpe23Type" in output
        # CPE parsing updates the package metadata
        assert "PackageSupplier: Organization: arm" in output

    def test_purl_external_ref(self):
        doc = make_doc()
        cfg = PackageConfig(
            name="test", spdxID="SPDXRef-test",
            externalReferences=["pkg:github/zephyrproject-rtos/zephyr@v3.5.0"],
        )
        pkg = Package(cfg, doc)
        doc.pkgs[cfg.spdxID] = pkg
        buf = io.StringIO()
        writePackageSPDX(buf, pkg)
        assert "ExternalRef: PACKAGE-MANAGER purl" in buf.getvalue()

    def test_supplier(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        pkg.cfg.supplier = "Zephyr Project"
        buf = io.StringIO()
        writePackageSPDX(buf, pkg)
        assert "PackageSupplier: Organization: Zephyr Project" in buf.getvalue()


class TestWriteDocumentSPDX:
    def test_header(self):
        doc = make_doc()
        buf = io.StringIO()
        writeDocumentSPDX(buf, doc)
        output = buf.getvalue()
        assert "SPDXVersion: SPDX-2.3" in output
        assert "DataLicense: CC0-1.0" in output
        assert "SPDXID: SPDXRef-DOCUMENT" in output
        assert "DocumentName: test-doc" in output
        assert "DocumentNamespace: http://example.com/test" in output
        assert "Creator: Tool: Zephyr SPDX builder" in output

    def test_version_2_2(self):
        doc = make_doc()
        buf = io.StringIO()
        writeDocumentSPDX(buf, doc, spdx_version=SPDX_VERSION_2_2)
        assert "SPDXVersion: SPDX-2.2" in buf.getvalue()

    def test_external_documents(self):
        doc = make_doc()
        ext = make_doc(name="ext-doc", namespace="http://example.com/ext")
        ext.myDocSHA1 = "deadbeef"
        doc.externalDocuments.add(ext)
        buf = io.StringIO()
        writeDocumentSPDX(buf, doc)
        output = buf.getvalue()
        assert "ExternalDocumentRef:" in output
        assert "SHA1: deadbeef" in output

    def test_custom_licenses(self):
        doc = make_doc()
        doc.customLicenseIDs.add("LicenseRef-custom")
        buf = io.StringIO()
        writeDocumentSPDX(buf, doc)
        output = buf.getvalue()
        assert "LicenseID: LicenseRef-custom" in output


class TestWriteOtherLicenseSPDX:
    def test_basic(self):
        buf = io.StringIO()
        writeOtherLicenseSPDX(buf, "LicenseRef-my-lic")
        output = buf.getvalue()
        assert "LicenseID: LicenseRef-my-lic" in output
        assert "ExtractedText: LicenseRef-my-lic" in output
        assert "LicenseName: LicenseRef-my-lic" in output


# ===========================================================================
# cmakecache.py
# ===========================================================================

class TestParseCMakeCacheFile:
    def test_parse(self, tmp_path):
        cache = tmp_path / "CMakeCache.txt"
        cache.write_text(textwrap.dedent("""\
            # This is a comment
            //Another comment
            CMAKE_C_COMPILER:FILEPATH=/usr/bin/gcc
            ZEPHYR_SDK_INSTALL_DIR:PATH=/opt/zephyr-sdk
            EMPTY_LINE_FOLLOWS:STRING=value

            ANOTHER_KEY:BOOL=ON
        """))
        with patch("zspdx.cmakecache.log"):
            result = parseCMakeCacheFile(str(cache))
        assert result["CMAKE_C_COMPILER"] == "/usr/bin/gcc"
        assert result["ZEPHYR_SDK_INSTALL_DIR"] == "/opt/zephyr-sdk"
        assert result["ANOTHER_KEY"] == "ON"

    def test_missing_file(self, tmp_path):
        with patch("zspdx.cmakecache.log"):
            result = parseCMakeCacheFile(str(tmp_path / "nonexistent"))
        assert result == {}


# ===========================================================================
# getincludes.py
# ===========================================================================

class TestExtractIncludes:
    def test_basic(self):
        resp = textwrap.dedent("""\
            . /path/to/kernel.h
            .. /path/to/types.h
            ... /path/to/stdint.h
            Multiple include guards may be useful for:
            /path/to/kernel.h
        """)
        result = extractIncludes(resp)
        assert "/path/to/kernel.h" in result
        assert "/path/to/types.h" in result
        assert "/path/to/stdint.h" in result
        assert len(result) == 3

    def test_deduplication(self):
        resp = ". /same/file.h\n. /same/file.h\n"
        result = extractIncludes(resp)
        assert result == ["/same/file.h"]

    def test_empty(self):
        assert extractIncludes("") == []


# ===========================================================================
# datatypes.py
# ===========================================================================

class TestDataTypes:
    def test_document_init(self):
        doc = make_doc()
        assert doc.cfg.name == "test-doc"
        assert doc.pkgs == {}
        assert doc.relationships == []
        assert doc.timesSeen == {}

    def test_package_init(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        assert pkg.cfg.name == "test-pkg"
        assert pkg.doc is doc
        assert pkg.concludedLicense == "NOASSERTION"

    def test_file_init(self):
        doc = make_doc()
        pkg = make_pkg(doc)
        f = File(doc, pkg)
        assert f.concludedLicense == "NOASSERTION"
        assert f.copyrightText == "NOASSERTION"
        assert f.pkg is pkg
        assert f.doc is doc

    def test_relationship_data(self):
        rd = RelationshipData()
        assert rd.ownerType == RelationshipDataElementType.UNKNOWN
        assert rd.otherType == RelationshipDataElementType.UNKNOWN
        assert rd.rlnType == ""

    def test_relationship(self):
        rln = Relationship(refA="SPDXRef-A", refB="SPDXRef-B", rlnType="DESCRIBES")
        assert rln.refA == "SPDXRef-A"
        assert rln.refB == "SPDXRef-B"


# ===========================================================================
# walker.py - modules-deps document
# ===========================================================================

class TestSetupModulesDocument:
    """Test Walker.setupModulesDocument() which produces the modules-deps.spdx."""

    def _make_walker(self):
        from zspdx.walker import Walker, WalkerConfig
        cfg = WalkerConfig(namespacePrefix="http://example.com")
        w = Walker(cfg)
        return w

    def test_modules_deps_packages_created(self):
        w = self._make_walker()
        modules = [
            {"name": "hal_stm32", "security": {"external-references": [
                "cpe:2.3:a:st:stm32:1.0.0:*:*:*:*:*:*:*",
            ]}},
            {"name": "mbedtls", "security": {"external-references": [
                "pkg:github/Mbed-TLS/mbedtls@v3.5.0",
            ]}},
        ]
        with patch("zspdx.walker.log"):
            w.setupModulesDocument(modules)

        doc = w.docModulesExtRefs
        assert doc is not None
        assert doc.cfg.name == "modules-deps"
        assert "SPDXRef-hal_stm32-deps" in doc.pkgs
        assert "SPDXRef-mbedtls-deps" in doc.pkgs

    def test_module_ext_refs_stored(self):
        w = self._make_walker()
        cpe = "cpe:2.3:a:st:stm32:1.0.0:*:*:*:*:*:*:*"
        modules = [{"name": "hal_stm32", "security": {"external-references": [cpe]}}]
        with patch("zspdx.walker.log"):
            w.setupModulesDocument(modules)
        pkg = w.docModulesExtRefs.pkgs["SPDXRef-hal_stm32-deps"]
        assert cpe in pkg.cfg.externalReferences

    def test_module_without_security(self):
        w = self._make_walker()
        modules = [{"name": "simple_module"}]
        with patch("zspdx.walker.log"):
            w.setupModulesDocument(modules)
        pkg = w.docModulesExtRefs.pkgs["SPDXRef-simple_module-deps"]
        assert pkg.cfg.externalReferences == []

    def test_module_without_name_returns_false(self):
        w = self._make_walker()
        modules = [{"path": "/some/path"}]
        with patch("zspdx.walker.log"):
            result = w.setupModulesDocument(modules)
        assert result is False

    def test_describe_relationships_queued(self):
        w = self._make_walker()
        modules = [
            {"name": "mod_a", "security": {"external-references": []}},
            {"name": "mod_b", "security": {"external-references": []}},
        ]
        with patch("zspdx.walker.log"):
            w.setupModulesDocument(modules)
        # Each module should add a DESCRIBES relationship
        describes = [r for r in w.pendingRelationships if r.rlnType == "DESCRIBES"]
        assert len(describes) == 2

    def test_modules_deps_written(self, tmp_path):
        """End-to-end: set up modules doc, scan it, write it."""
        w = self._make_walker()
        modules = [
            {"name": "hal_nordic", "security": {"external-references": [
                "cpe:2.3:a:nordic:hal:2.0.0:*:*:*:*:*:*:*",
            ]}},
        ]
        with patch("zspdx.walker.log"):
            w.setupModulesDocument(modules)

        # Write the document
        from zspdx.writer import writeSPDX
        out = tmp_path / "modules-deps.spdx"
        with patch("zspdx.writer.log"), patch("zspdx.util.log"):
            result = writeSPDX(str(out), w.docModulesExtRefs)
        assert result is True
        content = out.read_text()
        assert "modules-deps" in content
        assert "hal-nordic-deps" in content
        assert "ExternalRef: SECURITY cpe23Type" in content


# ===========================================================================
# walker.py - SDK document
# ===========================================================================

class TestSetupSDKDocument:
    """Test Walker.setupSDKDocument() which produces the sdk.spdx."""

    def _make_walker(self, sdk_path="/opt/zephyr-sdk"):
        from zspdx.walker import Walker, WalkerConfig
        cfg = WalkerConfig(
            namespacePrefix="http://example.com",
            includeSDK=True,
        )
        w = Walker(cfg)
        w.sdkPath = sdk_path
        return w

    def test_sdk_document_created(self):
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupSDKDocument()
        assert w.docSDK is not None
        assert w.docSDK.cfg.name == "sdk"
        assert w.docSDK.cfg.namespace == "http://example.com/sdk"

    def test_sdk_package_created(self):
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupSDKDocument()
        assert "SPDXRef-sdk" in w.docSDK.pkgs
        pkg = w.docSDK.pkgs["SPDXRef-sdk"]
        assert pkg.cfg.name == "sdk"
        assert pkg.cfg.relativeBaseDir == "/opt/zephyr-sdk"

    def test_sdk_describes_relationship(self):
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupSDKDocument()
        describes = [r for r in w.pendingRelationships if r.rlnType == "DESCRIBES"]
        assert len(describes) == 1
        assert describes[0].otherPackageID == "SPDXRef-sdk"

    def test_sdk_written(self, tmp_path):
        """Write SDK document and verify output."""
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupSDKDocument()

        from zspdx.writer import writeSPDX
        out = tmp_path / "sdk.spdx"
        with patch("zspdx.writer.log"), patch("zspdx.util.log"):
            result = writeSPDX(str(out), w.docSDK)
        assert result is True
        content = out.read_text()
        assert "DocumentName: sdk" in content
        assert "PackageName: sdk" in content
        assert "SPDXID: SPDXRef-sdk" in content


# ===========================================================================
# walker.py - app document
# ===========================================================================

class TestSetupAppDocument:
    def _make_walker(self):
        from zspdx.walker import Walker, WalkerConfig
        cfg = WalkerConfig(namespacePrefix="http://example.com")
        w = Walker(cfg)
        # Minimal codemodel mock
        w.cm = MagicMock()
        w.cm.paths_source = "/home/user/app"
        return w

    def test_app_document_created(self):
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupAppDocument()
        assert w.docApp is not None
        assert w.docApp.cfg.name == "app-sources"
        assert "SPDXRef-app-sources" in w.docApp.pkgs

    def test_app_package_purpose(self):
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupAppDocument()
        pkg = w.docApp.pkgs["SPDXRef-app-sources"]
        assert pkg.cfg.primaryPurpose == "SOURCE"


# ===========================================================================
# walker.py - build document
# ===========================================================================

class TestSetupBuildDocument:
    def _make_walker(self):
        from zspdx.walker import Walker, WalkerConfig
        cfg = WalkerConfig(namespacePrefix="http://example.com")
        w = Walker(cfg)
        return w

    def test_build_document_created(self):
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupBuildDocument()
        assert w.docBuild is not None
        assert w.docBuild.cfg.name == "build"
        # Build doc starts with no packages (created in walkTargets)
        assert len(w.docBuild.pkgs) == 0

    def test_zephyr_final_describes(self):
        w = self._make_walker()
        with patch("zspdx.walker.log"):
            w.setupBuildDocument()
        describes = [r for r in w.pendingRelationships
                     if r.rlnType == "DESCRIBES" and r.otherTargetName == "zephyr_final"]
        assert len(describes) == 1


# ===========================================================================
# walker.py - zephyr document
# ===========================================================================

class TestSetupZephyrDocument:
    def _make_walker(self):
        from zspdx.walker import Walker, WalkerConfig
        cfg = WalkerConfig(namespacePrefix="http://example.com")
        w = Walker(cfg)
        w.cm = MagicMock()
        w.cm.paths_source = "/home/user/zephyr"
        return w

    def test_zephyr_document_with_modules(self):
        w = self._make_walker()
        zephyr = {
            "remote": "https://github.com/zephyrproject-rtos/zephyr",
            "revision": "abc123",
            "tags": ["v3.5.0"],
        }
        modules = [
            {"name": "hal_stm32", "path": "/home/user/modules/hal/stm32",
             "remote": "https://github.com/zephyrproject-rtos/hal_stm32",
             "revision": "def456"},
        ]
        with patch("zspdx.walker.log"), patch("zspdx.walker.west_topdir", return_value="/home/user"):
            result = w.setupZephyrDocument(zephyr, modules)
        assert result is True
        assert w.docZephyr is not None
        # zephyr-sources + one module
        assert len(w.docZephyr.pkgs) == 2
        # Check zephyr package has CPE
        zephyr_pkg = w.docZephyr.pkgs["SPDXRef-zephyr-sources"]
        cpe_refs = [r for r in zephyr_pkg.cfg.externalReferences if r.startswith("cpe:")]
        assert len(cpe_refs) == 1
        assert "3.5.0" in cpe_refs[0]

    def test_module_url_and_revision(self):
        w = self._make_walker()
        zephyr = {"remote": "", "revision": "", "tags": []}
        modules = [
            {"name": "mymod", "path": "/tmp/mymod",
             "remote": "https://github.com/example/mymod", "revision": "aaa"},
        ]
        with patch("zspdx.walker.log"), patch("zspdx.walker.west_topdir", return_value="/tmp"):
            w.setupZephyrDocument(zephyr, modules)
        mod_pkg = w.docZephyr.pkgs["SPDXRef-mymod-sources"]
        assert mod_pkg.cfg.url == "https://github.com/example/mymod"
        assert mod_pkg.cfg.revision == "aaa"


# ===========================================================================
# sbom.py - setupCmakeQuery
# ===========================================================================

class TestSetupCmakeQuery:
    def test_creates_directory_and_file(self, tmp_path):
        from zspdx.sbom import setupCmakeQuery
        with patch("zspdx.sbom.log"):
            result = setupCmakeQuery(str(tmp_path))
        assert result is True
        query_file = tmp_path / ".cmake" / "api" / "v1" / "query" / "codemodel-v2"
        assert query_file.exists()

    def test_idempotent(self, tmp_path):
        from zspdx.sbom import setupCmakeQuery
        with patch("zspdx.sbom.log"):
            setupCmakeQuery(str(tmp_path))
            result = setupCmakeQuery(str(tmp_path))
        assert result is True
