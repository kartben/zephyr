#!/usr/bin/env python3

# Copyright (c) 2026 The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""
Runs several of the devicetree generation scripts in this directory in a
single Python process.

The build system needs to run gen_edt.py, gen_defines.py and
gen_driver_kconfig_dts.py back to back on every CMake configure. Running them
through this driver instead of as three separate processes saves two Python
interpreter startups and repeated imports of the devicetree libraries, which
is a measurable part of CMake configure time.

Usage:

    dts_pipeline.py <stage> [stage args...] [--then <stage> [stage args...]]...

where <stage> is the name of one of the scripts in this directory, without
the .py suffix. Each stage is imported as a module and its main() is invoked
with sys.argv set exactly as if the script had been run on its own, so
behavior, output and error reporting of the individual stages are unchanged.
Stages run in the given order; a failing stage aborts the pipeline with its
usual error output and a non-zero exit code.
"""

import importlib
import os
import sys

STAGES = frozenset({
    'gen_edt',
    'gen_defines',
    'gen_driver_kconfig_dts',
    'gen_dts_cmake',
})


def parse_stages(argv):
    stages = []
    current = None
    for token in argv:
        if token == '--then':
            current = None
        elif current is None:
            if token not in STAGES:
                sys.exit(f'dts_pipeline: unknown stage: {token} '
                         f'(expected one of: {", ".join(sorted(STAGES))})')
            current = [token]
            stages.append(current)
        else:
            current.append(token)
    if not stages:
        sys.exit('dts_pipeline: no stages given')
    return stages


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    saved_argv = sys.argv

    for name, *args in parse_stages(sys.argv[1:]):
        module = importlib.import_module(name)
        sys.argv = [os.path.join(script_dir, name + '.py')] + args
        try:
            module.main()
        finally:
            sys.argv = saved_argv


if __name__ == '__main__':
    main()
