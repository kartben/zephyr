# Copyright (c) 2025 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""Extract hardware components from a build's devicetree (EDT).

Zephyr builds emit ``<build>/zephyr/edt.pickle``: the fully resolved
:class:`devicetree.edtlib.EDT` for the target. This module loads it and turns
every enabled, binding-backed devicetree node into a small dict describing one
hardware component, which the SPDX 3.1 serializer maps to the Hardware profile.

Everything here is best-effort: when the pickle, the devicetree package or the
vendor registry cannot be found, an empty list is returned so SBOM generation
proceeds without devicetree hardware.
"""

import glob
import logging
import os
import pickle
import re
import sys

_logger = logging.getLogger(__name__)


def _zephyr_base() -> str:
    """Return ZEPHYR_BASE from the environment, or infer it from this file."""
    base = os.environ.get("ZEPHYR_BASE")
    if base:
        return base
    # This module lives at <zephyr>/scripts/pylib/zspdx/devicetree.py.
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _first_sentence(text: str | None) -> str:
    """Return the first sentence of a binding description, trimmed."""
    if not text:
        return ""
    text = text.replace("\n", " ")
    first_para = text.split("  ")[0].strip()
    match = re.search(r"(.*?)\.(?:\s|$)", first_para)
    return match.group(1).strip() if match else first_para


def _load_vendor_prefixes(zephyr_base: str) -> dict:
    """Parse ``dts/bindings/vendor-prefixes.txt`` into ``{prefix: full name}``."""
    path = os.path.join(zephyr_base, "dts", "bindings", "vendor-prefixes.txt")
    prefixes = {}
    try:
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                parts = line.split("\t", 1) if "\t" in line else line.split(None, 1)
                if len(parts) == 2:
                    prefixes[parts[0].strip()] = parts[1].strip()
    except OSError:
        _logger.debug("vendor-prefixes.txt not found at %s", path)
    return prefixes


def _find_edt_pickle(build_dir: str) -> str | None:
    """Locate ``edt.pickle`` in a build directory, including a sysbuild domain."""
    direct = os.path.join(build_dir, "zephyr", "edt.pickle")
    if os.path.isfile(direct):
        return direct
    # sysbuild: images live in <build>/<domain>/zephyr/edt.pickle
    matches = glob.glob(os.path.join(build_dir, "*", "zephyr", "edt.pickle"))
    return matches[0] if len(matches) == 1 else None


def _binding_type(binding_path: str, bindings_root: str) -> str:
    """Category of a binding: its top-level directory under ``dts/bindings``.

    Mirrors ``doc/_scripts/dts_binding_types.py`` / ``gen_catalogs._get_binding_type``.
    Returns ``"misc"`` for out-of-tree bindings or when it cannot be determined.
    """
    try:
        rel = os.path.relpath(binding_path, bindings_root)
    except ValueError:
        return "misc"
    parts = rel.split(os.sep)
    if parts[0] == ".." or len(parts) < 2:
        return "misc"
    return parts[0]


# A linker map line introducing an input object, e.g.
#   " .rodata 0x3c0ff310  0x20 zephyr/drivers/i2c/libdrivers__i2c.a(i2c_esp32.c.obj)"
# and, indented under it, the symbols it defines:
#   "            0x3c0ff310                __device_dts_ord_9"
_MAP_OBJECT_RE = re.compile(r"^\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+(\S+)$")
_MAP_DEVICE_RE = re.compile(r"^\s+0x[0-9a-f]+\s+__device_dts_ord_(\d+)$")
# "zephyr/drivers/i2c/libdrivers__i2c.a(i2c_esp32.c.obj)" -> ("drivers__i2c", "i2c_esp32.c")
_MAP_ARCHIVE_RE = re.compile(r"lib([^/]+)\.a\((.+?)\.obj\)$")
# "zephyr/CMakeFiles/zephyr.dir/drivers/mipi_dbi/mipi_dbi_spi.c.obj" -> ("zephyr", "mipi_dbi_spi.c")
_MAP_OBJECT_DIR_RE = re.compile(r"CMakeFiles/([^/]+)\.dir/(.+?)\.obj$")


def _find_linker_map(build_dir: str) -> str | None:
    """Locate the final image's linker map, including a sysbuild domain."""
    for candidate in (
        os.path.join(build_dir, "zephyr", "zephyr.map"),
        *sorted(glob.glob(os.path.join(build_dir, "*", "zephyr", "zephyr.map"))),
    ):
        if os.path.isfile(candidate):
            return candidate
    return None


def _driver_objects(build_dir: str) -> dict:
    """Map each instantiated device's dependency ordinal to the object that defines it.

    ``DEVICE_DT_DEFINE()`` emits one ``__device_dts_ord_<N>`` symbol per devicetree
    node it instantiates, where ``N`` is the node's ``dep_ordinal``. The linker map
    records which input object defines each symbol, so it names the driver
    translation unit that actually drives the node -- exactly, and without needing
    a toolchain binary or a source-level heuristic.

    Returns ``{ordinal: {"target": str, "source": str, "location": str}}``; see
    :func:`_parse_object_path` for the fields.
    """
    map_path = _find_linker_map(build_dir)
    if not map_path:
        _logger.debug("no linker map under %s; skipping driver linkage", build_dir)
        return {}

    drivers = {}
    current = None
    try:
        with open(map_path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                device = _MAP_DEVICE_RE.match(line)
                if device and current:
                    drivers[int(device.group(1))] = current
                    continue
                obj = _MAP_OBJECT_RE.match(line)
                if obj:
                    current = _parse_object_path(obj.group(1))
    except OSError as exc:
        _logger.warning("could not read linker map %s: %s", map_path, exc)
        return {}

    _logger.debug("linked %d device(s) to a driver object via %s", len(drivers), map_path)
    return drivers


def _parse_object_path(path: str) -> dict | None:
    """Split a linker-map object path into the driver's target, file name and location.

    Two forms appear: an archive member
    ``zephyr/drivers/i2c/libdrivers__i2c.a(i2c_esp32.c.obj)``, whose directory
    mirrors the source tree, and a loose object
    ``zephyr/CMakeFiles/zephyr.dir/drivers/mipi_dbi/mipi_dbi_spi.c.obj``, which
    carries the source path outright. ``location`` is that path relative to the
    source tree when it can be recovered, and is only ever a hint: it
    disambiguates a basename shared by several files.
    """
    archive = _MAP_ARCHIVE_RE.search(path)
    if archive:
        source = os.path.basename(archive.group(2))
        # "zephyr/drivers/i2c/libdrivers__i2c.a" -> "drivers/i2c"
        parts = os.path.dirname(path).split("/")
        location = "/".join(parts[1:] + [source]) if len(parts) > 1 else ""
        return {"target": archive.group(1), "source": source, "location": location}
    obj = _MAP_OBJECT_DIR_RE.search(path)
    if obj:
        rel = obj.group(2)
        return {
            "target": obj.group(1),
            "source": os.path.basename(rel),
            "location": rel,
        }
    return None


def _is_hardware(node) -> bool:
    """Whether an EDT node is an enabled, binding-backed hardware component.

    Synthetic ``zephyr,*`` nodes are software abstractions rather than parts and
    are excluded.
    """
    return bool(
        node.matching_compat
        and node.binding_path
        and node.status == "okay"
        and not node.matching_compat.startswith("zephyr,")
    )


def extract_hardware(build_dir: str) -> list[dict]:
    """Return one dict per hardware component in the build's devicetree.

    Each entry has ``compatible``, ``name``, ``path``, ``description``,
    ``vendor`` (resolved manufacturer name, or "") and ``parent`` (the path of
    the nearest ancestor that is itself a hardware component, or ``None`` when it
    hangs directly off the board). ``driver`` names the CMake target and source
    file of the driver that instantiates the node, or is ``None`` for a node no
    driver claims (see :func:`_driver_objects`).
    """
    edt_path = _find_edt_pickle(build_dir)
    if not edt_path:
        _logger.debug("no edt.pickle found under %s; skipping devicetree hardware", build_dir)
        return []

    zephyr_base = _zephyr_base()
    dt_src = os.path.join(zephyr_base, "scripts", "dts", "python-devicetree", "src")
    if os.path.isdir(dt_src) and dt_src not in sys.path:
        sys.path.insert(0, dt_src)

    try:
        with open(edt_path, "rb") as fh:
            edt = pickle.load(fh)
    except Exception as exc:  # noqa: BLE001
        _logger.warning("could not load EDT pickle %s: %s", edt_path, exc)
        return []

    vendors = _load_vendor_prefixes(zephyr_base)
    bindings_root = os.path.join(zephyr_base, "dts", "bindings")
    drivers = _driver_objects(build_dir)
    hardware = []
    for node in edt.nodes:
        if not _is_hardware(node):
            continue

        # nearest ancestor that is itself a captured hardware component
        ancestor = node.parent
        while ancestor is not None and not _is_hardware(ancestor):
            ancestor = ancestor.parent

        compat = node.matching_compat
        prefix = compat.split(",", 1)[0] if "," in compat else ""
        hardware.append(
            {
                "compatible": compat,
                "name": node.name,
                "path": node.path,
                "parent": ancestor.path if ancestor is not None else None,
                "description": _first_sentence(node.description),
                "vendor": vendors.get(prefix, ""),
                "binding_type": _binding_type(node.binding_path, bindings_root),
                "driver": drivers.get(node.dep_ordinal),
            }
        )

    _logger.info("extracted %d devicetree hardware component(s) from %s", len(hardware), edt_path)
    return hardware
