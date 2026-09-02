# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import argparse
import enum
import json
import os
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from zephyr_ext_common import ZephyrJSONEncoder, add_json_arg, emit_json


class Color(enum.Enum):
    RED = 'red'


@dataclass
class Inner:
    name: str
    tags: list[str] = field(default_factory=list)


@dataclass
class Outer:
    path: Path
    inner: Inner
    options: set[str]


def test_encoder_types():
    obj = Outer(Path('a') / 'b', Inner('x', ['t2', 't1']), {'z', 'a'})
    out = json.loads(json.dumps(obj, cls=ZephyrJSONEncoder))
    assert out == {
        'path': os.fspath(Path('a') / 'b'),
        'inner': {'name': 'x', 'tags': ['t2', 't1']},
        'options': ['a', 'z'],
    }
    assert json.dumps(Color.RED, cls=ZephyrJSONEncoder) == '"red"'
    with pytest.raises(TypeError):
        json.dumps(object(), cls=ZephyrJSONEncoder)


def test_emit_json_is_deterministic_and_bypasses_inf(capsys):
    emit_json({'b': [Path('x')], 'a': {'d': 1, 'c': 2}})
    out = capsys.readouterr().out
    assert out.endswith('\n')
    assert json.loads(out) == {'a': {'c': 2, 'd': 1}, 'b': ['x']}
    # Keys are sorted at every level.
    assert out.index('"a"') < out.index('"b"')
    assert out.index('"c"') < out.index('"d"')


def test_add_json_arg():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    add_json_arg(parser)
    assert parser.parse_args([]).json is False
    assert parser.parse_args(['--json']).json is True
