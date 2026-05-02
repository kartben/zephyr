#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
zephyr_re.py - reverse-engineer a Zephyr firmware binary.

Given an arbitrary Zephyr-built ARM Cortex-M binary (no DTS, no ELF, no map
file), recover as much build/structural information as possible:

  * container format (raw .bin, MCUboot, custom wrappers)
  * load address (auto-detected by self-consistency scoring)
  * Cortex-M vector table (MSP, reset, NMI/HF/SVC/PendSV/SysTick, IRQs)
  * Zephyr/NCS version banner & build SHA
  * "string-only" devicetree heuristics: name@hex peripherals, settings paths,
    BT/log/shell strings, errno table location
  * iterable-section content via structural fingerprinting in rodata:
      - struct device array     (name, config, api, state pointers)
      - init_entry array        (init_fn, dev) -> init order
      - _static_thread_data     (stack, prio, entry_fn, name)
      - log_const               (module_name, level)
      - shell_static_entry      (syntax, help, subcmd, handler)
      - settings_handler_static (path, handlers)
  * (stretch) MOVW/MOVT pointer reconstructor that walks the .text region
    and reassembles 32-bit immediates loaded into registers as MOVW;...;MOVT
    pairs - this is necessary because GCC for ARMv7-M typically does not
    emit literal pools, so absolute pointer references from code are not
    visible as raw 32-bit words.

The script is pure Python 3 stdlib and self-contained. CLI:

    zephyr_re.py FW [--load-addr 0xN] [--header-size N]
                    [--ram-base 0x20000000] [--ram-size 0x20000]
                    [--phase A|B|C] [--json out.json] [-v]

(C) 2026 - placed under Apache-2.0 to match the surrounding zephyr/scripts.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from dataclasses import dataclass, field, asdict
from typing import Callable


# --- Section: container detection -------------------------------------------

@dataclass
class Wrapper:
    name: str
    probe: Callable[[bytes], bool]
    parse: Callable[[bytes], tuple[int, int]]   # (header_size, payload_size)


def _u32le(b: bytes, off: int) -> int:
    return struct.unpack_from("<I", b, off)[0]


def _parse_zephyr_custom(buf: bytes) -> tuple[int, int]:
    payload_size = _u32le(buf, 4)
    return 32, payload_size


def _parse_mcuboot(buf: bytes) -> tuple[int, int]:
    # struct image_header { magic, load_addr, hdr_size, protect_tlv_size,
    #                       img_size, flags, version, _pad1 }
    hdr_size = struct.unpack_from("<H", buf, 8)[0]
    img_size = _u32le(buf, 12)
    return hdr_size, img_size


def _parse_raw(buf: bytes) -> tuple[int, int]:
    return 0, len(buf)


WRAPPERS: list[Wrapper] = [
    Wrapper("zephyr_custom_67_64",
            probe=lambda b: len(b) >= 8 and b[:4] == b"\x67\x64\xd8\xd2",
            parse=_parse_zephyr_custom),
    Wrapper("mcuboot",
            probe=lambda b: len(b) >= 32 and _u32le(b, 0) == 0x96f3b83d,
            parse=_parse_mcuboot),
    Wrapper("raw",
            probe=lambda b: True,
            parse=_parse_raw),
]


def detect_container(buf: bytes, override_hdr: int | None) -> tuple[str, int, int]:
    if override_hdr is not None:
        return ("manual_override", override_hdr, len(buf) - override_hdr)
    for w in WRAPPERS:
        if w.probe(buf):
            hdr, plen = w.parse(buf)
            if hdr + plen <= len(buf):
                return (w.name, hdr, plen)
    return ("raw", 0, len(buf))


# --- Section: Cortex-M vector table -----------------------------------------

ARMV7M_EXC_NAMES = [
    "SP_initial", "Reset", "NMI", "HardFault", "MemManage", "BusFault",
    "UsageFault", "Reserved7", "Reserved8", "Reserved9", "Reserved10",
    "SVCall", "DebugMon", "Reserved13", "PendSV", "SysTick",
]


@dataclass
class VectorTable:
    msp: int
    reset: int
    exceptions: dict[str, int]
    irq_count: int
    irq_handlers: list[int]


def parse_vector_table(payload: bytes, ram_base: int, ram_size: int,
                       load_addr: int) -> VectorTable | None:
    if len(payload) < 64:
        return None
    msp = _u32le(payload, 0)
    reset = _u32le(payload, 4)
    if not (ram_base <= msp <= ram_base + ram_size):
        return None
    if not (reset & 1):
        return None
    if not (load_addr <= (reset & ~1) < load_addr + len(payload)):
        return None
    exc = {}
    for i, name in enumerate(ARMV7M_EXC_NAMES):
        exc[name] = _u32le(payload, 4 * i)
    # IRQ handlers follow exception 15 (SysTick); count until we exit thumb-flag
    # range or hit the start of code.
    irqs: list[int] = []
    i = 16
    last_thumb = None
    while 4 * i + 4 <= len(payload) and 4 * i < 0x400:
        w = _u32le(payload, 4 * i)
        if w == 0:
            irqs.append(0)
            i += 1
            continue
        if not (w & 1):
            break
        if not (load_addr <= (w & ~1) < load_addr + len(payload)):
            break
        # Detect end-of-table heuristic: when we cross into code itself.
        if last_thumb is not None and 4 * i >= (last_thumb & ~1) - load_addr:
            break
        irqs.append(w)
        last_thumb = w if last_thumb is None else min(last_thumb, w)
        i += 1
    return VectorTable(msp=msp, reset=reset & ~1, exceptions=exc,
                       irq_count=len(irqs), irq_handlers=irqs)


# --- Section: load address auto-detect --------------------------------------

LOAD_CANDIDATES = [
    0x00000000,   # nRF, RP2040 XIP, generic
    0x0000C000,   # MCUboot slot1 on nRF
    0x00010000,   # nRF NCS app slot
    0x08000000,   # STM32 flash
    0x08020000,   # STM32 secondary
    0x10000000,   # NXP / RP2040 alt
    0x60000000,   # external XIP
]


def score_load_addr(payload: bytes, la: int, ram_base: int,
                    ram_size: int) -> int:
    n = len(payload)
    score = 0
    # Cheap sample: every 16 words.
    for off in range(0, (n // 4) * 4, 16):
        w = _u32le(payload, off)
        if w == 0:
            continue
        if la <= (w & ~1) < la + n:
            score += 1
        elif ram_base <= w < ram_base + ram_size:
            score += 1
    return score


def detect_load_addr(payload: bytes, override: int | None,
                     ram_base: int, ram_size: int) -> tuple[int, int]:
    if override is not None:
        return override, score_load_addr(payload, override, ram_base, ram_size)
    best = (0, -1)
    for la in LOAD_CANDIDATES:
        # Quick reject: vector table won't validate.
        if len(payload) < 8:
            continue
        msp = _u32le(payload, 0)
        if not (ram_base <= msp <= ram_base + ram_size):
            continue
        reset = _u32le(payload, 4)
        if not (reset & 1):
            continue
        if not (la <= (reset & ~1) < la + len(payload)):
            continue
        s = score_load_addr(payload, la, ram_base, ram_size)
        if s > best[1]:
            best = (la, s)
    if best[1] < 0:
        return 0x0, 0
    return best


# --- Section: FlashImage central object -------------------------------------

class FlashImage:
    """Wraps the raw payload bytes plus address-space knowledge."""

    def __init__(self, payload: bytes, load_addr: int,
                 ram_base: int, ram_size: int,
                 reset_handler: int | None = None):
        self.payload = payload
        self.load_addr = load_addr
        self.ram_base = ram_base
        self.ram_size = ram_size
        self.flash_lo = load_addr
        self.flash_hi = load_addr + len(payload)
        self.ram_lo = ram_base
        self.ram_hi = ram_base + ram_size
        # Heuristic: text spans [load_addr, reset_handler upper bound + N).
        # We'll refine this once we've enumerated text-resident addresses.
        self.text_lo = load_addr
        self.text_hi = load_addr + len(payload)  # refined later
        self.rodata_lo = load_addr               # refined later
        self.rodata_hi = load_addr + len(payload)
        if reset_handler is not None:
            self.text_lo = min(self.text_lo, reset_handler)

    # -- byte-level access in flash address space --
    def in_flash(self, a: int) -> bool:
        return self.flash_lo <= a < self.flash_hi

    def in_ram(self, a: int) -> bool:
        return self.ram_lo <= a < self.ram_hi

    def in_text(self, a: int) -> bool:
        a &= ~1
        return self.text_lo <= a < self.text_hi

    def in_rodata(self, a: int) -> bool:
        return self.rodata_lo <= a < self.rodata_hi

    def addr_to_off(self, a: int) -> int:
        return a - self.load_addr

    def read_u32(self, addr: int) -> int | None:
        off = self.addr_to_off(addr)
        if off < 0 or off + 4 > len(self.payload):
            return None
        return _u32le(self.payload, off)

    def read_byte(self, addr: int) -> int | None:
        off = self.addr_to_off(addr)
        if off < 0 or off >= len(self.payload):
            return None
        return self.payload[off]

    def read_cstr(self, addr: int, maxlen: int = 96) -> str | None:
        off = self.addr_to_off(addr)
        if off < 0 or off >= len(self.payload):
            return None
        e = off
        end = min(off + maxlen, len(self.payload))
        while e < end and self.payload[e] != 0:
            b = self.payload[e]
            if b < 0x09 or b == 0x7f:
                return None
            e += 1
        if e == off or e >= len(self.payload) or self.payload[e] != 0:
            return None
        try:
            return self.payload[off:e].decode("ascii")
        except UnicodeDecodeError:
            return None

    def is_cstr_start(self, addr: int, minlen: int = 1) -> bool:
        """True if addr is the start of a C-string in flash."""
        off = self.addr_to_off(addr)
        if off < 0 or off >= len(self.payload):
            return False
        # Must be preceded by NUL (or be at the start of the image).
        if off > 0 and self.payload[off - 1] != 0:
            return False
        s = self.read_cstr(addr)
        return s is not None and len(s) >= minlen

    # -- word classifier --
    def classify_word(self, w: int) -> str:
        """Classify a 32-bit word as one of: null, ram, thumb, rodata,
        flash_data, text, int, unk.

        ``rodata``     - address inside the .rodata string blob.
        ``flash_data`` - address in flash but BEFORE the string blob
                         (data sections like _device_list, init arrays,
                         api/config structs typically land here).
        ``thumb``      - odd address that lands inside .text.
        """
        if w == 0:
            return "null"
        if self.ram_lo <= w < self.ram_hi:
            return "ram"
        if self.in_flash(w):
            if w >= self.rodata_lo:
                return "rodata"
            if (w & 1) and self.in_text(w & ~1):
                return "thumb"
            # In flash, even, below rodata strings: data/struct region
            # placed by linker between .text and the strings blob, e.g.
            # ._device_list, ._init.PRE_KERNEL_1.0, log_const, etc.
            # Sub-256 values are almost always integer fields, not pointers.
            if w >= 0x100:
                return "flash_data"
            return "int"
        if w < 0x10000:
            return "int"
        return "unk"


# --- Section: string table builder ------------------------------------------

def find_rodata_boundary(strings: dict[int, str], img: FlashImage,
                         min_addr: int = 0) -> int:
    """Detect the start of the .rodata string blob.

    True .rodata is a dense run of NUL-terminated strings packed back-to-back.
    Stray "strings" inside .text (e.g. byte sequences in jump tables that
    happen to look ASCII) are sparse and short. We require:
      * the candidate string is at or above ``min_addr`` (caller passes the
        reset handler address so we never claim rodata starts inside the
        vector table or early code);
      * at least 8 *long* (>= 6 char) strings start within the next 1 KiB.
    """
    if not strings:
        return img.flash_lo
    long_addrs = sorted(a for a, s in strings.items()
                        if a >= min_addr and len(s) >= 6)
    for i, a in enumerate(long_addrs):
        n = 0
        j = i
        while j < len(long_addrs) and long_addrs[j] < a + 1024:
            n += 1
            j += 1
        if n >= 8:
            return a
    if long_addrs:
        return long_addrs[0]
    return min(strings)


def build_string_table(img: FlashImage, min_len: int = 3) -> dict[int, str]:
    """Extract NUL-terminated ASCII strings.

    Allows printable, plus \\t \\n \\r inside the string (Zephyr banner ends
    with \\n before the NUL terminator, for example).
    """
    out: dict[int, str] = {}
    buf = img.payload
    n = len(buf)

    def printable(b: int) -> bool:
        return (0x20 <= b < 0x7f) or b in (0x09, 0x0a, 0x0d)

    i = 0
    while i < n:
        if 0x20 <= buf[i] < 0x7f:           # require start with a visible char
            j = i
            while j < n and printable(buf[j]):
                j += 1
            if j - i >= min_len and j < n and buf[j] == 0:
                out[img.load_addr + i] = buf[i:j].decode("ascii", "replace").rstrip()
            i = j + 1
        else:
            i += 1
    return out


# --- Section: Zephyr signature scanner --------------------------------------

@dataclass
class ZephyrInfo:
    is_zephyr: bool = False
    zephyr_version: str | None = None
    zephyr_commit: str | None = None
    ncs_version: str | None = None
    banner_addr: int | None = None
    fatal_addr: int | None = None
    panic_addr: int | None = None
    oops_addr: int | None = None
    errno_table_addr: int | None = None


# Match e.g. "*** Booting Zephyr OS build v4.0.0 ***",
#           "*** Using Zephyr OS v3.7.99-93ba569c5b31 ***",
#           "*** Booting Zephyr OS build v3.4.0-rc2-7-g5fa92a01ad08 ***".
_BANNER_RE_ZEPHYR = re.compile(
    r"\*\*\* (?:Booting|Using) Zephyr OS (?:build\s+)?v?"
    r"([0-9][0-9a-zA-Z._+-]*?)"            # version - non-greedy
    r"(?:-(?:g)?([0-9a-f]{7,40}))?"        # optional -gSHA or -SHA
    r"\s*\*\*\*"
)
_BANNER_RE_NCS = re.compile(
    r"\*\*\* Using nRF Connect SDK (?:build\s+)?v?([^\s*]+)\s*\*\*\*"
)


def scan_zephyr(img: FlashImage, strings: dict[int, str]) -> ZephyrInfo:
    info = ZephyrInfo()
    for addr, s in strings.items():
        m = _BANNER_RE_ZEPHYR.search(s)
        if m:
            info.is_zephyr = True
            info.zephyr_version = m.group(1)
            info.zephyr_commit = m.group(2)
            info.banner_addr = addr
        m = _BANNER_RE_NCS.search(s)
        if m:
            info.ncs_version = m.group(1)
        if "ZEPHYR FATAL ERROR" in s:
            info.fatal_addr = addr
            info.is_zephyr = True
        if s == "Kernel panic":
            info.panic_addr = addr
        if s == "Kernel oops":
            info.oops_addr = addr

    # Errno table: look for the canonical "Not owner" string immediately
    # followed by "No such file or directory" in flash.
    a_no = next((a for a, s in strings.items() if s == "Not owner"), None)
    if a_no is not None:
        info.errno_table_addr = a_no
    return info


# --- Section: DT-style harvester --------------------------------------------

DT_NODE_RE = re.compile(r"^([a-z][a-z0-9_-]*)@([0-9a-f]+)$")
SETTINGS_PATH_RE = re.compile(r"^[a-z][a-z0-9_/-]*$")
DRIVER_MODULE_RE = re.compile(r"^[a-z][a-z0-9_]*_(nrfx|nrf|nrf5|stm32|esp32|nxp|cc13xx|cc26xx|sam|sama|rpi|max|sifive|gd32)[_a-z0-9]*$")


@dataclass
class DTPeripheral:
    name: str
    addr: int
    bus: str
    flash_addr: int


def harvest_dt(strings: dict[int, str]) -> tuple[list[DTPeripheral], list[DTPeripheral], list[str]]:
    mmio: list[DTPeripheral] = []
    busc: list[DTPeripheral] = []
    drivers: set[str] = set()
    for addr, s in strings.items():
        m = DT_NODE_RE.match(s)
        if m:
            name, hexaddr = m.group(1), m.group(2)
            try:
                a = int(hexaddr, 16)
            except ValueError:
                continue
            bus = "mmio" if a >= 0x40000000 else "i2c-or-other"
            p = DTPeripheral(name=s, addr=a, bus=bus, flash_addr=addr)
            (mmio if bus == "mmio" else busc).append(p)
        if DRIVER_MODULE_RE.match(s):
            drivers.add(s)
    mmio.sort(key=lambda x: x.addr)
    busc.sort(key=lambda x: x.addr)
    return mmio, busc, sorted(drivers)


def harvest_settings(strings: dict[int, str]) -> list[tuple[int, str]]:
    out = []
    for addr, s in strings.items():
        if "/" not in s:
            continue
        if not s.startswith(("settings/", "bt/", "fs/", "nvs/", "cal/", "lwm2m/")):
            continue
        if " " in s or len(s) < 4 or len(s) > 64:
            continue
        out.append((addr, s))
    out.sort(key=lambda x: x[1])
    return out


# --- Section: iterable-section fingerprint engine ---------------------------

@dataclass
class FieldSpec:
    name: str
    classes: tuple[str, ...]      # any of: cstr, rodata, ram, thumb, null, int_in
    allow_null: bool = False
    int_lo: int = 0
    int_hi: int = 0


@dataclass
class Fingerprint:
    kind: str
    strides: tuple[int, ...]
    fields: tuple[FieldSpec, ...]
    min_records: int
    description: str = ""


@dataclass
class FingerprintHit:
    kind: str
    base_addr: int
    stride: int
    count: int
    records: list[tuple[int, ...]]


def _field_ok(img: FlashImage, w: int, spec: FieldSpec) -> bool:
    if w == 0 and (spec.allow_null or "null" in spec.classes):
        return True
    cls = img.classify_word(w)
    # Signed view, used for int_in checks that accept negative values.
    w_signed = w if w < 0x80000000 else w - 0x100000000
    for c in spec.classes:
        if c == "cstr":
            if cls in ("rodata", "flash_data") and img.is_cstr_start(w):
                return True
        elif c == "rodata":
            # A "rodata" check accepts both real rodata strings and
            # general flash data (struct pointers).
            if cls in ("rodata", "flash_data"):
                return True
        elif c == "flash_data":
            if cls == "flash_data":
                return True
        elif c == "ram":
            if cls == "ram":
                return True
        elif c == "thumb":
            if cls == "thumb":
                return True
        elif c == "int_in":
            if spec.int_lo <= w_signed <= spec.int_hi:
                return True
        elif c == "null":
            if w == 0:
                return True
        elif c == "any":
            return True
    return False


def _record_ok(img: FlashImage, base: int, fp: Fingerprint) -> bool:
    for i, spec in enumerate(fp.fields):
        w = img.read_u32(base + 4 * i)
        if w is None:
            return False
        if not _field_ok(img, w, spec):
            return False
    return True


def scan_fingerprint(img: FlashImage, fp: Fingerprint) -> list[FingerprintHit]:
    hits: list[FingerprintHit] = []
    payload = img.payload
    n = len(payload)
    # Iterable sections (devices, init_entry, log_const, ...) are placed by
    # the linker in dedicated subsections that land BETWEEN .text and the
    # bulk string blob in .rodata. So we cannot restrict the scan to addr
    # >= rodata_lo - we must search anything past the vector table.
    scan_start = 0x100   # past a typical 256-byte vector table
    end_off = n - max(fp.strides) * fp.min_records
    for stride in fp.strides:
        if stride < 4 * len(fp.fields):
            continue
        off = scan_start
        # Round to 4.
        off = (off + 3) & ~3
        while off < end_off:
            base_addr = img.load_addr + off
            if _record_ok(img, base_addr, fp):
                # Extend run as long as records keep matching.
                run = 1
                while True:
                    nb = base_addr + run * stride
                    if img.addr_to_off(nb) + 4 * len(fp.fields) > n:
                        break
                    if not _record_ok(img, nb, fp):
                        break
                    run += 1
                if run >= fp.min_records:
                    recs = []
                    for k in range(run):
                        nb = base_addr + k * stride
                        recs.append(tuple(
                            img.read_u32(nb + 4 * i) or 0
                            for i in range(stride // 4)
                        ))
                    hits.append(FingerprintHit(
                        kind=fp.kind, base_addr=base_addr,
                        stride=stride, count=run, records=recs))
                    off += run * stride
                    continue
            off += 4
    return hits


def validate_device_hit(img: FlashImage, h: FingerprintHit) -> FingerprintHit | None:
    """A run is the device array only if state ptrs cluster tightly in RAM
    and are all distinct.

    The Zephyr linker packs `struct device_state` instances into a tight
    `.z_devstate` RAM region. Each device gets a unique state slot. The
    *order* in which the linker assigns slots does NOT match the order
    of devices in the rodata array, so we don't assume monotonicity --
    just clustering and uniqueness.
    """
    state_idx = 3   # state is at offset 12 = field[3] in both v37 and v40
    api_idx = 2
    states = [r[state_idx] for r in h.records]
    apis = [r[api_idx] for r in h.records]
    if not all(img.in_ram(s) for s in states):
        return None
    if len(set(states)) != len(states):
        return None
    smin, smax = min(states), max(states)
    if smax - smin > 0x200:
        return None
    # Steps between sorted values should be small (typically 2-4 bytes).
    sorted_s = sorted(states)
    steps = [b - a for a, b in zip(sorted_s, sorted_s[1:])]
    if steps and max(steps) > 32:
        return None
    # At least 30% of records must point at an api struct (in flash). Pure
    # virtual devices (api=NULL) exist but most real devices have one.
    nonnull_api = sum(1 for a in apis if a != 0 and img.in_flash(a))
    if nonnull_api / len(apis) < 0.3:
        return None
    return h


def validate_init_hit(img: FlashImage, h: FingerprintHit,
                      dev_array_ranges: list[tuple[int, int]]) -> FingerprintHit | None:
    """init_entry hit valid if it references known devices, or if we have no
    device array reference and the run is reassuringly long.

    Reject runs that are mostly NULLs - those are weak-symbol IRQ tables,
    not init arrays. Real init_entry records always have a non-NULL
    init_fn.
    """
    nonzero_fn = sum(1 for r in h.records if r[0] != 0)
    if nonzero_fn / max(h.count, 1) < 0.5:
        return None

    devs = [r[1] for r in h.records if r[1] != 0]
    if dev_array_ranges and devs:
        in_dev = sum(1 for d in devs
                     for lo, hi in dev_array_ranges
                     if lo <= d < hi)
        if in_dev / len(devs) >= 0.25:
            return h
    # Accept large, well-formed runs even without device references.
    return h if h.count >= 32 else None


def merge_hits(hits: list[FingerprintHit]) -> list[FingerprintHit]:
    if not hits:
        return []
    # Prefer largest count; on overlap drop shorter.
    hits = sorted(hits, key=lambda h: (-h.count, -h.stride, h.base_addr))
    kept: list[FingerprintHit] = []
    occupied: list[tuple[int, int]] = []
    for h in hits:
        a, b = h.base_addr, h.base_addr + h.count * h.stride
        if any(not (b <= s or a >= e) for s, e in occupied):
            continue
        kept.append(h)
        occupied.append((a, b))
    kept.sort(key=lambda h: h.base_addr)
    return kept


# --- Section: fingerprint table ---------------------------------------------

# Predicates used in the FINGERPRINTS list.
def _F(name, classes, allow_null=False, int_lo=0, int_hi=0):
    return FieldSpec(name, classes, allow_null, int_lo, int_hi)


FINGERPRINTS: list[Fingerprint] = [
    # The struct device fingerprint is permissive on field[0] (name): in some
    # NCS builds device names are stripped to the empty string and the
    # pointer simply points at a NUL byte. The discriminator we rely on is
    # field[3] - a RAM pointer that must cluster tightly across an array.
    # That clustering is enforced by a post-validation step (see
    # validate_device_array in main()).
    Fingerprint(
        kind="device_v37",
        strides=(20, 24, 28),
        fields=(
            # `name` may point past the loaded image when CONFIG_DEVICE_NAMES_OFF
            # collapses every empty string to a single shared "" in a stripped
            # part of .rodata. `api` may be NULL for purely-virtual devices.
            # The state-cluster validator below carries the discriminating
            # signal.
            _F("name",   ("any",)),
            _F("config", ("rodata", "null"), allow_null=True),
            _F("api",    ("rodata", "null"), allow_null=True),
            _F("state",  ("ram",)),
            _F("data",   ("ram", "rodata", "null"), allow_null=True),
        ),
        min_records=4,
        description="struct device (Zephyr 3.x layout)",
    ),
    Fingerprint(
        kind="device_v40",
        strides=(28, 32, 36, 40),
        fields=(
            _F("name",   ("any",)),
            _F("config", ("rodata", "null"), allow_null=True),
            _F("api",    ("rodata", "null"), allow_null=True),
            _F("state",  ("ram",)),
            _F("data",   ("ram", "rodata", "null"), allow_null=True),
            _F("ops",    ("rodata", "null"), allow_null=True),
            _F("flags",  ("int_in", "null"), allow_null=True, int_lo=0, int_hi=0xFF),
        ),
        min_records=4,
        description="struct device (mainline)",
    ),
    # init_entry false-positives are common (any 8-byte pair of
    # thumb-ptr+rodata-ptr matches), so we require a high min_records and
    # post-validate that at least 1/3 of the dev fields land inside an
    # already-identified device array.
    Fingerprint(
        kind="init_entry",
        strides=(8,),
        fields=(
            _F("init_fn", ("thumb", "null"), allow_null=True),
            _F("dev",     ("rodata", "null"), allow_null=True),
        ),
        min_records=8,
        description="struct init_entry",
    ),
    Fingerprint(
        kind="static_thread_data",
        strides=(40, 44, 48),
        fields=(
            _F("init_thread", ("ram",)),
            _F("init_stack",  ("ram",)),
            _F("stack_size",  ("int_in",), int_lo=128, int_hi=0x10000),
            _F("entry_fn",    ("thumb",)),
            _F("p1", ("rodata", "ram", "null", "int_in"), allow_null=True,
               int_lo=0, int_hi=0xFFFF),
            _F("p2", ("rodata", "ram", "null", "int_in"), allow_null=True,
               int_lo=0, int_hi=0xFFFF),
            _F("p3", ("rodata", "ram", "null", "int_in"), allow_null=True,
               int_lo=0, int_hi=0xFFFF),
            _F("prio",    ("int_in",), int_lo=-32, int_hi=32),
            _F("options", ("int_in",), int_lo=0, int_hi=0xFFFF),
            _F("delay",   ("int_in",), int_lo=-1, int_hi=0x7FFFFFFF),
        ),
        min_records=2,
        description="struct _static_thread_data",
    ),
    Fingerprint(
        kind="log_const",
        strides=(8,),
        fields=(
            _F("name",   ("cstr",)),
            _F("level",  ("int_in",), int_lo=0, int_hi=4),
        ),
        min_records=8,
        description="struct log_source_const_data",
    ),
    Fingerprint(
        kind="shell_static_entry",
        strides=(20, 24),
        fields=(
            _F("syntax",  ("cstr",)),
            _F("help",    ("cstr", "null"), allow_null=True),
            _F("subcmd",  ("rodata", "null"), allow_null=True),
            _F("handler", ("thumb", "null"), allow_null=True),
            _F("args",    ("int_in",), int_lo=0, int_hi=0xFFFF),
        ),
        min_records=3,
        description="struct shell_static_entry",
    ),
    Fingerprint(
        kind="settings_handler_static",
        strides=(24, 28),
        fields=(
            _F("path", ("cstr",)),
            _F("h_get",    ("thumb", "null"), allow_null=True),
            _F("h_set",    ("thumb", "null"), allow_null=True),
            _F("h_commit", ("thumb", "null"), allow_null=True),
            _F("h_export", ("thumb", "null"), allow_null=True),
        ),
        min_records=2,
        description="struct settings_handler_static",
    ),
]


# --- Section: per-type post-processors --------------------------------------

@dataclass
class DeviceInfo:
    addr: int
    name: str
    config: int
    api: int
    state: int
    data: int


@dataclass
class DriverGroup:
    api_addr: int
    devices: list[DeviceInfo]
    likely_kind: str = ""


def post_devices(img: FlashImage, hit: FingerprintHit) -> tuple[list[DeviceInfo], list[DriverGroup]]:
    out: list[DeviceInfo] = []
    for k, rec in enumerate(hit.records):
        addr = hit.base_addr + k * hit.stride
        name_ptr = rec[0]
        name = img.read_cstr(name_ptr)
        if not name:
            name = "(unnamed)"
        out.append(DeviceInfo(
            addr=addr, name=name,
            config=rec[1] if len(rec) > 1 else 0,
            api=rec[2] if len(rec) > 2 else 0,
            state=rec[3] if len(rec) > 3 else 0,
            data=rec[4] if len(rec) > 4 else 0,
        ))
    by_api: dict[int, DriverGroup] = {}
    for d in out:
        g = by_api.setdefault(
            d.api, DriverGroup(api_addr=d.api, devices=[], likely_kind=""))
        g.devices.append(d)
    groups = sorted(by_api.values(), key=lambda g: -len(g.devices))
    return out, groups


def label_driver_groups(groups: list[DriverGroup],
                        mmio: list[DTPeripheral],
                        bus_children: list[DTPeripheral]) -> None:
    """Best-effort labeling of driver groups by matching device count to DT
    peripheral count.

    Heuristic - we cannot say which exact device is uart@40002000 without
    the api struct content - but we can say "this 3-device group is PWM"
    when DT lists 3 PWM nodes.
    """
    from collections import Counter
    kinds: Counter = Counter()
    for p in mmio:
        kind = p.name.split("@")[0]
        kinds[kind] += 1

    # First pass: groups with count > 1 take exact matches.
    used: set[str] = set()
    for g in sorted(groups, key=lambda g: -len(g.devices)):
        if g.api_addr == 0:
            g.likely_kind = "(virtual / api=NULL)"
            continue
        n = len(g.devices)
        candidates = [k for k, c in kinds.items() if c == n and k not in used]
        if candidates and n > 1:
            g.likely_kind = candidates[0]
            used.add(candidates[0])

    # Second pass: 1-device groups - tentatively assign one of the
    # remaining 1-instance DT kinds.
    singletons = [k for k, c in kinds.items() if c == 1 and k not in used]
    for g in sorted([g for g in groups
                     if not g.likely_kind and g.api_addr != 0
                     and len(g.devices) == 1],
                    key=lambda g: g.api_addr):
        if singletons:
            g.likely_kind = f"{singletons.pop(0)} (?)"


@dataclass
class InitInfo:
    addr: int
    init_fn: int
    dev_ptr: int
    dev_name: str | None


def post_inits(img: FlashImage, hit: FingerprintHit,
               devices: list[DeviceInfo]) -> list[InitInfo]:
    name_by_dev_addr = {d.addr: d.name for d in devices}
    out: list[InitInfo] = []
    for k, rec in enumerate(hit.records):
        addr = hit.base_addr + k * hit.stride
        init_fn, dev = rec[0], rec[1]
        out.append(InitInfo(
            addr=addr, init_fn=init_fn & ~1, dev_ptr=dev,
            dev_name=name_by_dev_addr.get(dev),
        ))
    return out


@dataclass
class ThreadInfo:
    addr: int
    stack: int
    stack_size: int
    entry_fn: int
    prio: int
    options: int
    delay: int


def post_threads(img: FlashImage, hit: FingerprintHit) -> list[ThreadInfo]:
    out: list[ThreadInfo] = []
    for k, rec in enumerate(hit.records):
        addr = hit.base_addr + k * hit.stride
        prio = rec[7]
        if prio & 0x80000000:
            prio -= 1 << 32
        out.append(ThreadInfo(
            addr=addr, stack=rec[1], stack_size=rec[2],
            entry_fn=rec[3] & ~1, prio=prio,
            options=rec[8], delay=rec[9],
        ))
    return out


@dataclass
class LogSrcInfo:
    addr: int
    name: str
    level: int


def post_log(img: FlashImage, hit: FingerprintHit) -> list[LogSrcInfo]:
    out: list[LogSrcInfo] = []
    for k, rec in enumerate(hit.records):
        addr = hit.base_addr + k * hit.stride
        n = img.read_cstr(rec[0]) or f"<@0x{rec[0]:x}>"
        out.append(LogSrcInfo(addr=addr, name=n, level=rec[1]))
    return out


@dataclass
class ShellCmdInfo:
    addr: int
    syntax: str
    help: str | None
    handler: int


def post_shell(img: FlashImage, hit: FingerprintHit) -> list[ShellCmdInfo]:
    out: list[ShellCmdInfo] = []
    for k, rec in enumerate(hit.records):
        addr = hit.base_addr + k * hit.stride
        syntax = img.read_cstr(rec[0]) or "?"
        help_s = img.read_cstr(rec[1]) if rec[1] else None
        out.append(ShellCmdInfo(addr=addr, syntax=syntax, help=help_s,
                                handler=rec[3] & ~1))
    return out


@dataclass
class SettingsHandlerInfo:
    addr: int
    path: str
    h_get: int
    h_set: int
    h_commit: int
    h_export: int


def post_settings(img: FlashImage, hit: FingerprintHit) -> list[SettingsHandlerInfo]:
    out: list[SettingsHandlerInfo] = []
    for k, rec in enumerate(hit.records):
        addr = hit.base_addr + k * hit.stride
        path = img.read_cstr(rec[0]) or "?"
        out.append(SettingsHandlerInfo(
            addr=addr, path=path,
            h_get=rec[1] & ~1, h_set=rec[2] & ~1,
            h_commit=rec[3] & ~1, h_export=rec[4] & ~1,
        ))
    return out


# --- Section: literal-pool inventory + MOVW/MOVT pairs (Phase C) ------------


def scan_literal_pool_pointers(img: FlashImage,
                               text_lo: int, text_hi: int) -> dict[int, list[int]]:
    """Inventory all 4-byte aligned 32-bit values in the text region that
    look like rodata/RAM pointers.

    These are predominantly literal-pool entries (32-bit constants
    referenced by `LDR Rd, [PC, #imm]`) - they hold every absolute address
    that the code needs (string pointers, struct pointers, MMIO bases,
    iterable-section start/end symbols).

    Returns ``{target_addr: [pc_addrs_holding_it]}``.
    """
    out: dict[int, list[int]] = {}
    p = img.payload
    off = max(0, text_lo - img.load_addr)
    end = min(len(p), text_hi - img.load_addr)
    off = (off + 3) & ~3
    while off + 4 <= end:
        w = struct.unpack_from("<I", p, off)[0]
        cls = img.classify_word(w)
        if cls in ("rodata", "ram"):
            out.setdefault(w, []).append(img.load_addr + off)
        off += 4
    return out


# --- Section: MOVW/MOVT pointer reconstructor (Phase C) ---------------------

def _thumb_is_32bit(h1: int) -> bool:
    """Thumb-2 length test: hw1 starts with 11101, 11110 or 11111 -> 32-bit."""
    top = (h1 >> 11) & 0x1F
    return top in (0x1D, 0x1E, 0x1F)


def _decode_movw_movt(h1: int, h2: int) -> tuple[int, int]:
    """Return (Rd, imm16) decoded from a MOVW/MOVT 32-bit Thumb-2 instruction."""
    i_   = (h1 >> 10) & 1
    imm4 = h1 & 0xF
    imm3 = (h2 >> 12) & 7
    rd   = (h2 >> 8) & 0xF
    imm8 = h2 & 0xFF
    imm16 = (imm4 << 12) | (i_ << 11) | (imm3 << 8) | imm8
    return rd, imm16


def reconstruct_movw_movt(img: FlashImage,
                          text_lo: int, text_hi: int) -> list[tuple[int, int, int]]:
    """Return (pc_of_movt, Rd, reconstructed_addr) for adjacent MOVW;MOVT pairs.

    We only report the MOST RELIABLE pattern - a MOVW followed immediately by
    a MOVT writing to the same Rd, with no instruction in between. Walking
    arbitrary distance with a per-register shadow gets fooled by data
    embedded in .text (literal pools for VLDR), so we trade recall for
    precision.

    MOVW (T3): hw1 = 1111 0i 100100 imm4 ;  hw2 = 0 imm3 Rd imm8
    MOVT (T1): hw1 = 1111 0i 101100 imm4 ;  hw2 = 0 imm3 Rd imm8
    """
    out: list[tuple[int, int, int]] = []
    p = img.payload
    off = max(0, text_lo - img.load_addr)
    end = min(len(p), text_hi - img.load_addr) - 8
    while off <= end:
        h1 = struct.unpack_from("<H", p, off)[0]
        if (h1 & 0xFBF0) == 0xF240:                          # MOVW
            h2 = struct.unpack_from("<H", p, off + 2)[0]
            h3 = struct.unpack_from("<H", p, off + 4)[0]
            if (h3 & 0xFBF0) == 0xF2C0:                       # then MOVT
                h4 = struct.unpack_from("<H", p, off + 6)[0]
                rd_lo, lo = _decode_movw_movt(h1, h2)
                rd_hi, hi = _decode_movw_movt(h3, h4)
                if rd_lo == rd_hi:
                    addr = (hi << 16) | lo
                    out.append((img.load_addr + off + 4, rd_hi, addr))
                    off += 8
                    continue
            off += 4   # MOVW alone (16-bit constant) - skip past it
            continue
        # Step by Thumb-2 instruction length to stay aligned with the
        # instruction stream. This still mis-aligns inside literal pools,
        # but the strict "MOVW immediately followed by MOVT" predicate
        # filters out almost all spurious matches.
        is_32 = _thumb_is_32bit(h1)
        off += 4 if is_32 else 2
    return out


# --- Section: report --------------------------------------------------------

@dataclass
class Report:
    file: str
    file_size: int
    container: dict
    payload_size: int
    load_addr: int
    ram_base: int
    ram_size: int
    vector_table: dict | None
    zephyr: dict
    strings_summary: dict
    dt_peripherals_mmio: list[dict]
    dt_peripherals_bus: list[dict]
    drivers_detected: list[str]
    settings_paths: list[dict]
    iterables: list[dict]
    devices: list[dict] = field(default_factory=list)
    driver_groups: list[dict] = field(default_factory=list)
    inits: list[dict] = field(default_factory=list)
    threads: list[dict] = field(default_factory=list)
    log_sources: list[dict] = field(default_factory=list)
    shell_cmds: list[dict] = field(default_factory=list)
    settings_handlers: list[dict] = field(default_factory=list)
    movw_movt_constants_summary: dict | None = None
    notable_strings: list[dict] = field(default_factory=list)


def render_text(r: Report) -> str:
    lines: list[str] = []
    a = lines.append
    a(f"=== zephyr_re report: {r.file} ({r.file_size} bytes) ===\n")
    a("[Container]")
    a(f"  format        : {r.container['format']}")
    a(f"  header_size   : {r.container['header_size']}")
    a(f"  payload_size  : {r.payload_size} (0x{r.payload_size:x})")
    a(f"  load_addr     : 0x{r.load_addr:08x}")
    a(f"  ram_window    : [0x{r.ram_base:08x}, 0x{r.ram_base + r.ram_size:08x})")
    a("")
    if r.vector_table:
        vt = r.vector_table
        a("[Vector table]")
        a(f"  MSP_initial   : 0x{vt['msp']:08x}")
        a(f"  Reset         : 0x{vt['exceptions']['Reset'] & ~1:08x}")
        a(f"  NMI           : 0x{vt['exceptions']['NMI'] & ~1:08x}")
        a(f"  HardFault     : 0x{vt['exceptions']['HardFault'] & ~1:08x}")
        a(f"  SVCall        : 0x{vt['exceptions']['SVCall'] & ~1:08x}")
        a(f"  PendSV        : 0x{vt['exceptions']['PendSV'] & ~1:08x}")
        a(f"  SysTick       : 0x{vt['exceptions']['SysTick'] & ~1:08x}")
        a(f"  IRQ count     : {vt['irq_count']}")
        a("")
    a("[Zephyr signature]")
    z = r.zephyr
    a(f"  is_zephyr     : {z['is_zephyr']}")
    a(f"  zephyr_version: {z['zephyr_version']}")
    a(f"  zephyr_commit : {z['zephyr_commit']}")
    a(f"  ncs_version   : {z['ncs_version']}")
    if z['banner_addr']:
        a(f"  banner @      : 0x{z['banner_addr']:08x}")
    if z['fatal_addr']:
        a(f"  fatal @       : 0x{z['fatal_addr']:08x}")
    if z['errno_table_addr']:
        a(f"  errno table @ : 0x{z['errno_table_addr']:08x}")
    a("")
    a("[Strings]")
    a(f"  total strings : {r.strings_summary['count']}")
    a(f"  total bytes   : {r.strings_summary['bytes']}")
    a("")
    a(f"[DT peripherals: MMIO ({len(r.dt_peripherals_mmio)})]")
    for p in r.dt_peripherals_mmio:
        a(f"  {p['name']:30s} reg=0x{p['addr']:08x}")
    a("")
    a(f"[DT peripherals: bus children ({len(r.dt_peripherals_bus)})]")
    for p in r.dt_peripherals_bus:
        a(f"  {p['name']:30s} addr=0x{p['addr']:x}")
    a("")
    a(f"[Drivers detected from module strings ({len(r.drivers_detected)})]")
    for d in r.drivers_detected:
        a(f"  {d}")
    a("")
    a(f"[Settings paths ({len(r.settings_paths)})]")
    for s in r.settings_paths[:30]:
        a(f"  0x{s['addr']:08x}  {s['path']}")
    if len(r.settings_paths) > 30:
        a(f"  ... ({len(r.settings_paths) - 30} more)")
    a("")
    a(f"[Iterable sections found: {len(r.iterables)}]")
    for it in r.iterables:
        a(f"  {it['kind']:24s} stride={it['stride']:>2} "
          f"count={it['count']:>3} @ 0x{it['base_addr']:08x}")
    a("")
    if r.devices:
        a(f"[Devices: {len(r.devices)}]")
        for d in r.devices[:50]:
            a(f"  0x{d['addr']:08x}  {d['name']:40s} "
              f"api=0x{d['api']:08x} state=0x{d['state']:08x}")
        if len(r.devices) > 50:
            a(f"  ... ({len(r.devices) - 50} more)")
        a("")
    if r.driver_groups:
        a(f"[Driver groups (devices sharing api ptr): {len(r.driver_groups)}]")
        for g in r.driver_groups[:25]:
            label = g.get('likely_kind') or "?"
            count = len(g['devices'])
            a(f"  api=0x{g['api_addr']:08x}  {count:>2} dev(s)   guess: {label}")
        a("")
    if r.inits:
        a(f"[Init order: {len(r.inits)}]")
        for i in r.inits[:60]:
            tag = i['dev_name'] or "(SYS_INIT)"
            a(f"  init_fn=0x{i['init_fn']:08x}  dev=0x{i['dev_ptr']:08x}  {tag}")
        if len(r.inits) > 60:
            a(f"  ... ({len(r.inits) - 60} more)")
        a("")
    if r.threads:
        a(f"[Static threads: {len(r.threads)}]")
        for t in r.threads:
            a(f"  stack=0x{t['stack']:08x}({t['stack_size']:>5}B) "
              f"entry=0x{t['entry_fn']:08x} prio={t['prio']:>3} "
              f"opt=0x{t['options']:x} delay={t['delay']}")
        a("")
    if r.log_sources:
        a(f"[Log sources: {len(r.log_sources)}]")
        for ls in r.log_sources[:60]:
            a(f"  level={ls['level']}  {ls['name']}")
        if len(r.log_sources) > 60:
            a(f"  ... ({len(r.log_sources) - 60} more)")
        a("")
    if r.shell_cmds:
        a(f"[Shell commands: {len(r.shell_cmds)}]")
        for c in r.shell_cmds[:40]:
            help_s = (c['help'] or "")[:48]
            a(f"  {c['syntax']:20s} handler=0x{c['handler']:08x}  {help_s}")
        a("")
    if r.settings_handlers:
        a(f"[Settings handlers: {len(r.settings_handlers)}]")
        for h in r.settings_handlers:
            a(f"  {h['path']}")
        a("")
    if r.movw_movt_constants_summary:
        s = r.movw_movt_constants_summary
        a("[Code -> data references (literal pools + MOVW/MOVT)]")
        a(f"  total references : {s['total']}")
        a(f"  unique targets   : {s['unique']}")
        a(f"  into rodata      : {s['into_rodata']}")
        a(f"  MOVW/MOVT pairs  : {s.get('movw_movt_pairs', 0)}")
        a("  most-referenced targets:")
        for t in s['top'][:20]:
            a(f"    0x{t['addr']:08x}  refs={t['refs']:>3}  {t.get('preview','')}")
        a("")
    if r.notable_strings:
        a(f"[Notable strings ({len(r.notable_strings)})]")
        for ns in r.notable_strings:
            a(f"  0x{ns['addr']:08x}  {ns['str']}")
        a("")
    return "\n".join(lines)


# --- Section: main ----------------------------------------------------------

NOTABLE_PATTERNS = [
    re.compile(r"BUILD_TIME"),
    re.compile(r"GIT_SHA"),
    re.compile(r"^Starting "),
    re.compile(r"^Booting "),
    re.compile(r"firmware", re.I),
    re.compile(r"build", re.I),
]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Reverse-engineer a Zephyr firmware binary.")
    ap.add_argument("firmware", help="path to .bin / .fw")
    ap.add_argument("--load-addr", type=lambda x: int(x, 0), default=None)
    ap.add_argument("--header-size", type=int, default=None)
    ap.add_argument("--ram-base", type=lambda x: int(x, 0), default=0x20000000)
    ap.add_argument("--ram-size", type=lambda x: int(x, 0), default=0x40000)
    ap.add_argument("--phase", choices=["A", "B", "C"], default="C")
    ap.add_argument("--json", dest="json_out", default=None)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    raw = open(args.firmware, "rb").read()
    fmt, hdr, plen = detect_container(raw, args.header_size)
    payload = raw[hdr:hdr + plen]
    if args.verbose:
        print(f"[+] container: {fmt}, header={hdr}, payload={plen}")

    la, score = detect_load_addr(payload, args.load_addr,
                                 args.ram_base, args.ram_size)
    if args.verbose:
        print(f"[+] load addr: 0x{la:08x}  (score {score})")

    img = FlashImage(payload, la, args.ram_base, args.ram_size)
    vt = parse_vector_table(payload, args.ram_base, args.ram_size, la)
    if vt is not None:
        # .text typically starts right after the vector table. The
        # reset_handler address is NOT a lower bound - other functions can
        # be placed at lower addresses by the linker. We use a fixed
        # 0x100 offset (256-byte / 64-entry vector table) below which any
        # odd address is presumed to be raw vector-table data, not a
        # thumb function pointer.
        img.text_lo = la + 0x100

    strings = build_string_table(img)
    z = scan_zephyr(img, strings)

    # Refine text/rodata boundary using string density.
    if strings:
        # The reset handler is the canonical lower bound for "real" code, and
        # anything below it can't be the start of rodata.
        min_addr = vt.reset if vt else la
        img.rodata_lo = find_rodata_boundary(strings, img, min_addr)
        img.text_hi = img.rodata_lo
        if args.verbose:
            print(f"[+] rodata starts at 0x{img.rodata_lo:08x}")

    mmio, busc, drivers = harvest_dt(strings)
    settings_paths = harvest_settings(strings)

    # --- Phase B: structural fingerprinting ---
    iterables: list[FingerprintHit] = []
    if args.phase in ("B", "C"):
        # Pass 1: device-shaped fingerprints, validated by state-cluster
        # check. We need these first so init_entry validation can use the
        # device array's address range.
        device_hits: list[FingerprintHit] = []
        for fp in FINGERPRINTS:
            if fp.kind.startswith("device_"):
                for cand in scan_fingerprint(img, fp):
                    v = validate_device_hit(img, cand)
                    if v is not None:
                        device_hits.append(v)
        device_hits = merge_hits(device_hits)
        dev_ranges = [(h.base_addr, h.base_addr + h.count * h.stride)
                      for h in device_hits]

        # Pass 2: init_entry hits, validated against device array.
        init_hits: list[FingerprintHit] = []
        for fp in FINGERPRINTS:
            if fp.kind == "init_entry":
                for cand in scan_fingerprint(img, fp):
                    v = validate_init_hit(img, cand, dev_ranges)
                    if v is not None:
                        init_hits.append(v)
        init_hits = merge_hits(init_hits)

        # Pass 3: everything else.
        other_hits: list[FingerprintHit] = []
        for fp in FINGERPRINTS:
            if fp.kind.startswith("device_") or fp.kind == "init_entry":
                continue
            other_hits.extend(scan_fingerprint(img, fp))
        other_hits = merge_hits(other_hits)

        iterables = sorted(device_hits + init_hits + other_hits,
                           key=lambda h: h.base_addr)
        # Apply driver-group labeling once devices are post-processed.

    # Per-type post-processing.
    devices: list[DeviceInfo] = []
    driver_groups: list[DriverGroup] = []
    inits: list[InitInfo] = []
    threads: list[ThreadInfo] = []
    logs: list[LogSrcInfo] = []
    shells: list[ShellCmdInfo] = []
    settings_h: list[SettingsHandlerInfo] = []
    for h in iterables:
        if h.kind in ("device_v37", "device_v40"):
            d, g = post_devices(img, h)
            devices.extend(d)
            driver_groups.extend(g)
        elif h.kind == "init_entry":
            inits.extend(post_inits(img, h, devices))
        elif h.kind == "static_thread_data":
            threads.extend(post_threads(img, h))
        elif h.kind == "log_const":
            logs.extend(post_log(img, h))
        elif h.kind == "shell_static_entry":
            shells.extend(post_shell(img, h))
        elif h.kind == "settings_handler_static":
            settings_h.extend(post_settings(img, h))

    # Once all device hits have been merged, label the driver groups by
    # matching their member counts against DT peripheral counts.
    if driver_groups:
        # De-duplicate on api_addr (multiple device-array hits can yield
        # duplicate group entries).
        merged: dict[int, DriverGroup] = {}
        for g in driver_groups:
            mg = merged.get(g.api_addr)
            if mg is None:
                merged[g.api_addr] = g
            else:
                mg.devices.extend(g.devices)
        driver_groups = sorted(merged.values(), key=lambda g: -len(g.devices))
        label_driver_groups(driver_groups, mmio, busc)

    # --- Phase C: literal pool + MOVW/MOVT reconstruction ---
    movw_summary: dict | None = None
    if args.phase == "C" and vt is not None:
        text_lo = vt.reset & ~1
        text_hi = img.rodata_lo
        if text_hi <= text_lo:
            text_hi = la + len(payload)
        if args.verbose:
            print(f"[+] code-pointer scan: text 0x{text_lo:x}..0x{text_hi:x}")

        # Reliable: scan literal pools for absolute pointer constants.
        litpool = scan_literal_pool_pointers(img, text_lo, text_hi)
        # Best-effort: adjacent MOVW;MOVT pairs (rare in this codegen but
        # sometimes present).
        pairs = reconstruct_movw_movt(img, text_lo, text_hi)

        from collections import Counter
        # Combine reference counts.
        ctr: Counter[int] = Counter()
        for tgt, locs in litpool.items():
            ctr[tgt] += len(locs)
        for _, _, addr in pairs:
            ctr[addr] += 1
        into_rodata = sum(1 for a in ctr if img.in_rodata(a))

        top = []
        for addr, n in ctr.most_common(40):
            preview = ""
            if img.in_rodata(addr):
                s = img.read_cstr(addr)
                if s:
                    preview = f"-> rodata: {s!r}"
                else:
                    # Maybe it's a struct - report the first 4 bytes.
                    w0 = img.read_u32(addr)
                    preview = f"-> rodata struct/data (w0=0x{w0:x})" if w0 is not None else ""
            elif img.in_ram(addr):
                preview = "-> ram"
            top.append({"addr": addr, "refs": n, "preview": preview})
        movw_summary = {
            "total": sum(len(v) for v in litpool.values()) + len(pairs),
            "unique": len(ctr),
            "into_rodata": into_rodata,
            "movw_movt_pairs": len(pairs),
            "top": top,
        }

    # Notable strings.
    notable = []
    for addr, s in strings.items():
        if any(p.search(s) for p in NOTABLE_PATTERNS):
            notable.append({"addr": addr, "str": s})
    notable.sort(key=lambda x: x["addr"])

    # --- Build report ---
    report = Report(
        file=args.firmware, file_size=len(raw),
        container={"format": fmt, "header_size": hdr},
        payload_size=plen, load_addr=la,
        ram_base=args.ram_base, ram_size=args.ram_size,
        vector_table=asdict(vt) if vt else None,
        zephyr=asdict(z),
        strings_summary={
            "count": len(strings),
            "bytes": sum(len(s) + 1 for s in strings.values()),
        },
        dt_peripherals_mmio=[asdict(p) for p in mmio],
        dt_peripherals_bus=[asdict(p) for p in busc],
        drivers_detected=drivers,
        settings_paths=[{"addr": a, "path": p} for a, p in settings_paths],
        iterables=[{
            "kind": h.kind, "base_addr": h.base_addr,
            "stride": h.stride, "count": h.count,
        } for h in iterables],
        devices=[asdict(d) for d in devices],
        driver_groups=[{
            "api_addr": g.api_addr,
            "likely_kind": g.likely_kind,
            "devices": [asdict(d) for d in g.devices],
        } for g in driver_groups],
        inits=[asdict(i) for i in inits],
        threads=[asdict(t) for t in threads],
        log_sources=[asdict(l) for l in logs],
        shell_cmds=[asdict(c) for c in shells],
        settings_handlers=[asdict(h) for h in settings_h],
        movw_movt_constants_summary=movw_summary,
        notable_strings=notable[:30],
    )

    print(render_text(report))
    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump(asdict(report), f, indent=2, default=str)
        if args.verbose:
            print(f"[+] wrote {args.json_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
