#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for the JSON reports produced by twisterlib.reports."""

import json
import os
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import jsonschema
import pytest
import scl
from twisterlib.reports import JsonReport, Reporting
from twisterlib.statuses import TwisterStatus

ZEPHYR_BASE = os.getenv('ZEPHYR_BASE')
SCHEMA_PATH = Path(ZEPHYR_BASE) / 'scripts' / 'schemas' / 'twister' / 'twister-report-schema.yaml'


@pytest.fixture(scope='module')
def report_schema():
    return scl.yaml_load(SCHEMA_PATH)


def make_env(**options):
    env = mock.Mock()
    env.toolchain = 'zephyr'
    env.commit_date = '2026-01-01T00:00:00+00:00'
    env.run_date = '2026-01-02T00:00:00+00:00'
    env.non_default_options.return_value = {'platform': ['native_sim']}
    defaults = {
        'report_all_options': False,
        'report_filtered': False,
        'create_rom_ram_report': False,
        'footprint_report': None,
    }
    env.options = SimpleNamespace(**(defaults | options))
    return env


def make_testcase(name, status=TwisterStatus.PASS, duration=0.5, output='', reason=None):
    case = mock.Mock(status=status, duration=duration, output=output, reason=reason, freeform=False)
    case.name = name
    return case


def make_instance(tmp_path, name, status, testcases, reason=None, metrics=None):
    instance = mock.Mock()
    instance.testsuite = mock.Mock(source_dir_rel='tests/dummy')
    instance.testsuite.name = name
    instance.platform = mock.Mock(arch='x86')
    instance.platform.name = 'native_sim'
    instance.status = status
    instance.reason = reason
    instance.metrics = {'handler_time': 1.234} | (metrics or {})
    instance.run_id = 'abc123'
    instance.run = True
    instance.retries = 0
    instance.toolchain = 'zephyr'
    instance.hardware_id = None
    instance.build_time = 2.5
    instance.recording = None
    instance.testcases = testcases
    build_dir = tmp_path / name
    build_dir.mkdir()
    instance.build_dir = str(build_dir)
    return instance


def create_report(tmp_path, env, instances, filters=None):
    filename = tmp_path / 'report.json'
    JsonReport(env, instances).create(filename, version='v4.4.0', filters=filters)
    with open(filename) as fh:
        return json.load(fh)


def test_json_report_passed_instance(tmp_path, report_schema):
    instance = make_instance(
        tmp_path,
        'dummy.passed',
        TwisterStatus.PASS,
        [make_testcase('dummy.passed.one'), make_testcase('dummy.passed.two', duration=0.25)],
        metrics={'used_ram': 1024, 'used_rom': 2048},
    )
    report = create_report(
        tmp_path, make_env(), {instance.name: instance}, Reporting.json_filters['twister.json']
    )

    jsonschema.validate(report, report_schema)
    assert report['environment']['options'] == {'platform': ['native_sim']}
    (suite,) = report['testsuites']
    assert suite['status'] == 'passed'
    assert suite['execution_time'] == '1.23'
    assert suite['build_time'] == '2.50'
    assert suite['used_ram'] == 1024
    assert 'footprint' not in suite
    assert [tc['identifier'] for tc in suite['testcases']] == [
        'dummy.passed.one',
        'dummy.passed.two',
    ]
    assert [tc['execution_time'] for tc in suite['testcases']] == ['0.50', '0.25']


def test_json_report_failed_instance(tmp_path, report_schema):
    instance = make_instance(
        tmp_path,
        'dummy.failed',
        TwisterStatus.FAIL,
        [make_testcase('dummy.failed.one', status=TwisterStatus.FAIL, reason='assertion')],
        reason='Build failure',
    )
    Path(instance.build_dir, 'build.log').write_text('main.c:1:1: error: boom\n')
    report = create_report(tmp_path, make_env(), {instance.name: instance})

    jsonschema.validate(report, report_schema)
    (suite,) = report['testsuites']
    assert suite['status'] == 'failed'
    assert suite['reason'] == 'Build failure - error: boom'
    assert 'error: boom' in suite['log']
    assert suite['testcases'][0]['reason'] == 'assertion'


def test_json_report_filtered_instance(tmp_path, report_schema):
    instance = make_instance(
        tmp_path,
        'dummy.filtered',
        TwisterStatus.FILTER,
        [make_testcase('dummy.filtered.one', status=TwisterStatus.SKIP)],
        reason='Not in testsuite platform allow list',
    )
    instance.run = False
    instances = {instance.name: instance}

    assert create_report(tmp_path, make_env(), instances)['testsuites'] == []

    report = create_report(tmp_path, make_env(report_filtered=True), instances)
    jsonschema.validate(report, report_schema)
    (suite,) = report['testsuites']
    assert suite['status'] == 'filtered'
    assert suite['runnable'] is False
    assert suite['testcases'][0]['status'] == 'filtered'


def test_json_report_none_status_instance(tmp_path, report_schema):
    instance = make_instance(
        tmp_path,
        'dummy.none',
        TwisterStatus.NONE,
        [make_testcase('dummy.none.one', TwisterStatus.NONE)],
    )
    report = create_report(tmp_path, make_env(), {instance.name: instance})

    jsonschema.validate(report, report_schema)
    (suite,) = report['testsuites']
    assert suite['status'] == 'None'
    assert 'execution_time' not in suite
    assert 'execution_time' not in suite['testcases'][0]


def test_json_report_footprint_filter(tmp_path, report_schema):
    instance = make_instance(
        tmp_path, 'dummy.footprint', TwisterStatus.PASS, [make_testcase('dummy.footprint.one')]
    )
    Path(instance.build_dir, 'rom.json').write_text(json.dumps({'symbols': {'name': 'root'}}))
    env = make_env(create_rom_ram_report=True, footprint_report=['ROM'])
    report = create_report(
        tmp_path, env, {instance.name: instance}, Reporting.json_filters['footprint.json']
    )

    jsonschema.validate(report, report_schema)
    (suite,) = report['testsuites']
    assert suite['footprint'] == {'ROM': {'symbols': {'name': 'root'}}}
    for key in ('testcases', 'execution_time', 'recording', 'retries', 'runnable'):
        assert key not in suite


def test_json_report_schema_rejects_unknown_suite_field(tmp_path, report_schema):
    instance = make_instance(
        tmp_path, 'dummy.passed', TwisterStatus.PASS, [make_testcase('dummy.passed.one')]
    )
    report = create_report(tmp_path, make_env(), {instance.name: instance})
    report['testsuites'][0]['unknown'] = 1

    with pytest.raises(jsonschema.ValidationError):
        jsonschema.validate(report, report_schema)
