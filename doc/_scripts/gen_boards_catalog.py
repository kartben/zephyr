# Copyright (c) 2024 The Linux Foundation
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import logging
import os
import subprocess
import sys
from collections import namedtuple
from pathlib import Path

import list_boards
import pykwalify
import yaml
import zephyr_module
from devicetree import edtlib
from gen_devicetree_rest import VndLookup

ZEPHYR_BASE = Path(__file__).parents[2]

BINDING_TYPE_TO_HUMAN_READABLE = {
    "acpi": ":abbr:`ACPI (Advanced Configuration and Power Interface)`",
    "adc": ":abbr:`ADC (Analog to Digital Converter)`",
    "alh": ":abbr:`ALH (Audio Link Hub)`",
    "arc": "ARC architecture",
    "arm": "ARM architecture",
    "audio": "Audio",
    "auxdisplay": "Auxiliary Display",
    "battery": "Battery",
    "base": "Base",
    "bluetooth": "Bluetooth",
    "cache": "Cache",
    "can": ":abbr:`CAN (Controller Area Network)` controller",
    "charger": "Charger",
    "clock": "Clock control",
    "coredump": "Core dump",
    "counter": "Counter",
    "cpu": "CPU",
    "crypto": "Cryptographic accelerator",
    "dac": ":abbr:`DAC (Digital to Analog Converter)`",
    "dai": ":abbr:`DAI (Digital Audio Interface)`",
    "debug": "Debug",
    "dfpmcch": "DFPMCCH",
    "dfpmccu": "DFPMCCU",
    "disk": "Disk",
    "display": "Display",
    "display/panel": "Display panel",
    "dma": ":abbr:`DMA (Direct Memory Access)`",
    "dsa": ":abbr:`DSA (Distributed Switch Architecture)`",
    "edac": ":abbr:`EDAC (Error Detection and Correction)`",
    "espi": ":abbr:`eSPI (Enhanced Serial Peripheral Interface)`",
    "ethernet": "Ethernet",
    "firmware": "Firmware",
    "flash_controller": "Flash controller",
    "fpga": ":abbr:`FPGA (Field Programmable Gate Array)`",
    "fs": "File system",
    "fuel-gauge": "Fuel gauge",
    "gnss": ":abbr:`GNSS (Global Navigation Satellite System)`",
    "gpio": ":abbr:`GPIO (General Purpose Input/Output)`",
    "haptics": "Haptics",
    "hda": ":abbr:`HDA (High Definition Audio)`",
    "hwinfo": "Hardware information",
    "hwspinlock": "Hardware spinlock",
    "i2c": ":abbr:`I2C (Inter-Integrated Circuit)`",
    "i2s": ":abbr:`I2S (Inter-IC Sound)`",
    "i3c": ":abbr:`I3C (Improved Inter-Integrated Circuit)`",
    "ieee802154": "IEEE 802.15.4",
    "input": "Input",
    "interrupt-controller": "Interrupt controller",
    "ipc": ":abbr:`IPC (Inter-Processor Communication)`",
    "ipm": ":abbr:`IPM (Inter-Processor Mailbox)`",
    "kscan": "Keyscan",
    "led": ":abbr:`LED (Light Emitting Diode)` controller",
    "led_strip": ":abbr:`LED (Light Emitting Diode)` strip",
    "lora": "LoRa",
    "mbox": "Mailbox",
    "mdio": ":abbr:`MDIO (Management Data Input/Output)`",
    "memory-controllers": "Memory controller",
    "memory-window": "Memory window",
    "mfd": ":abbr:`MFD (Multi-Function Device)`",
    "mhu": ":abbr:`MHU (Mailbox Handling Unit)`",
    "mipi-dbi": ":abbr:`MIPI DBI (Mobile Industry Processor Interface Display Bus Interface)`",
    "mipi-dsi": ":abbr:`MIPI DSI (Mobile Industry Processor Interface Display Serial Interface)`",
    "misc": "Miscellaneous",
    "mm": "Memory management",
    "mmc": ":abbr:`MMC (MultiMediaCard)`",
    "mmu_mpu": ":abbr:`MMU (Memory Management Unit)` / :abbr:`MPU (Memory Protection Unit)`",
    "modem": "Modem",
    "mspi": "Multi-bit :abbr:`SPI (Serial Peripheral Interface)`",
    "mtd": ":abbr:`MTD (Memory Technology Device)`",
    "wireless": "Wireless network",
    "options": "Options",
    "ospi": "Octal :abbr:`SPI (Serial Peripheral Interface)`",
    "pcie": ":abbr:`PCIe (Peripheral Component Interconnect Express)`",
    "peci": ":abbr:`PECI (Platform Environment Control Interface)`",
    "phy": "PHY",
    "pinctrl": "Pin control",
    "pm_cpu_ops": "Power management CPU operations",
    "power": "Power management",
    "power-domain": "Power domain",
    "ppc": "PPC architecture",
    "ps2": ":abbr:`PS/2 (Personal System/2)`",
    "pwm": ":abbr:`PWM (Pulse Width Modulation)`",
    "qspi": "Quad :abbr:`SPI (Serial Peripheral Interface)`",
    "regulator": "Regulator",
    "reserved-memory": "Reserved memory",
    "reset": "Reset controller",
    "retained_mem": "Retained memory",
    "retention": "Retention",
    "riscv": "RISC-V architecture",
    "rng": ":abbr:`RNG (Random Number Generator)`",
    "rtc": ":abbr:`RTC (Real Time Clock)`",
    "sd": "SD",
    "sdhc": "SDHC",
    "sensor": "Sensors",
    "serial": "Serial controller",
    "shi": ":abbr:`SHI (Secure Hardware Interface)`",
    "sip_svc": ":abbr:`SIP (Service in Platform)` service",
    "smbus": ":abbr:`SMBus (System Management Bus)`",
    "sound": "Sound",
    "spi": ":abbr:`SPI (Serial Peripheral Interface)`",
    "sram": "SRAM",
    "stepper": "Stepper",
    "syscon": "System controller",
    "tach": "Tachometer",
    "tcpc": ":abbr:`TCPC (USB Type-C Port Controller)`",
    "test": "Test",
    "timer": "Timer",
    "timestamp": "Timestamp",
    "usb": "USB",
    "usb-c": "USB Type-C",
    "uac2": "USB Audio Class 2",
    "video": "Video",
    "virtualization": "Virtualization",
    "w1": "1-Wire",
    "watchdog": "Watchdog",
    "wifi": "Wi-Fi",
    "xen": "Xen",
    "xspi": ":abbr:`XSPI (Expanded Serial Peripheral Interface)`",
}


def print_edt_nodes(nodes, indent=0):
    print("Nodes:")
    for node in nodes:
        print(" " * indent + f"{node.name} ({node.status})")


def main():
    """
    Generate a board_catalog.json file that contains information about all the boards in the Zephyr
    tree. Also generates an .rst file for each board that lists all the enabled features for that
    board in the form of a list-table that can then be included in the board's README.

    Running with --turbo-mode will skip the hello_world build for all boards, which allows to run
    the script faster and avoid the need to have a full-blown Zephyr development environment set up.
    In this case, the script will only generate the board_catalog.json file and the .rst files will
    be empty.
    """

    logging.basicConfig(level=logging.INFO)
    logging.info("Generating board catalog...")

    pykwalify.init_logging(1)

    parser = argparse.ArgumentParser(description="Generate boards catalog", allow_abbrev=False)
    parser.add_argument("--turbo-mode", action="store_true", help="Enable turbo mode")
    parser.add_argument("out_dir", help="output files are generated here")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    if not args.turbo_mode:
        # cmd = [
        #     "../../scripts/twister",
        #     "-T",
        #     "../../samples/hello_world/",
        #     "-p",
        #     "wio_terminal",
        #     "-p",
        #     "walter/esp32s3/appcpu",
        #     "-p",
        #     "walter/esp32s3/procpu",
        #     "-p",
        #     "frdm_k64f",
        #     "--cmake-only",
        # ]

        cmd = ["../../scripts/twister", "-T", "../../samples/hello_world/", "--all", "--cmake-only"]

        # do not fail if the hello_world build fails
        try:
            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError:
            pass

    # Load the vendor lookup table
    vnd_lookup = VndLookup(ZEPHYR_BASE / "dts/bindings/vendor-prefixes.txt", [])

    module_settings = {
        "arch_root": [ZEPHYR_BASE],
        "board_root": [ZEPHYR_BASE],
        "soc_root": [ZEPHYR_BASE],
    }

    for module in zephyr_module.parse_modules(ZEPHYR_BASE):
        for key in module_settings:
            root = module.meta.get("build", {}).get("settings", {}).get(key)
            if root is not None:
                module_settings[key].append(Path(module.project) / root)

    Args = namedtuple("args", ["arch_roots", "board_roots", "soc_roots", "board_dir", "board"])
    args_find_boards = Args(
        arch_roots=module_settings["arch_root"],
        board_roots=module_settings["board_root"],
        soc_roots=module_settings["soc_root"],
        board_dir=ZEPHYR_BASE / "boards",
        board=None,
    )

    boards = list_boards.find_v2_boards(args_find_boards)
    print(f"Found {len(boards)} boards")

    board_catalog = {}

    # get a list of all folders directly contained under ZEPHYR_BASE / "dts/bindings"
    binding_types = [x for x in (ZEPHYR_BASE / "dts/bindings").iterdir() if x.is_dir()]

    # sort alphabetically and if the list contains something that ends with /sensor, put it at the
    # end
    binding_types.sort(key=lambda x: x.name)
    # sensor = [x for x in binding_types if x.name.endswith("sensor")]
    # if sensor:
    #     binding_types.remove(sensor[0])
    #     binding_types.append(sensor[0])

    for board in boards:
        vendor = vnd_lookup.vnd2vendor.get(board.vendor)
        name = board.name

        # guessed twister-out folder = ZEPHYR_BASE/twister-out/{board.name}_{board.qualifiers}
        # where qualifiers are the board qualifiers, separated by underscores (need to be replaced
        # from forward slashes)

        qualifiers = list_boards.board_v2_qualifiers(board)
        if len(qualifiers) == 1:
            qualifiers = [""]

        for q in qualifiers:
            if q == "":
                full_name = f"{board.name}"
            else:
                full_name = f"{board.name}_{q.replace('/', '_')}"

            supported_features = []

            dts_file = f"twister-out/{full_name}/samples/hello_world/sample.basic.helloworld/zephyr/zephyr.dts"
            if os.path.exists(dts_file):
                edt = edtlib.EDT(dts_file, bindings_dirs=[ZEPHYR_BASE / "dts/bindings"])

                # iterate on nodes with status 'okay'
                okay_nodes = [node for node in edt.nodes if node.status == "okay"]

                list_table_file = Path(f"{args.out_dir}/{name}/{full_name}.rst")
                list_table_file.parent.mkdir(parents=True, exist_ok=True)
                with open(list_table_file, "w") as f:
                    f.write(":orphan:\n\n")
                    f.write(".. list-table::\n")
                    f.write("   :widths: 20 80\n")
                    f.write("   :header-rows: 1\n\n")
                    f.write("   * - Type\n")
                    f.write("     - Compatible\n")

                    # Go through all the binding types excluding the "sensor" ones
                    for binding_type in [x for x in binding_types if not x.name == "sensor"]:
                        nodes = [
                            node
                            for node in okay_nodes
                            if str(node.binding_path).startswith(str(binding_type))
                            and node.matching_compat is not None
                        ]
                        nodes = {node.matching_compat: node for node in nodes}.values()

                        if nodes:
                            if BINDING_TYPE_TO_HUMAN_READABLE.get(binding_type.name):
                                f.write(
                                    f"   * - {BINDING_TYPE_TO_HUMAN_READABLE[binding_type.name]}\n"
                                )
                            else:
                                f.write(f"   * - {binding_type.name.upper()}\n")

                            f.write(
                                "     - "
                                + "\n       ".join(
                                    [
                                        f"* {node.description.splitlines()[0]} (:dtcompatible:`{node.matching_compat}`)"
                                        for node in nodes
                                    ]
                                )
                            )
                            f.write("\n")
                            supported_features.append(binding_type.name)

                    # Show a list of sensors at the end
                    f.write(f"Sensors\n")
                    f.write(f"-------\n")
                    for binding_type in [x for x in binding_types if x.name == "sensor"]:
                        nodes = [
                            node
                            for node in okay_nodes
                            if str(node.binding_path).startswith(str(binding_type))
                            and node.matching_compat is not None
                        ]
                        nodes = {node.matching_compat: node for node in nodes}.values()

                        if nodes:
                            for node in nodes:
                                f.write(
                                    f"* {node.description.splitlines()[0]} (:dtcompatible:`{node.matching_compat}`)\n"
                                )
                                supported_features.append(binding_type.name)

            else:
                placeholder_file = Path(f"{args.out_dir}/{name}/{full_name}.rst")
                placeholder_file.parent.mkdir(parents=True, exist_ok=True)
                with open(placeholder_file, "w") as f:
                    f.write(":orphan:\n\n")
                    f.write("[ Placeholder for supported features. ]")

            # load the twister file for the board
            twister_file = board.dir / f"{full_name}.yaml"

            try:
                with open(twister_file, "r") as f:
                    board_data = yaml.safe_load(f)
                    board_dir = board.dir
                    board_human_readable_name = board_data.get("name")
                    board_description = board_data.get("description")
                    arch = board_data.get("arch")
                    ram = board_data.get("ram")
                    flash = board_data.get("flash")
                    # supported = board_data.get("supported") or []

                    img_exts = ["jpg", "webp", "png"]
                    img_file = None

                    # Try to find the image file with the exact name and extension
                    img_file = next(
                        (file for ext in img_exts for file in board.dir.glob(f"**/{name}.{ext}")),
                        None,
                    )

                    # If not found, try to find the image file with the name as a prefix
                    if not img_file:
                        img_file = next(
                            (
                                file
                                for ext in img_exts
                                for file in board.dir.glob(f"**/{name}*.{ext}")
                            ),
                            None,
                        )

                    # If still not found, try to find any image file with the given extensions
                    if not img_file:
                        img_file = next(
                            (file for ext in img_exts for file in board.dir.glob(f"**/*.{ext}")),
                            None,
                        )

                    if img_file:
                        img_file = Path("../_images") / img_file.name

                    print(board_dir.relative_to(ZEPHYR_BASE).as_posix())

                    board_catalog[full_name] = {
                        "dir": board_dir.relative_to(ZEPHYR_BASE).as_posix(),
                        "board_name": name,
                        "name": board_human_readable_name,
                        "vendor_raw": board.vendor,
                        "vendor": vendor,
                        "description": board_description,
                        "arch": arch,
                        "ram": ram,
                        "flash": flash,
                        "supported": supported_features,
                        "image": img_file.as_posix() if img_file else "",
                    }

            except FileNotFoundError:
                pass

    with open(f"{args.out_dir}/board_catalog.json", "w") as f:
        json.dump(board_catalog, f, indent=4)


if __name__ == "__main__":
    main()
    sys.exit(0)
