# Copyright (c) 2026 The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for Zephyr module dependency and requirement helpers."""

import json
import sys
from pathlib import Path

import pytest

SCRIPTS_DIR = Path(__file__).resolve().parents[2]
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import zephyr_module  # noqa: E402


def _write_module(root, name, depends=None):
    module_dir = root / name
    zephyr_dir = module_dir / 'zephyr'
    zephyr_dir.mkdir(parents=True)
    meta = {'name': name}
    if depends is not None:
        meta['build'] = {'depends': list(depends)}
    (zephyr_dir / 'module.yml').write_text(
        __import__('yaml').safe_dump(meta), encoding='utf-8'
    )
    return module_dir


def test_sanitize_module_name_hyphen_and_dot():
    assert zephyr_module.sanitize_module_name('foo-bar') == 'foo_bar'
    assert zephyr_module.sanitize_module_name('mipi-sys-t') == 'mipi_sys_t'
    assert zephyr_module.sanitize_module_name('hal_tdk') == 'hal_tdk'


def test_kconfig_symbol_names_preserve_logical_module():
    assert zephyr_module.module_kconfig_symbol('foo-bar') == 'ZEPHYR_FOO_BAR_MODULE'
    assert (
        zephyr_module.module_requirement_symbol('foo-bar')
        == 'ZEPHYR_FOO_BAR_MODULE_REQUIRED'
    )


def test_sort_modules_is_deterministic_and_respects_depends(tmp_path):
    root_a = _write_module(tmp_path, 'mod_a', depends=['mod_b'])
    root_b = _write_module(tmp_path, 'mod_b')
    meta_a = zephyr_module.process_module(root_a, require_yaml_validation=False)
    meta_b = zephyr_module.process_module(root_b, require_yaml_validation=False)
    modules = {
        'mod_a': zephyr_module.Module(root_a, meta_a, ['mod_b']),
        'mod_b': zephyr_module.Module(root_b, meta_b, []),
    }

    ordered = zephyr_module.sort_modules(modules)

    assert [m.meta['name'] for m in ordered] == ['mod_b', 'mod_a']
    # Original depends lists are not consumed by the sort.
    assert modules['mod_a'].depends == ['mod_b']


def test_sort_modules_reports_missing_dependency(tmp_path):
    root_a = _write_module(tmp_path, 'mod_a', depends=['mod_missing'])
    meta_a = zephyr_module.process_module(root_a, require_yaml_validation=False)
    modules = {
        'mod_a': zephyr_module.Module(root_a, meta_a, ['mod_missing']),
    }

    with pytest.raises(zephyr_module.MissingModuleDependency) as excinfo:
        zephyr_module.sort_modules(modules)

    assert 'mod_missing' in str(excinfo.value)
    assert 'Cyclic' not in str(excinfo.value)


def test_sort_modules_reports_cycle(tmp_path):
    root_a = _write_module(tmp_path, 'mod_a', depends=['mod_b'])
    root_b = _write_module(tmp_path, 'mod_b', depends=['mod_a'])
    meta_a = zephyr_module.process_module(root_a, require_yaml_validation=False)
    meta_b = zephyr_module.process_module(root_b, require_yaml_validation=False)
    modules = {
        'mod_a': zephyr_module.Module(root_a, meta_a, ['mod_b']),
        'mod_b': zephyr_module.Module(root_b, meta_b, ['mod_a']),
    }

    with pytest.raises(zephyr_module.CyclicModuleDependency) as excinfo:
        zephyr_module.sort_modules(modules)

    message = str(excinfo.value)
    assert 'Cyclic module dependencies' in message
    assert 'please fetch' not in message.lower()


def test_parse_modules_duplicate_names_last_definition_wins(tmp_path):
    first = _write_module(tmp_path / 'first', 'shared')
    second = _write_module(tmp_path / 'second', 'shared')

    parsed = zephyr_module.parse_modules(
        zephyr_base=tmp_path,
        modules=[first, second],
        extra_modules=[],
        require_yaml_validation=False,
    )

    assert len(parsed) == 1
    assert parsed[0].project == second


def test_module_requirement_set_detects_symbol_collision():
    reqs = zephyr_module.ModuleRequirementSet()
    reqs.add(
        zephyr_module.ModuleRequirement(
            name='foo-bar',
            present=False,
            kconfig_symbol='ZEPHYR_FOO_BAR_MODULE',
            requirement_symbol='ZEPHYR_FOO_BAR_MODULE_REQUIRED',
        )
    )
    reqs.add(
        zephyr_module.ModuleRequirement(
            name='foo_bar',
            present=False,
            kconfig_symbol='ZEPHYR_FOO_BAR_MODULE',
            requirement_symbol='ZEPHYR_FOO_BAR_MODULE_REQUIRED',
        )
    )

    with pytest.raises(zephyr_module.ModuleNameCollision):
        reqs.validate_symbol_collisions()


def test_module_requirement_set_evaluate_present_vs_missing():
    reqs = zephyr_module.ModuleRequirementSet([
        zephyr_module.ModuleRequirement(
            name='hal_tdk',
            present=False,
            kconfig_symbol='ZEPHYR_HAL_TDK_MODULE',
            requirement_symbol='ZEPHYR_HAL_TDK_MODULE_REQUIRED',
            west_project='hal_tdk',
        ),
        zephyr_module.ModuleRequirement(
            name='hal_stm32',
            present=True,
            kconfig_symbol='ZEPHYR_HAL_STM32_MODULE',
            requirement_symbol='ZEPHYR_HAL_STM32_MODULE_REQUIRED',
            west_project='hal_stm32',
        ),
        zephyr_module.ModuleRequirement(
            name='liblc3',
            present=False,
            kconfig_symbol='ZEPHYR_LIBLC3_MODULE',
            requirement_symbol='ZEPHYR_LIBLC3_MODULE_REQUIRED',
        ),
    ])

    required, missing = reqs.evaluate({
        'ZEPHYR_HAL_TDK_MODULE_REQUIRED',
        'ZEPHYR_HAL_STM32_MODULE_REQUIRED',
        'ZEPHYR_HAL_STM32_MODULE',
    })

    assert [r.name for r in required] == ['hal_stm32', 'hal_tdk']
    assert [r.name for r in missing] == ['hal_tdk']

    document = reqs.to_document(required, missing)
    assert document['schema_version'] == 1
    assert document['missing'] == ['hal_tdk']
    assert document['required'][0]['module'] == 'hal_stm32'
    assert document['required'][0]['present'] is True
    assert document['required'][1]['present'] is False


def test_manual_external_module_satisfies_requirement():
    reqs = zephyr_module.ModuleRequirementSet([
        zephyr_module.ModuleRequirement(
            name='hal_tdk',
            present=True,
            kconfig_symbol='ZEPHYR_HAL_TDK_MODULE',
            requirement_symbol='ZEPHYR_HAL_TDK_MODULE_REQUIRED',
            west_project='hal_tdk',
        ),
    ])

    required, missing = reqs.evaluate({
        'ZEPHYR_HAL_TDK_MODULE',
        'ZEPHYR_HAL_TDK_MODULE_REQUIRED',
    })

    assert [r.name for r in required] == ['hal_tdk']
    assert missing == []


def test_parse_west_yml_projects_uses_userdata_override(tmp_path):
    west_yml = tmp_path / 'west.yml'
    west_yml.write_text(
        '''
manifest:
  projects:
    - name: vendor_hal
      userdata:
        zephyr:
          module: hal_vendor
    - name: plain_lib
''',
        encoding='utf-8',
    )

    pairs = zephyr_module.parse_west_yml_projects(west_yml)

    assert pairs == [('hal_vendor', 'vendor_hal'), ('plain_lib', 'plain_lib')]


def test_parse_dotconfig_enabled(tmp_path):
    dotconfig = tmp_path / '.config'
    dotconfig.write_text(
        '# comment\n'
        'CONFIG_ZEPHYR_HAL_TDK_MODULE_REQUIRED=y\n'
        'CONFIG_ZEPHYR_HAL_TDK_MODULE=n\n'
        'CONFIG_FOO="string"\n',
        encoding='utf-8',
    )

    enabled = zephyr_module.parse_dotconfig_enabled(dotconfig)

    assert enabled == {'ZEPHYR_HAL_TDK_MODULE_REQUIRED'}


def test_generate_requirement_kconfig_and_json_roundtrip(tmp_path):
    reqs = zephyr_module.ModuleRequirementSet([
        zephyr_module.ModuleRequirement(
            name='hal_tdk',
            present=False,
            kconfig_symbol='ZEPHYR_HAL_TDK_MODULE',
            requirement_symbol='ZEPHYR_HAL_TDK_MODULE_REQUIRED',
            west_project='hal_tdk',
        ),
    ])

    snippet = zephyr_module.generate_requirement_kconfig(reqs)
    assert 'config ZEPHYR_HAL_TDK_MODULE_REQUIRED' in snippet
    assert "module 'hal_tdk'" in snippet

    out = tmp_path / 'modules-required.json'
    required, missing = reqs.evaluate({'ZEPHYR_HAL_TDK_MODULE_REQUIRED'})
    zephyr_module.write_json_atomic(out, reqs.to_document(required, missing))

    data = json.loads(out.read_text(encoding='utf-8'))
    assert data['missing'] == ['hal_tdk']
    assert data['required'][0]['west_project'] == 'hal_tdk'


def test_collect_requireable_modules_from_west_yml_without_workspace(tmp_path):
    zephyr_base = tmp_path / 'zephyr'
    zephyr_base.mkdir()
    (zephyr_base / 'west.yml').write_text(
        '''
manifest:
  projects:
    - name: hal_tdk
    - name: lora-basics-modem
''',
        encoding='utf-8',
    )
    present = _write_module(tmp_path, 'hal_tdk')

    reqs = zephyr_module.collect_requireable_modules(
        zephyr_base,
        modules=[present],
        extra_modules=[],
        require_yaml_validation=False,
    )

    tdk = reqs.get('hal_tdk')
    modem = reqs.get('lora-basics-modem')
    assert tdk is not None and tdk.present is True
    assert tdk.west_project == 'hal_tdk'
    assert modem is not None and modem.present is False
    assert modem.kconfig_symbol == 'ZEPHYR_LORA_BASICS_MODEM_MODULE'


def test_evaluate_requirements_cli(tmp_path):
    mapping = tmp_path / 'map.json'
    mapping.write_text(
        json.dumps({
            'schema_version': 1,
            'modules': [
                {
                    'name': 'hal_tdk',
                    'kconfig_symbol': 'ZEPHYR_HAL_TDK_MODULE',
                    'requirement_symbol': 'ZEPHYR_HAL_TDK_MODULE_REQUIRED',
                    'present': False,
                    'west_project': 'hal_tdk',
                }
            ],
        }),
        encoding='utf-8',
    )
    dotconfig = tmp_path / '.config'
    dotconfig.write_text('CONFIG_ZEPHYR_HAL_TDK_MODULE_REQUIRED=y\n', encoding='utf-8')
    result = tmp_path / 'modules-required.json'

    completed = __import__('subprocess').run(
        [
            sys.executable,
            str(SCRIPTS_DIR / 'zephyr_module.py'),
            '--evaluate-requirements',
            '--dotconfig', str(dotconfig),
            '--requirements-map-out', str(mapping),
            '--requirements-result-out', str(result),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0

    data = json.loads(result.read_text(encoding='utf-8'))
    assert data['missing'] == ['hal_tdk']
    assert data['required'][0]['module'] == 'hal_tdk'
