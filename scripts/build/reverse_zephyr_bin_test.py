#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import reverse_zephyr_bin


class SyntheticZephyrBin:
    def __init__(self):
        self.base = 0x08000000
        self.ptr_size = 4
        self.endian = "little"
        self.data = bytearray(0x400)
        self.cursor = 0x20
        self.strings = {}

    def _pack_ptr(self, offset, value):
        self.data[offset : offset + 4] = struct.pack("<I", value)

    def _write_string(self, value):
        offset = self.cursor
        encoded = value.encode("ascii") + b"\x00"
        self.data[offset : offset + len(encoded)] = encoded
        self.cursor += len(encoded)
        self.strings[value] = offset
        return self.base + offset

    def _write_nodelabel_table(self, label):
        label_ptr = self._write_string(label)
        offset = self.cursor
        self.data[offset : offset + 8] = struct.pack("<II", 1, label_ptr)
        self.cursor += 8
        return self.base + offset

    def _write_dt_meta(self, nodelabel_struct_ptr):
        offset = self.cursor
        self.data[offset : offset + 4] = struct.pack("<I", nodelabel_struct_ptr)
        self.cursor += 4
        return self.base + offset

    def _write_deps(self, required, injected, supported):
        offset = self.cursor
        values = [*required, -32768, *injected, -32768, *supported, 32767]
        self.data[offset : offset + len(values) * 2] = struct.pack("<" + "h" * len(values), *values)
        self.cursor += len(values) * 2
        return self.base + offset

    def build(self):
        gpio_name = self._write_string("GPIO_0")
        i2c_name = self._write_string("I2C_0")
        self._write_string("nordic,nrf-gpio")
        self._write_string("nordic,nrf-twim")
        self._write_string("/soc/gpio@50000000")
        self._write_string("/soc/i2c@40003000")

        gpio_labels = self._write_nodelabel_table("gpio0")
        i2c_labels = self._write_nodelabel_table("i2c0")
        gpio_dt_meta = self._write_dt_meta(gpio_labels)
        i2c_dt_meta = self._write_dt_meta(i2c_labels)

        gpio_deps = self._write_deps([], [], [2])
        i2c_deps = self._write_deps([1], [], [])

        gpio_dev = 0x100
        i2c_dev = 0x124
        gpio_init = self.base + 0x300
        i2c_init = self.base + 0x320

        device_fields = [
            [
                gpio_name,
                self.base + 0x340,
                self.base + 0x350,
                0x20001000,
                0x20002000,
                gpio_init,
                0,
                gpio_deps,
                gpio_dt_meta,
            ],
            [
                i2c_name,
                self.base + 0x360,
                self.base + 0x370,
                0x20001020,
                0x20002020,
                i2c_init,
                0,
                i2c_deps,
                i2c_dt_meta,
            ],
        ]

        for device_offset, fields in zip((gpio_dev, i2c_dev), device_fields, strict=True):
            for index, value in enumerate(fields[:6]):
                self._pack_ptr(device_offset + index * 4, value)
            self.data[device_offset + 24] = fields[6]
            self._pack_ptr(device_offset + 28, fields[7])
            self._pack_ptr(device_offset + 32, fields[8])

        self._pack_ptr(0x200, gpio_init)
        self._pack_ptr(0x204, self.base + gpio_dev)
        self._pack_ptr(0x208, i2c_init)
        self._pack_ptr(0x20C, self.base + i2c_dev)

        return bytes(self.data)


class TestReverseZephyrBin(unittest.TestCase):
    def setUp(self):
        self.image = SyntheticZephyrBin().build()

    def test_auto_detects_devices_and_devicetree(self):
        analyzer = reverse_zephyr_bin.ZephyrBinaryAnalyzer(self.image)
        result = analyzer.analyze()

        self.assertEqual(result["format"]["pointer_size"], 4)
        self.assertEqual(result["format"]["endianness"], "little")
        self.assertEqual(result["format"]["base"], 0x08000000)
        self.assertEqual([dev["name"] for dev in result["devices"]], ["GPIO_0", "I2C_0"])

        gpio, i2c = result["devices"]
        self.assertEqual(gpio["handle"], 1)
        self.assertEqual(i2c["handle"], 2)
        self.assertEqual(gpio["devicetree"]["nodelabels"], ["gpio0"])
        self.assertEqual(i2c["devicetree"]["nodelabels"], ["i2c0"])
        self.assertEqual(i2c["deps"]["required_resolved"], [{"handle": 1, "name": "GPIO_0"}])

        self.assertEqual(result["iterable_regions"][0]["kind"], "device_array")
        self.assertEqual(result["iterable_regions"][0]["count"], 2)
        self.assertEqual(result["iterable_regions"][1]["kind"], "init_entries")
        self.assertEqual(len(result["init_entries"]), 2)

        self.assertIn("gpio0", result["devicetree"]["nodelabels"])
        self.assertIn("nordic,nrf-gpio", result["devicetree"]["compatibles"])
        self.assertIn("/soc/gpio@50000000", result["devicetree"]["paths"])

    def test_cli_json_output(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            binary = Path(tmpdir) / "zephyr.bin"
            binary.write_bytes(self.image)

            proc = subprocess.run(
                [sys.executable, str(Path(reverse_zephyr_bin.__file__)), str(binary)],
                check=True,
                capture_output=True,
                text=True,
            )

            result = json.loads(proc.stdout)
            self.assertEqual(result["devices"][0]["name"], "GPIO_0")
            self.assertEqual(result["devices"][1]["name"], "I2C_0")


if __name__ == "__main__":
    unittest.main()
