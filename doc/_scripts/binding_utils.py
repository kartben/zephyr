# Copyright (c) 2025 The Linux Foundation
# SPDX-License-Identifier: Apache-2.0

"""
Shared utilities for working with devicetree binding types.

Binding types are derived from the first directory component of a binding's
path relative to ``dts/bindings/``. For example, a binding at
``dts/bindings/sensor/bosch,bme280.yaml`` has type ``sensor``.

The human-readable descriptions for each type come from
``dts/bindings/binding-types.txt``.

This module also provides generic acronym/abbreviation extraction from binding
type descriptions. Descriptions in ``binding-types.txt`` may contain acronyms
in the form ``ACRONYM (Expanded Form)``, e.g.
``ADC (Analog to Digital Converter)``. The :func:`parse_acronyms` function
extracts these into a list of :class:`TextSegment` and :class:`AcronymSegment`
objects that can be converted into framework-specific representations:

- **docutils nodes** (used by the Sphinx domain extension)
- **RST ``:abbr:`` roles** (used by ``gen_devicetree_rest.py``)

This module is used by:
- ``gen_devicetree_rest.py`` (bindings documentation generation)
- ``gen_boards_catalog.py`` (board catalog / hardware features)
- The Zephyr Sphinx domain extension (``doc/_extensions/zephyr/domain``)
"""

import re
from pathlib import Path
from typing import NamedTuple

ZEPHYR_BASE = Path(__file__).parents[2]
ZEPHYR_BINDINGS = ZEPHYR_BASE / "dts" / "bindings"
BINDING_TYPES_TXT = ZEPHYR_BINDINGS / "binding-types.txt"

# Regex patterns for extracting acronyms from description strings.
# Matches tokens like ``ADC (Analog to Digital Converter)``.
ACRONYM_PATTERN = re.compile(r'([a-zA-Z0-9-]+)\s*\((.*?)\)')
ACRONYM_PATTERN_UPPERCASE_ONLY = re.compile(r'(\b[A-Z0-9-]+)\s*\((.*?)\)')


class TextSegment(NamedTuple):
    """A plain-text segment in a parsed description string."""
    text: str


class AcronymSegment(NamedTuple):
    """An acronym/abbreviation segment with its expanded explanation.

    For example, for ``ADC (Analog to Digital Converter)``:
    - ``abbr`` is ``"ADC"``
    - ``explanation`` is ``"Analog to Digital Converter"``
    """
    abbr: str
    explanation: str


def load_binding_types(binding_types_path=None):
    """Load binding type descriptions from a ``binding-types.txt`` file.

    Args:
        binding_types_path: Path to ``binding-types.txt``. Defaults to the
            standard location under ``dts/bindings/``.

    Returns:
        A ``dict[str, str]`` mapping type keys (e.g. ``"sensor"``) to
        human-readable descriptions (e.g. ``"Sensors"``).
    """
    if binding_types_path is None:
        binding_types_path = BINDING_TYPES_TXT

    types = {}
    with open(binding_types_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            key, value = line.split('\t', 1)
            types[key] = value

    return types


def parse_acronyms(text, uppercase_only=False):
    """Parse a description string into a list of text and acronym segments.

    Scans *text* for patterns like ``ACRONYM (Expanded Form)`` and splits
    the string into a sequence of :class:`TextSegment` and
    :class:`AcronymSegment` objects.

    Args:
        text: The description string to parse.
        uppercase_only: If ``True``, only match uppercase acronyms (useful
            when processing free-form text where lowercase words followed
            by parenthetical text should not be treated as acronyms).

    Returns:
        A ``list`` of :class:`TextSegment` and :class:`AcronymSegment`
        instances.

    Example::

        >>> parse_acronyms("ADC (Analog to Digital Converter)")
        [AcronymSegment(abbr='ADC', explanation='Analog to Digital Converter')]

        >>> parse_acronyms("GPIO (General Purpose Input/Output) & Headers")
        [AcronymSegment(abbr='GPIO', explanation='General Purpose Input/Output'),
         TextSegment(text=' & Headers')]
    """
    pattern = ACRONYM_PATTERN_UPPERCASE_ONLY if uppercase_only else ACRONYM_PATTERN
    segments = []
    last_end = 0

    for match in pattern.finditer(text):
        # Add any plain text before the match
        if match.start() > last_end:
            segments.append(TextSegment(text[last_end:match.start()]))

        abbr, explanation = match.groups()
        segments.append(AcronymSegment(abbr, explanation))
        last_end = match.end()

    # Add any remaining text
    if last_end < len(text):
        segments.append(TextSegment(text[last_end:]))

    # If there were no matches, return the whole string as a single TextSegment
    if not segments:
        segments.append(TextSegment(text))

    return segments


def acronyms_to_rst(segments):
    """Convert parsed acronym segments into RST markup using ``:abbr:`` roles.

    Plain text segments are returned as-is. Acronym segments are converted
    to the Sphinx ``:abbr:`` role, e.g.
    ``:abbr:`ADC (Analog to Digital Converter)```.

    Args:
        segments: A list of :class:`TextSegment` and :class:`AcronymSegment`
            objects, as returned by :func:`parse_acronyms`.

    Returns:
        An RST string with acronyms wrapped in ``:abbr:`` roles.

    Example::

        >>> acronyms_to_rst(parse_acronyms("ADC (Analog to Digital Converter)"))
        ':abbr:`ADC (Analog to Digital Converter)`'
    """
    parts = []
    for segment in segments:
        if isinstance(segment, AcronymSegment):
            parts.append(f':abbr:`{segment.abbr} ({segment.explanation})`')
        else:
            parts.append(segment.text)
    return ''.join(parts)


def strip_acronym_parentheticals(text):
    """Remove parenthetical acronym expansions from a description string.

    Returns only the short form of each acronym, with parenthetical
    explanations stripped. Useful when the full expansion is not desired,
    e.g. for computing RST underline lengths.

    Args:
        text: The description string (e.g. ``"ADC (Analog to Digital
            Converter)"``).

    Returns:
        The string with parentheticals removed (e.g. ``"ADC"``).

    Example::

        >>> strip_acronym_parentheticals("GPIO (General Purpose I/O) & Headers")
        'GPIO & Headers'
    """
    return ACRONYM_PATTERN.sub(r'\1', text).strip()


def binding_type_from_path(binding_path, zephyr_bindings_dir=None):
    """Extract the binding type from a binding's file path.

    The type is the first directory component after the ``dts/bindings/``
    prefix. For bindings not under the Zephyr bindings directory, returns
    ``"misc"``.

    Args:
        binding_path: Path (string or ``Path``) to the binding YAML file.
        zephyr_bindings_dir: Path to the Zephyr ``dts/bindings/`` directory.
            Defaults to the standard location.

    Returns:
        A string like ``"sensor"``, ``"gpio"``, ``"i2c"``, or ``"misc"``.
    """
    if zephyr_bindings_dir is None:
        zephyr_bindings_dir = ZEPHYR_BINDINGS

    binding_path = Path(binding_path)
    zephyr_bindings_dir = Path(zephyr_bindings_dir)

    if binding_path.is_relative_to(zephyr_bindings_dir):
        return binding_path.relative_to(zephyr_bindings_dir).parts[0]

    # Fall back: try to find 'dts/bindings/' in the path string
    as_posix = binding_path.as_posix()
    dts_bindings = 'dts/bindings/'
    idx = as_posix.rfind(dts_bindings)
    if idx != -1:
        remainder = as_posix[idx + len(dts_bindings):]
        parts = remainder.split('/')
        if len(parts) > 1:
            return parts[0]

    return "misc"
