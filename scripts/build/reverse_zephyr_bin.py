#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Best-effort reverse engineering for raw Zephyr binary images.

This script works on a raw ``zephyr.bin`` without requiring the original
build directory. It uses heuristics to infer:

* the linked flash base address
* pointer width and endianness
* candidate ``struct device`` instances
* device dependency arrays
* device devicetree metadata such as node labels
* candidate iterable regions such as the device array and init entries
* likely devicetree strings embedded in the image

The output is heuristic and should be treated as reverse-engineering guidance,
not as authoritative build metadata.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEVICE_FLAGS_MAX = 0x3F
DEVICE_NAME_MAX_LEN = 48
DEVICE_DEPS_SEP = -(1 << 15)
DEVICE_DEPS_ENDS = (1 << 15) - 1
PAGE_ALIGNMENT = 0x1000

PRINTABLE_RE = re.compile(r"^[ -~]+$")
DEVICE_NAME_RE = re.compile(r"^[A-Za-z0-9_:@./+\-]{1,48}$")
NODELABEL_RE = re.compile(r"^[a-z_][a-z0-9_]*$")
COMPATIBLE_RE = re.compile(r"^[a-z0-9][a-z0-9,+._\-]*,[a-z0-9][a-z0-9,+._\-]*$")
DT_PATH_RE = re.compile(r"^/[A-Za-z0-9,._@/\-+]+$")


def parse_int(value: str) -> int:
    return int(value, 0)


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def read_int(data: bytes, offset: int, size: int, endian: str) -> int | None:
    if offset < 0 or offset + size > len(data):
        return None
    return int.from_bytes(data[offset : offset + size], endian)


def read_ptr(data: bytes, offset: int, ptr_size: int, endian: str) -> int | None:
    return read_int(data, offset, ptr_size, endian)


def read_s16(data: bytes, offset: int, endian: str) -> int | None:
    raw = read_int(data, offset, 2, endian)
    if raw is None:
        return None
    if raw & 0x8000:
        raw -= 0x10000
    return raw


def extract_strings(data: bytes, min_length: int = 2) -> list[tuple[int, str]]:
    strings: list[tuple[int, str]] = []
    start: int | None = None

    for idx, value in enumerate(data):
        if 32 <= value <= 126:
            if start is None:
                start = idx
            continue

        if value == 0 and start is not None:
            candidate = data[start:idx].decode("ascii", errors="ignore")
            if len(candidate) >= min_length and PRINTABLE_RE.match(candidate):
                strings.append((start, candidate))
            start = None
            continue

        start = None

    return strings


def probable_device_name(value: str) -> bool:
    return bool(DEVICE_NAME_RE.fullmatch(value) and not value.startswith("/"))


def probable_nodelabel(value: str) -> bool:
    return bool(NODELABEL_RE.fullmatch(value))


def probable_compatible(value: str) -> bool:
    return bool(COMPATIBLE_RE.fullmatch(value))


def probable_dt_path(value: str) -> bool:
    return bool(DT_PATH_RE.fullmatch(value))


@dataclass(frozen=True)
class DeviceLayout:
    ptr_size: int
    has_deinit: bool
    has_deps: bool
    has_pm: bool
    has_dt_meta: bool

    @property
    def ops_ptr_count(self) -> int:
        return 2 if self.has_deinit else 1

    @property
    def flags_offset(self) -> int:
        return (5 + self.ops_ptr_count) * self.ptr_size

    @property
    def optional_offset(self) -> int:
        return align_up(self.flags_offset + 1, self.ptr_size)

    @property
    def size(self) -> int:
        optional_ptrs = int(self.has_deps) + int(self.has_pm) + int(self.has_dt_meta)
        return self.optional_offset + optional_ptrs * self.ptr_size

    @property
    def name(self) -> str:
        parts = ["device"]
        if self.has_deinit:
            parts.append("deinit")
        if self.has_deps:
            parts.append("deps")
        if self.has_pm:
            parts.append("pm")
        if self.has_dt_meta:
            parts.append("dt")
        return "_".join(parts)


@dataclass
class DeviceCandidate:
    offset: int
    address: int
    layout: DeviceLayout
    score: int
    name: str
    flags: int
    config: int
    api: int
    state: int
    data: int
    init: int
    deinit: int | None
    deps: dict[str, Any] | None
    pm: int | None
    dt_meta: dict[str, Any] | None
    handle: int | None = None


@dataclass
class RegionGroup:
    kind: str
    offset: int
    stride: int
    count: int
    score: int


class ZephyrBinaryAnalyzer:
    def __init__(
        self,
        data: bytes,
        *,
        pointer_size: int | None = None,
        endianness: str | None = None,
        base: int | None = None,
    ) -> None:
        self.data = data
        self.pointer_size = pointer_size
        self.endianness = endianness
        self.base = base
        self.strings = extract_strings(data)
        self.strings_by_offset = {offset: value for offset, value in self.strings}

    def analyze(self) -> dict[str, Any]:
        candidates: list[dict[str, Any]] = []

        ptr_sizes = [self.pointer_size] if self.pointer_size else [4, 8]
        endiannesses = [self.endianness] if self.endianness else ["little", "big"]

        for ptr_size in ptr_sizes:
            for endian in endiannesses:
                bases = (
                    [self.base]
                    if self.base is not None
                    else self._guess_bases(ptr_size, endian)
                )
                for base in bases:
                    result = self._analyze_format(ptr_size, endian, base)
                    if result is not None:
                        candidates.append(result)

        if not candidates:
            return {
                "input_size": len(self.data),
                "devices": [],
                "iterable_regions": [],
                "init_entries": [],
                "devicetree": self._collect_devicetree_strings(),
                "warnings": [
                    "No Zephyr-specific layout could be inferred from the raw binary.",
                    "Try providing --base, --pointer-size, or --endianness.",
                ],
            }

        best = max(candidates, key=lambda item: item["score"])
        best["warnings"] = [
            (
                "Results are heuristic because raw zephyr.bin images do not preserve "
                "symbols or sections."
            )
        ]
        return best

    def _guess_bases(self, ptr_size: int, endian: str) -> list[int]:
        interesting_offsets = [
            offset
            for offset, value in self.strings
            if probable_device_name(value) or probable_nodelabel(value)
        ]

        if not interesting_offsets:
            return [0]

        buckets: dict[int, list[int]] = {}
        for offset in interesting_offsets:
            buckets.setdefault(offset & (PAGE_ALIGNMENT - 1), []).append(offset)

        counts: Counter[int] = Counter()
        for offset in range(0, len(self.data) - ptr_size + 1, ptr_size):
            value = read_ptr(self.data, offset, ptr_size, endian)
            if not value:
                continue
            for string_offset in buckets.get(value & (PAGE_ALIGNMENT - 1), []):
                base = value - string_offset
                if base >= 0 and (base & (PAGE_ALIGNMENT - 1)) == 0:
                    counts[base] += 1

        if not counts:
            return [0]

        return [base for base, _ in counts.most_common(8)]

    def _analyze_format(self, ptr_size: int, endian: str, base: int) -> dict[str, Any] | None:
        devices = self._scan_devices(ptr_size, endian, base)
        device_group = self._best_device_group(devices)
        chosen_devices = device_group["devices"] if device_group else devices

        if not chosen_devices:
            return None

        for index, device in enumerate(chosen_devices, start=1):
            device.handle = index

        device_by_address = {device.address: device for device in chosen_devices}
        for device in chosen_devices:
            if device.deps is not None:
                self._resolve_handles(device.deps, chosen_devices)

        init_entries, init_group = self._scan_init_entries(
            ptr_size,
            endian,
            base,
            device_by_address,
        )

        iterable_regions = []
        if device_group is not None:
            iterable_regions.append(
                {
                    "kind": "device_array",
                    "offset": device_group["offset"],
                    "address": base + device_group["offset"],
                    "entry_size": device_group["stride"],
                    "count": device_group["count"],
                }
            )
        if init_group is not None:
            iterable_regions.append(
                {
                    "kind": "init_entries",
                    "offset": init_group.offset,
                    "address": base + init_group.offset,
                    "entry_size": init_group.stride,
                    "count": init_group.count,
                }
            )

        score = sum(device.score for device in chosen_devices)
        score += len(chosen_devices) * 25
        score += len(init_entries) * 4
        score += sum(4 for device in chosen_devices if device.dt_meta is not None)
        if device_group is not None:
            score += device_group["count"] * 20

        return {
            "input_size": len(self.data),
            "score": score,
            "format": {
                "pointer_size": ptr_size,
                "endianness": endian,
                "base": base,
            },
            "devices": [self._device_to_dict(device) for device in chosen_devices],
            "iterable_regions": iterable_regions,
            "init_entries": init_entries,
            "devicetree": self._collect_devicetree_strings(chosen_devices),
        }

    def _scan_devices(self, ptr_size: int, endian: str, base: int) -> list[DeviceCandidate]:
        layouts = [
            DeviceLayout(ptr_size, has_deinit, has_deps, has_pm, has_dt_meta)
            for has_deinit in (False, True)
            for has_deps in (False, True)
            for has_pm in (False, True)
            for has_dt_meta in (False, True)
        ]

        best_by_offset: dict[int, DeviceCandidate] = {}
        for offset in range(0, len(self.data) - ptr_size + 1, ptr_size):
            for layout in layouts:
                candidate = self._try_device_candidate(offset, layout, endian, base)
                if candidate is None:
                    continue
                previous = best_by_offset.get(offset)
                if previous is None or candidate.score > previous.score:
                    best_by_offset[offset] = candidate

        return sorted(best_by_offset.values(), key=lambda candidate: candidate.offset)

    def _try_device_candidate(
        self,
        offset: int,
        layout: DeviceLayout,
        endian: str,
        base: int,
    ) -> DeviceCandidate | None:
        if offset + layout.size > len(self.data):
            return None

        ptr_size = layout.ptr_size
        name_ptr = read_ptr(self.data, offset, ptr_size, endian)
        config = read_ptr(self.data, offset + ptr_size, ptr_size, endian)
        api = read_ptr(self.data, offset + 2 * ptr_size, ptr_size, endian)
        state = read_ptr(self.data, offset + 3 * ptr_size, ptr_size, endian)
        data_ptr = read_ptr(self.data, offset + 4 * ptr_size, ptr_size, endian)
        init_ptr = read_ptr(self.data, offset + 5 * ptr_size, ptr_size, endian)
        if None in (name_ptr, config, api, state, data_ptr, init_ptr):
            return None

        name = self._string_from_pointer(base, name_ptr)
        if name is None or not probable_device_name(name):
            return None

        flags = self.data[offset + layout.flags_offset]
        if flags > DEVICE_FLAGS_MAX:
            return None

        optional_offset = layout.optional_offset
        deinit_ptr = None
        if layout.has_deinit:
            deinit_ptr = read_ptr(self.data, offset + 6 * ptr_size, ptr_size, endian)
            if deinit_ptr is None:
                return None

        deps_ptr = None
        pm_ptr = None
        dt_meta_ptr = None

        cursor = offset + optional_offset
        if layout.has_deps:
            deps_ptr = read_ptr(self.data, cursor, ptr_size, endian)
            cursor += ptr_size
        if layout.has_pm:
            pm_ptr = read_ptr(self.data, cursor, ptr_size, endian)
            cursor += ptr_size
        if layout.has_dt_meta:
            dt_meta_ptr = read_ptr(self.data, cursor, ptr_size, endian)

        score = 8
        if self._pointer_inside_image(base, init_ptr):
            score += 4
        else:
            return None

        for value in (config, api, state, data_ptr):
            if value and value % ptr_size == 0:
                score += 1

        if deinit_ptr is not None:
            if deinit_ptr == 0 or self._pointer_inside_image(base, deinit_ptr):
                score += 1
            else:
                return None

        deps = None
        if deps_ptr:
            deps = self._parse_deps(base, deps_ptr, endian)
            if deps is None:
                return None
            score += 4

        dt_meta = None
        if dt_meta_ptr:
            dt_meta = self._parse_dt_meta(base, dt_meta_ptr, ptr_size, endian)
            if dt_meta is None:
                return None
            score += 4 + len(dt_meta["nodelabels"])

        if layout.has_pm:
            if pm_ptr == 0 or pm_ptr is None or (pm_ptr % ptr_size) != 0:
                return None
            score += 1

        return DeviceCandidate(
            offset=offset,
            address=base + offset,
            layout=layout,
            score=score,
            name=name,
            flags=flags,
            config=config,
            api=api,
            state=state,
            data=data_ptr,
            init=init_ptr,
            deinit=deinit_ptr,
            deps=deps,
            pm=pm_ptr,
            dt_meta=dt_meta,
        )

    def _best_device_group(self, devices: list[DeviceCandidate]) -> dict[str, Any] | None:
        if not devices:
            return None

        best: dict[str, Any] | None = None
        current: list[DeviceCandidate] = []

        for device in devices:
            if not current:
                current = [device]
                continue

            previous = current[-1]
            if (
                device.layout.size == previous.layout.size
                and device.layout.name == previous.layout.name
                and device.offset == previous.offset + previous.layout.size
            ):
                current.append(device)
                continue

            best = self._pick_better_group(best, current)
            current = [device]

        best = self._pick_better_group(best, current)
        return best

    def _pick_better_group(
        self,
        best: dict[str, Any] | None,
        group: list[DeviceCandidate],
    ) -> dict[str, Any] | None:
        if len(group) < 2:
            return best

        score = sum(device.score for device in group) + len(group) * 10
        candidate = {
            "devices": group,
            "offset": group[0].offset,
            "stride": group[0].layout.size,
            "count": len(group),
            "score": score,
        }
        if best is None or candidate["score"] > best["score"]:
            return candidate
        return best

    def _scan_init_entries(
        self,
        ptr_size: int,
        endian: str,
        base: int,
        device_by_address: dict[int, DeviceCandidate],
    ) -> tuple[list[dict[str, Any]], RegionGroup | None]:
        entry_size = ptr_size * 2
        candidates: list[dict[str, Any]] = []

        for offset in range(0, len(self.data) - entry_size + 1, ptr_size):
            init_ptr = read_ptr(self.data, offset, ptr_size, endian)
            device_ptr = read_ptr(self.data, offset + ptr_size, ptr_size, endian)
            if init_ptr is None or device_ptr is None:
                continue
            device = device_by_address.get(device_ptr)
            if device is None:
                continue
            if init_ptr != 0 and not self._pointer_inside_image(base, init_ptr):
                continue
            candidates.append(
                {
                    "offset": offset,
                    "address": base + offset,
                    "init": init_ptr,
                    "device_handle": device.handle,
                    "device_name": device.name,
                }
            )

        group = self._best_record_group(candidates, entry_size, "init_entries")
        if group is None:
            return candidates, None

        start = group.offset
        stop = start + group.count * group.stride
        chosen = [entry for entry in candidates if start <= entry["offset"] < stop]
        return chosen, group

    def _best_record_group(
        self,
        entries: list[dict[str, Any]],
        stride: int,
        kind: str,
    ) -> RegionGroup | None:
        if not entries:
            return None

        ordered = sorted(entries, key=lambda item: item["offset"])
        best: RegionGroup | None = None
        current = [ordered[0]]

        for entry in ordered[1:]:
            previous = current[-1]
            if entry["offset"] == previous["offset"] + stride:
                current.append(entry)
                continue

            if len(current) >= 2:
                group = RegionGroup(kind, current[0]["offset"], stride, len(current), len(current))
                if best is None or group.score > best.score:
                    best = group
            current = [entry]

        if len(current) >= 2:
            group = RegionGroup(kind, current[0]["offset"], stride, len(current), len(current))
            if best is None or group.score > best.score:
                best = group

        return best

    def _parse_deps(self, base: int, address: int, endian: str) -> dict[str, Any] | None:
        offset = self._image_offset(base, address)
        if offset is None:
            return None

        values: list[int] = []
        cursor = offset
        limit = min(len(self.data), offset + 2 * 128)

        while cursor + 2 <= limit:
            value = read_s16(self.data, cursor, endian)
            if value is None:
                return None
            values.append(value)
            cursor += 2
            if value == DEVICE_DEPS_ENDS:
                break
        else:
            return None

        if values.count(DEVICE_DEPS_SEP) > 2 or values[-1] != DEVICE_DEPS_ENDS:
            return None

        groups: list[list[int]] = [[]]
        for value in values[:-1]:
            if value == DEVICE_DEPS_SEP:
                groups.append([])
                continue
            groups[-1].append(value)

        while len(groups) < 3:
            groups.append([])

        return {
            "offset": offset,
            "address": address,
            "required": groups[0],
            "injected": groups[1],
            "supported": groups[2],
        }

    def _parse_dt_meta(
        self,
        base: int,
        address: int,
        ptr_size: int,
        endian: str,
    ) -> dict[str, Any] | None:
        meta_offset = self._image_offset(base, address)
        if meta_offset is None:
            return None

        nodelabel_struct_ptr = read_ptr(self.data, meta_offset, ptr_size, endian)
        if nodelabel_struct_ptr is None:
            return None

        labels_offset = self._image_offset(base, nodelabel_struct_ptr)
        if labels_offset is None:
            return None

        count = read_int(self.data, labels_offset, ptr_size, endian)
        if count is None or count == 0 or count > 16:
            return None

        labels: list[str] = []
        cursor = labels_offset + ptr_size
        for _ in range(count):
            label_ptr = read_ptr(self.data, cursor, ptr_size, endian)
            if label_ptr is None:
                return None
            label = self._string_from_pointer(base, label_ptr)
            if label is None or not probable_nodelabel(label):
                return None
            labels.append(label)
            cursor += ptr_size

        return {
            "offset": meta_offset,
            "address": address,
            "nodelabels_offset": labels_offset,
            "nodelabels_address": nodelabel_struct_ptr,
            "nodelabels": labels,
        }

    def _resolve_handles(self, deps: dict[str, Any], devices: list[DeviceCandidate]) -> None:
        device_by_handle = {
            device.handle: device.name
            for device in devices
            if device.handle is not None
        }
        for key in ("required", "injected", "supported"):
            resolved = []
            for handle in deps[key]:
                resolved.append(
                    {
                        "handle": handle,
                        "name": device_by_handle.get(handle),
                    }
                )
            deps[f"{key}_resolved"] = resolved

    def _collect_devicetree_strings(
        self,
        devices: list[DeviceCandidate] | None = None,
    ) -> dict[str, Any]:
        nodelabels = {value for _, value in self.strings if probable_nodelabel(value)}
        compatibles = {value for _, value in self.strings if probable_compatible(value)}
        paths = {value for _, value in self.strings if probable_dt_path(value)}

        if devices is not None:
            for device in devices:
                if device.dt_meta is not None:
                    nodelabels.update(device.dt_meta["nodelabels"])

        return {
            "nodelabels": sorted(nodelabels),
            "compatibles": sorted(compatibles),
            "paths": sorted(paths),
        }

    def _device_to_dict(self, device: DeviceCandidate) -> dict[str, Any]:
        return {
            "handle": device.handle,
            "name": device.name,
            "offset": device.offset,
            "address": device.address,
            "layout": {
                "name": device.layout.name,
                "size": device.layout.size,
                "has_deinit": device.layout.has_deinit,
                "has_deps": device.layout.has_deps,
                "has_pm": device.layout.has_pm,
                "has_dt_meta": device.layout.has_dt_meta,
            },
            "flags": device.flags,
            "config": device.config,
            "api": device.api,
            "state": device.state,
            "data": device.data,
            "init": device.init,
            "deinit": device.deinit,
            "deps": device.deps,
            "pm": device.pm,
            "devicetree": device.dt_meta,
            "score": device.score,
        }

    def _image_offset(self, base: int, pointer: int) -> int | None:
        offset = pointer - base
        if 0 <= offset < len(self.data):
            return offset
        return None

    def _pointer_inside_image(self, base: int, pointer: int) -> bool:
        return self._image_offset(base, pointer) is not None

    def _string_from_pointer(self, base: int, pointer: int) -> str | None:
        offset = self._image_offset(base, pointer)
        if offset is None:
            return None
        return self.strings_by_offset.get(offset)


def analyze_binary(
    path: Path,
    *,
    pointer_size: int | None = None,
    endianness: str | None = None,
    base: int | None = None,
) -> dict[str, Any]:
    analyzer = ZephyrBinaryAnalyzer(
        path.read_bytes(),
        pointer_size=pointer_size,
        endianness=endianness,
        base=base,
    )
    return analyzer.analyze()


def format_text(result: dict[str, Any]) -> str:
    lines = [
        f"input_size: {result['input_size']}",
    ]

    fmt = result.get("format")
    if fmt is not None:
        lines.append(
            "format: "
            f"pointer_size={fmt['pointer_size']} "
            f"endianness={fmt['endianness']} "
            f"base=0x{fmt['base']:x}"
        )

    lines.append(f"devices: {len(result['devices'])}")
    for device in result["devices"]:
        line = (
            f"  - handle={device['handle']} "
            f"name={device['name']} "
            f"addr=0x{device['address']:x} "
            f"layout={device['layout']['name']}"
        )
        lines.append(line)
        if device["devicetree"] is not None:
            labels = ",".join(device["devicetree"]["nodelabels"])
            lines.append(f"    nodelabels={labels}")

    lines.append(f"iterable_regions: {len(result['iterable_regions'])}")
    for region in result["iterable_regions"]:
        lines.append(
            "  - "
            f"{region['kind']} "
            f"addr=0x{region['address']:x} "
            f"entry_size={region['entry_size']} "
            f"count={region['count']}"
        )

    lines.append(f"init_entries: {len(result['init_entries'])}")
    for entry in result["init_entries"]:
        lines.append(
            "  - "
            f"addr=0x{entry['address']:x} "
            f"init=0x{entry['init']:x} "
            f"device={entry['device_name']}"
        )

    dt = result["devicetree"]
    lines.append("devicetree:")
    lines.append(f"  nodelabels: {', '.join(dt['nodelabels'])}")
    lines.append(f"  compatibles: {', '.join(dt['compatibles'])}")
    lines.append(f"  paths: {', '.join(dt['paths'])}")

    for warning in result.get("warnings", []):
        lines.append(f"warning: {warning}")

    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    parser.add_argument("binary", type=Path, help="Input raw zephyr.bin file")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Optional output file. Defaults to stdout.",
    )
    parser.add_argument(
        "-f",
        "--format",
        choices=("json", "text"),
        default="json",
        help="Output format",
    )
    parser.add_argument(
        "--base",
        type=parse_int,
        help="Override the inferred linked flash base address",
    )
    parser.add_argument(
        "--pointer-size",
        type=int,
        choices=(4, 8),
        help="Override the inferred pointer size",
    )
    parser.add_argument(
        "--endianness",
        choices=("little", "big"),
        help="Override the inferred endianness",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    result = analyze_binary(
        args.binary,
        pointer_size=args.pointer_size,
        endianness=args.endianness,
        base=args.base,
    )

    if args.format == "json":
        text = json.dumps(result, indent=2, sort_keys=True)
    else:
        text = format_text(result)

    if args.output is None:
        sys.stdout.write(f"{text}\n")
    else:
        args.output.write_text(f"{text}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
