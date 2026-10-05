# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
The modules a build can use with --strict-soc-modules.
"""

import functools
from argparse import Namespace
from pathlib import Path

import list_hardware
from twisterlib.environment import _parse_modules


@functools.cache
def _socs(soc_roots):
    systems = list_hardware.find_v2_systems(Namespace(soc_roots=list(soc_roots)))
    return {soc.name: soc for soc in systems.get_socs()}


def visible_modules(platform_name, test_modules, soc_roots):
    """Returns the paths of the modules a build for 'platform_name' can use.

    A module that any soc.yml lists is left out, unless the SoC or CPU cluster
    of the board target lists it, or the test does. Modules that no SoC lists,
    such as libraries, stay available to every build.
    """
    socs = _socs(tuple(soc_roots))
    listed = {m for soc in socs.values() for m in soc.modules}
    listed |= {m for soc in socs.values() for mods in soc.cpucluster_modules.values() for m in mods}

    # A board target is <board>[@<revision>]/<soc>[/<cluster>][/<variant>]
    qualifiers = platform_name.split('/')[1:]
    soc = socs.get(qualifiers[0]) if qualifiers else None
    needed = set(test_modules)
    if soc is not None:
        needed |= set(soc.modules)
        if len(qualifiers) > 1:
            needed |= set(soc.cpucluster_modules.get(qualifiers[1], []))

    return [
        Path(module.project).as_posix()
        for module in _parse_modules()
        if module.meta['name'] not in listed or module.meta['name'] in needed
    ]
