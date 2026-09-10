# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from argparse import Namespace
from collections import namedtuple
from pathlib import Path
from unittest.mock import patch

import pytest

import run_common

Module = namedtuple('Module', ['project', 'meta', 'depends'])
Domain = namedtuple('Domain', ['name', 'build_dir'])


class FakeCommand:
    name = 'flash'
    manifest = object()

    def inf(self, *args, **kwargs):
        pass

    def wrn(self, *args, **kwargs):
        pass

    def die(self, msg):
        raise SystemExit(msg)


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


DOMAINS = [Domain('app', '/b/app'), Domain('mcuboot', '/b/mcuboot')]


def test_select_context_domain_passthrough():
    cmd = FakeCommand()
    assert run_common.select_context_domain(cmd, Namespace(), DOMAINS[:1]) == DOMAINS[:1]
    assert run_common.select_context_domain(cmd, Namespace(domain=['app']), DOMAINS) == DOMAINS


def test_select_context_domain_without_tty_dies():
    with patch('sys.stdin.isatty', return_value=False), pytest.raises(SystemExit) as e:
        run_common.select_context_domain(FakeCommand(), Namespace(), DOMAINS)
    assert 'app, mcuboot' in str(e.value)


def test_select_context_domain_prompts_on_tty():
    with (
        patch('sys.stdin.isatty', return_value=True),
        patch('builtins.input', side_effect=['x', '9', '2']),
    ):
        assert run_common.select_context_domain(FakeCommand(), Namespace(), DOMAINS) == [DOMAINS[1]]
