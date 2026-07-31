# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

import logging
import os
from datetime import UTC, datetime

_logger = logging.getLogger(__name__)

# Regex patterns for external reference validation
CPE23TYPE_REGEX = (
    r'^cpe:2\.3:[aho\*\-](:(((\?*|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^'
    r"`\{\|}~]))+(\?*|\*?))|[\*\-])){5}(:(([a-zA-Z]{2,3}(-([a-zA-Z]{2}|[0-9]{3}))?)|[\*\-]))(:(((\?*"
    r'|\*?)([a-zA-Z0-9\-\._]|(\\[\\\*\?!"#$$%&\'\(\)\+,\/:;<=>@\[\]\^`\{\|}~]))+(\?*|\*?))|[\*\-])){4}$'
)
PURL_REGEX = r"^pkg:.+(\/.+)?\/.+(@.+)?(\?.+)?(#.+)?$"


def creation_timestamp() -> datetime:
    """Return the timestamp to record as the SBOM creation time.

    ``SOURCE_DATE_EPOCH`` wins when it is set to a valid Unix timestamp, so that
    regenerating an SBOM for an unchanged build produces byte-identical documents;
    without it a rebuild differs from its predecessor in every document, and a consumer
    cannot tell a re-run apart from a real change. Falls back to the current time.
    """
    source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if source_date_epoch:
        try:
            return datetime.fromtimestamp(int(source_date_epoch), tz=UTC)
        except (OSError, OverflowError, ValueError):
            _logger.warning(
                f"ignoring SOURCE_DATE_EPOCH={source_date_epoch!r}: not a Unix timestamp"
            )
    return datetime.now(UTC)


def normalize_spdx_name(name: str) -> str:
    """Replace '_' by '-' since it's not allowed in SPDX ID."""
    return name.replace("_", "-")


def generate_download_url(url: str, revision: str) -> str:
    """Generate download URL with revision if available."""
    if not revision:
        return url
    return f'git+{url}@{revision}'


def get_standard_licenses() -> set:
    """Get set of standard SPDX license IDs."""
    # Import here to avoid circular dependency
    from zspdx.licenses import LICENSES

    return set(LICENSES)
