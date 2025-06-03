# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import json
from datetime import datetime, timezone

from west import log

try:
    from spdx_python_model import v3_0_1 as spdx

    SPDX_V3_AVAILABLE = True
except ImportError as e:
    SPDX_V3_AVAILABLE = False
    log.wrn(f"spdx_python_model not available; SPDX 3.0 support disabled: {e}")

from zspdx.util import getHashes


def _convert_spdx_id(spdx_id: str) -> str:
    """Convert SPDX v2 format ID to v3 format"""
    # Remove SPDXRef- prefix if present and normalize for v3
    if spdx_id.startswith("SPDXRef-"):
        spdx_id = spdx_id[8:]
    return spdx_id.replace("_", "-")


def write_spdx_v3(spdx_path: str, doc, namespace: str) -> bool:
    """Write SPDX v3 document in JSON-LD format"""
    if not SPDX_V3_AVAILABLE:
        log.err("SPDX 3.0 support not available - spdx_python_model not installed")
        return False

    try:
        log.inf(f"Writing SPDX 3.0 document {doc.cfg.name} to {spdx_path}")

        # Create all elements for the document
        elements = []

        # Create creation info
        creation_info_id = f"{namespace}#creation-info"
        creation_info = spdx.CreationInfo()
        creation_info._obj_data['@id'] = creation_info_id
        creation_info.created = datetime.now(timezone.utc)
        creation_info.specVersion = "3.0.1"

        # Create agent/tool
        tool_id = f"{namespace}#zephyr-spdx-builder"
        tool = spdx.Agent()
        tool._obj_data['@id'] = tool_id
        tool.name = "Zephyr SPDX builder"
        tool._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = creation_info_id

        # Link agent to creation info
        creation_info.createdBy.append(tool_id)

        elements.extend([creation_info, tool])

        # Create packages as elements
        for pkg in doc.pkgs.values():
            pkg_id = f"{namespace}#{_convert_spdx_id(pkg.cfg.spdxID)}"

            package = spdx.software_Package()
            package._obj_data['@id'] = pkg_id
            package.name = _convert_spdx_id(pkg.cfg.name)
            package._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = (
                creation_info_id
            )

            elements.append(package)

        # Create the main document
        doc_id = f"{namespace}#document"
        spdx_doc = spdx.SpdxDocument()
        spdx_doc._obj_data['@id'] = doc_id
        spdx_doc.name = _convert_spdx_id(doc.cfg.name)
        spdx_doc._obj_data['https://spdx.org/rdf/3.0.1/terms/Core/creationInfo'] = creation_info_id

        # Add element references to document
        for element in elements:
            if element != creation_info:  # Don't include creation info in element list
                element_id = element._obj_data.get('@id')
                if element_id:
                    spdx_doc.element.append(element_id)

        elements.append(spdx_doc)

        # Serialize using the proper encoding approach
        elements_data = []
        for element in elements:
            encoder = spdx.JSONLDEncoder()
            state = spdx.EncodeState()
            element.encode(encoder, state)
            if encoder.data:
                elements_data.append(encoder.data)

        # Create the final JSON-LD structure
        complete_dict = {
            "@context": "https://spdx.org/rdf/3.0.1/spdx-context.jsonld",
            "@graph": elements_data,
        }

        # Write the JSON-LD document
        with open(spdx_path, "w") as f:
            json.dump(complete_dict, f, indent=2, ensure_ascii=False)

        # Calculate hash of the document we just wrote
        hashes = getHashes(spdx_path)
        if not hashes:
            log.err("Error: created document but unable to calculate hash values")
            return False
        doc.myDocSHA1 = hashes[0]

        return True

    except Exception as e:
        log.err(f"Error writing SPDX 3.0 document: {str(e)}")
        import traceback

        log.err(f"Traceback: {traceback.format_exc()}")
        return False
