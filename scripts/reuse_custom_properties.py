"""Backport of the ``custom_properties`` annotations API onto released ``reuse``.

The docs licensing page and the licensing helper scripts need ``reuse`` to preserve the
``[[annotations]]`` keys of ``REUSE.toml`` that it does not itself recognise (the ``Zephyr-*``
keys), instead of silently discarding them. That API is proposed upstream in
`reuse-tool pull request 1368 <https://codeberg.org/fsfe/reuse-tool/pulls/1368>`_ but is not part
of a released ``reuse`` yet.

Importing this module monkey patches :class:`reuse.global_licensing.AnnotationsItem` with the
equivalent of that pull request: any annotation parsed from ``REUSE.toml`` afterwards exposes the
keys ``reuse`` does not recognise through a new ``custom_properties`` dict attribute. Import it
*before* calling into the ``reuse`` library.

The patch applies itself only when the installed ``reuse`` lacks the attribute, so once the API is
released (and the requirement bumped) this module turns into a no-op and can simply be deleted.

SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
"""

from __future__ import annotations

from typing import Any

import attrs
from reuse import global_licensing


def _apply() -> None:
    base = global_licensing.AnnotationsItem
    if hasattr(base, "custom_properties"):
        # The installed reuse already provides the API natively.
        return

    base_from_dict = base.from_dict.__func__
    known_keys = frozenset(global_licensing._TOML_KEYS.values())

    @attrs.define(frozen=True)
    class AnnotationsItem(base):
        """:class:`reuse.global_licensing.AnnotationsItem` with ``custom_properties``."""

        #: Keys of the [[annotations]] table that reuse does not itself recognise, preserved
        #: verbatim instead of being discarded.
        custom_properties: dict[str, Any] = attrs.field(
            factory=dict,
            validator=global_licensing._instance_of(dict),
        )

        @classmethod
        def from_dict(cls, values: dict[str, Any]) -> AnnotationsItem:
            # Let the original parser do all the heavy lifting (converters, validation, ...),
            # then graft the unrecognised keys on.
            item = base_from_dict(cls, values)
            return attrs.evolve(
                item,
                custom_properties={
                    key: value for key, value in values.items() if key not in known_keys
                },
            )

    # ReuseTOML.from_dict() looks AnnotationsItem up in the module namespace at call time, and
    # its own attrs validator accepts the subclass, so swapping the class in is all it takes.
    global_licensing.AnnotationsItem = AnnotationsItem


_apply()
