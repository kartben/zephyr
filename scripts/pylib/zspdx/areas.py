# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Group Zephyr sources into SBOM components, one per MAINTAINERS.yml area.

Areas are allowed to overlap, so a file can end up in several area components.
Where MAINTAINERS.yml says an area only covers a file on behalf of another one
(a ``defer-to-other-areas`` file group), the deferring area is dropped from that
file as soon as some other area claims it outright.
"""

from __future__ import annotations

import logging
import os
import re
import sys
from pathlib import Path

_logger = logging.getLogger(__name__)

# Prefix given to every component derived from a maintainer area, so that area
# components are recognizable and cannot collide with the "-sources"/"-deps"
# components.
AREA_COMPONENT_PREFIX = "area-"

# Free-form note emitted as the package comment of an area component.
AREA_COMMENT = (
    'Maintainer area package: the part of the Zephyr sources in this build that the "{name}" '
    "area of MAINTAINERS.yml covers. Areas may overlap, so a file can be contained by more "
    "than one area package. Status: {status}. Maintainers: {maintainers}."
)

# GitHub labels in MAINTAINERS.yml are written as "area: Foo" / "platform: Bar". Area
# names themselves are unprefixed, but strip such a prefix anyway so a label used as a
# name does not yield an "area-area-Foo" component.
_LABEL_PREFIX_RE = re.compile(r"^\s*(?:areas?|platforms?)\s*:\s*", re.IGNORECASE)

# Characters that are not SPDX-ID-safe; runs of them collapse into a single dash.
_UNSAFE_CHARS_RE = re.compile(r"[^A-Za-z0-9.]+")


def area_component_name(area_name: str) -> str:
    """Return the SBOM component name for a MAINTAINERS.yml area name.

    Component names double as SPDX IDs, so anything that is not ID-safe is turned
    into a dash. Returns "" when nothing usable is left.
    """
    slug = _UNSAFE_CHARS_RE.sub("-", _LABEL_PREFIX_RE.sub("", area_name)).strip("-.")
    return AREA_COMPONENT_PREFIX + slug if slug else ""


def area_comment(area) -> str:
    """Return the package comment describing a maintainer area."""
    return AREA_COMMENT.format(
        name=area.name,
        status=area.status or "unknown",
        maintainers=", ".join(area.maintainers) or "none",
    )


def _import_get_maintainer():
    """Import get_maintainer, the reference implementation of the MAINTAINERS.yml rules.

    It lives in scripts/, which is not otherwise on the path of the zspdx package.
    """
    scripts_dir = str(Path(__file__).resolve().parents[2])
    if scripts_dir not in sys.path:
        sys.path.append(scripts_dir)
    import get_maintainer

    return get_maintainer


class MaintainerAreas:
    """The MAINTAINERS.yml areas of a Zephyr tree, matched against built sources."""

    def __init__(self, maintainers):
        self.maintainers = maintainers

    @classmethod
    def load(cls, zephyr_base: str) -> MaintainerAreas | None:
        """Load MAINTAINERS.yml from a Zephyr source tree.

        Returns ``None``, after logging why, when the file is missing or unusable.
        """
        maintainers_file = os.path.join(zephyr_base, "MAINTAINERS.yml") if zephyr_base else ""
        if not maintainers_file or not os.path.isfile(maintainers_file):
            _logger.error(
                "cannot find MAINTAINERS.yml in %s; skipping maintainer area packages",
                zephyr_base or "<unknown Zephyr base>",
            )
            return None

        try:
            get_maintainer = _import_get_maintainer()
        except ImportError as e:
            _logger.error("cannot import get_maintainer: %s; skipping maintainer area packages", e)
            return None

        try:
            return cls(get_maintainer.Maintainers(maintainers_file))
        except (get_maintainer.MaintainersError, OSError) as e:
            _logger.error(
                "cannot parse %s: %s; skipping maintainer area packages", maintainers_file, e
            )
            return None

    def areas_for(self, rel_path: str) -> list:
        """Return the areas owning ``rel_path``, a path relative to the Zephyr tree.

        An area matching only through a ``defer-to-other-areas`` file group is
        dropped as soon as another area claims the path outright; when every match
        is a deferring one, they are all kept, as MAINTAINERS.yml prescribes.
        """
        areas = self.maintainers.relpath2areas(rel_path)
        owning = [area for area in areas if not area.is_deferred_for_path(rel_path)]
        return owning or areas

    def group(self, files_by_rel_path: dict) -> tuple[list, list]:
        """Split files across the areas that own them.

        Args:
            files_by_rel_path: files to group, keyed by their path relative to the
                               Zephyr source tree.

        Returns:
            A ``(grouped, unassigned)`` pair. ``grouped`` maps each area that owns at
            least one file to its files, ordered as in MAINTAINERS.yml; ``unassigned``
            holds the paths no area covers.
        """
        files_by_area = {}
        unassigned = []

        for rel_path in sorted(files_by_rel_path):
            areas = self.areas_for(rel_path)
            if not areas:
                unassigned.append(rel_path)
                continue
            for area in areas:
                files_by_area.setdefault(area.name, []).append(files_by_rel_path[rel_path])

        grouped = [
            (area, files_by_area[name])
            for name, area in self.maintainers.areas.items()
            if name in files_by_area
        ]
        return grouped, unassigned
