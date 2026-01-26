# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import re
from collections import defaultdict
from typing import Dict

from zspdx.model import SBOMComponent


# Regex patterns for external reference validation
CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"


def normalize_spdx_name(name: str) -> str:
    """Replace '_' by '-' since it's not allowed in SPDX ID."""
    return name.replace("_", "-")


def generate_download_url(url: str, revision: str) -> str:
    """Generate download URL with revision if available."""
    if not revision:
        return url
    return f'git+{url}@{revision}'


def get_document_name(component: SBOMComponent) -> str:
    """Determine which document a component belongs to."""
    name = component.name

    if name == "app-sources":
        return "app"
    elif name == "sdk-sources":
        return "sdk"
    elif name == "zephyr-sources" or name.endswith("-sources"):
        # zephyr-sources and module sources (e.g., "module-name-sources")
        return "zephyr"
    elif name.endswith("-deps"):
        return "modules-deps"
    else:
        # Build targets go into build document
        return "build"


def group_components_into_documents(components: Dict[str, SBOMComponent]) -> Dict[str, list]:
    """Group components into SPDX documents based on their names/purposes."""
    documents = defaultdict(list)

    for component in components.values():
        doc_name = get_document_name(component)
        documents[doc_name].append(component)

    return dict(documents)


def get_standard_licenses() -> set:
    """Get set of standard SPDX license IDs."""
    # Import here to avoid circular dependency
    from zspdx.licenses import LICENSES
    return set(LICENSES)
