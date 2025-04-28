#!/usr/bin/env python3

# Copyright (c) 2024 Vestas Wind Systems A/S
# Copyright (c) 2020 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

import argparse
from dataclasses import dataclass
from pathlib import Path
import pykwalify.core
import sys
import yaml
from typing import List, Union, Optional

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

SHIELD_SCHEMA_PATH = str(Path(__file__).parent / 'schemas' / 'shield-schema.yml')
with open(SHIELD_SCHEMA_PATH, 'r') as f:
    shield_schema = yaml.load(f.read(), Loader=SafeLoader)

SHIELD_YML = 'shield.yml'

#
# This is shared code between the build system's 'shields' target
# and the 'west shields' extension command. If you change it, make
# sure to test both ways it can be used.
#
# (It's done this way to keep west optional, making it possible to run
# 'ninja shields' in a build directory without west installed.)
#

@dataclass(frozen=True)
class ShieldVariant:
    name: str
    description: Optional[str]
    overlay: str

@dataclass(frozen=True)
class Shield:
    name: str
    dir: Path
    full_name: str = None
    vendor: str = None
    description: str = None
    compatible: List[str] = None
    variants: List[ShieldVariant] = None

def shield_key(shield):
    return shield.name

def find_shields(args):
    ret = []

    for root in args.board_roots:
        for shields in find_shields_in(root):
            ret.append(shields)

    return sorted(ret, key=shield_key)

def find_shields_in(root):
    shields = root / 'boards' / 'shields'
    ret = []

    for maybe_shield in (shields).iterdir():
        if not maybe_shield.is_dir():
            continue

        # Check for shield.yml first
        shield_yml = maybe_shield / SHIELD_YML
        if shield_yml.is_file():
            with shield_yml.open('r', encoding='utf-8') as f:
                shield_data = yaml.load(f.read(), Loader=SafeLoader)

            try:
                pykwalify.core.Core(source_data=shield_data, schema_data=shield_schema).validate()
            except pykwalify.errors.SchemaError as e:
                sys.exit('ERROR: Malformed shield.yml in file: {}\n{}'
                         .format(shield_yml.as_posix(), e))

            # Create shield variants if present
            variants = None
            if 'variants' in shield_data:
                variants = [
                    ShieldVariant(
                        name=v['name'],
                        description=v.get('description'),
                        overlay=v['overlay']
                    )
                    for v in shield_data['variants']
                ]

            # Create shield from yaml data
            ret.append(Shield(
                name=shield_data['name'],
                dir=maybe_shield,
                full_name=shield_data.get('full_name'),
                vendor=shield_data.get('vendor'),
                description=shield_data.get('description'),
                compatible=shield_data.get('compatible'),
                variants=variants
            ))
            continue

        # Fallback to old method if no shield.yml
        # Look for Kconfig.shield and overlay files
        kconfig_file = maybe_shield / 'Kconfig.shield'
        if kconfig_file.is_file():
            # Find all overlay files
            overlay_files = list(maybe_shield.glob('*.overlay'))
            if overlay_files:
                # If there's only one overlay, it's the main shield
                if len(overlay_files) == 1:
                    shield_name = overlay_files[0].stem
                    ret.append(Shield(shield_name, maybe_shield))
                else:
                    # Multiple overlays indicate variants
                    variants = []
                    for overlay in overlay_files:
                        name = overlay.stem
                        if '_' in name:
                            # Extract variant name from overlay filename
                            base, variant = name.rsplit('_', 1)
                            variants.append(ShieldVariant(
                                name=variant,
                                description=None,
                                overlay=name
                            ))
                    if variants:
                        ret.append(Shield(
                            name=maybe_shield.name,
                            dir=maybe_shield,
                            variants=variants
                        ))

    return sorted(ret, key=shield_key)

def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    add_args(parser)
    return parser.parse_args()

def add_args(parser):
    # Remember to update west-completion.bash if you add or remove
    # flags
    parser.add_argument("--board-root", dest='board_roots', default=[],
                        type=Path, action='append',
                        help='add a board root, may be given more than once')

def dump_shields(shields):
    for shield in shields:
        print(f'  {shield.name}')
        if shield.variants:
            for variant in shield.variants:
                print(f'    - {variant.name}')

if __name__ == '__main__':
    dump_shields(find_shields(parse_args()))
