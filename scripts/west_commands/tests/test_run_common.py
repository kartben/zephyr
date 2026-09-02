# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from collections import namedtuple
from pathlib import Path
from unittest.mock import patch

import run_common

Module = namedtuple('Module', ['project', 'meta', 'depends'])


class FakeCommand:
    name = 'flash'
    manifest = object()


def test_import_module_runners():
    modules = [
        Module('/mod/a', {'name': 'hal_a', 'runners': [{'file': 'scripts/a_runner.py'}]}, []),
        Module('/mod/b', {'name': 'hal_b'}, []),
        Module('/mod/c', {'runners': [{'file': 'x.py'}]}, []),
    ]
    with (
        patch('run_common.zephyr_module.parse_modules', return_value=modules),
        patch('run_common.import_from_path') as import_from_path,
    ):
        run_common.import_module_runners(FakeCommand())

    assert import_from_path.call_args_list == [
        (('hal_a.a_runner', Path('/mod/a/scripts/a_runner.py')),),
        (('runners_ext.x', Path('/mod/c/x.py')),),
    ]
