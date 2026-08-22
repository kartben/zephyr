#!/usr/bin/env python3
#
# Copyright (c) 2019, Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

'''Tool for parsing a list of projects to determine if they are Zephyr
projects. If no projects are given then the output from `west list` will be
used as project list.

Include file is generated for Kconfig using --kconfig-out.
A <name>:<path> text file is generated for use with CMake using --cmake-out.

Using --twister-out <filename> an argument file for twister script will
be generated which would point to test and sample roots available in modules
that can be included during a twister run. This allows testing code
maintained in modules in addition to what is available in the main Zephyr tree.
'''

# Warning: avoid adding third party dependencies other than those provided by west
# to this file. The west 'packages' extension imports this module to install Python
# dependencies for Zephyr and Zephyr modules

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import yaml
from collections import namedtuple
from dataclasses import dataclass
from pathlib import Path, PurePath, PurePosixPath

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

# NOTE: keep in sync with doc/develop/modules.rst
METADATA_SCHEMA = '''
## A JSON Schema (Draft 2020-12) for basic validation of the structure of a
## metadata YAML file.
##
$schema: "https://json-schema.org/draft/2020-12/schema"
type: object
properties:
  name:
    type: string
  build:
    type: object
    properties:
      cmake:
        type: string
      kconfig:
        type: string
      cmake-ext:
        type: boolean
      kconfig-ext:
        type: boolean
      sysbuild-cmake:
        type: string
      sysbuild-kconfig:
        type: string
      sysbuild-cmake-ext:
        type: boolean
      sysbuild-kconfig-ext:
        type: boolean
      depends:
        type: array
        items:
          type: string
      settings:
        type: object
        properties:
          board_root:
            type: string
          dts_root:
            type: string
          snippet_root:
            type: string
          soc_root:
            type: string
          arch_root:
            type: string
          module_ext_root:
            type: string
          sca_root:
            type: string
  tests:
    type: array
    items:
      type: string
  samples:
    type: array
    items:
      type: string
  boards:
    type: array
    items:
      type: string
  blobs:
    type: array
    items:
      type: object
      properties:
        path:
          type: string
        sha256:
          type: string
        type:
          type: string
          enum: ['img', 'lib']
        version:
          type: string
        license-path:
          type: string
        click-through:
          type: boolean
        url:
          anyOf:
            - type: string
            - type: array
              items:
                type: string
        description:
          type: string
        doc-url:
          type: string
        fetcher:
          type: string
        size:
          type: integer
      required:
        - path
        - sha256
        - type
        - version
        - license-path
        - url
        - description
  security:
    type: object
    properties:
      external-references:
        type: array
        items:
          type: string
  package-managers:
    type: object
    properties:
      pip:
        type: object
        properties:
          requirement-files:
            type: array
            items:
              type: string
  runners:
    type: array
    items:
      type: object
      properties:
        file:
          type: string
      required:
        - file
'''

MODULE_YML_PATH = PurePath('zephyr/module.yml')
# Path to the blobs folder
MODULE_BLOBS_PATH = PurePath('zephyr/blobs')
BLOB_PRESENT = 'A'
BLOB_NOT_PRESENT = 'D'
BLOB_OUTDATED = 'M'

# Keep sanitization in sync with process_module() and doc/develop/modules.rst.
MODULE_NAME_SANITIZE_RE = re.compile('[^a-zA-Z0-9]')
MODULE_REQUIREMENTS_SCHEMA_VERSION = 1


def sanitize_module_name(name):
    """Return the Kconfig/CMake-safe form of a logical Zephyr module name."""
    return MODULE_NAME_SANITIZE_RE.sub('_', name)


def module_kconfig_symbol(name):
    return f'ZEPHYR_{sanitize_module_name(name).upper()}_MODULE'


def module_requirement_symbol(name):
    return f'{module_kconfig_symbol(name)}_REQUIRED'


class ModuleNameCollision(ValueError):
    """Two logical module names sanitize to the same Kconfig symbol."""


class MissingModuleDependency(Exception):
    """A present module lists a build.depends name that is not present."""

    def __init__(self, missing):
        # missing: list[(module_path, [dep_name, ...])]
        self.missing = missing
        lines = ['Missing module dependencies:']
        for project, depends in missing:
            dep_list = ', '.join(depends)
            lines.append(f'{project} depends on missing module(s): {dep_list}')
        super().__init__('\n'.join(lines))


class CyclicModuleDependency(Exception):
    """Present modules have a cycle in build.depends."""

    def __init__(self, modules):
        # modules: list[(module_path, [remaining_dep_name, ...])]
        self.modules = modules
        lines = ['Cyclic module dependencies:']
        for project, depends in modules:
            dep_list = ', '.join(depends)
            lines.append(f'{project} depends on: {dep_list}')
        super().__init__('\n'.join(lines))


@dataclass(frozen=True)
class ModuleRequirement:
    """One logical Zephyr module that may be required by a build."""

    name: str
    present: bool
    kconfig_symbol: str
    requirement_symbol: str
    west_project: str | None = None

    def to_dict(self):
        data = {
            'module': self.name,
            'present': self.present,
            'kconfig_symbol': self.kconfig_symbol,
            'requirement_symbol': self.requirement_symbol,
        }
        if self.west_project is not None:
            data['west_project'] = self.west_project
        return data


class ModuleRequirementSet:
    """Deterministic collection of module requirements, keyed by logical name."""

    def __init__(self, requirements=None):
        self._by_name = {}
        if requirements:
            for req in requirements:
                self.add(req)

    def add(self, requirement):
        existing = self._by_name.get(requirement.name)
        if existing is None:
            self._by_name[requirement.name] = requirement
            return
        # Later sources may add presence or a west project mapping.
        self._by_name[requirement.name] = ModuleRequirement(
            name=requirement.name,
            present=existing.present or requirement.present,
            kconfig_symbol=existing.kconfig_symbol,
            requirement_symbol=existing.requirement_symbol,
            west_project=requirement.west_project or existing.west_project,
        )

    def validate_symbol_collisions(self):
        by_symbol = {}
        for req in self:
            symbol = req.kconfig_symbol
            previous = by_symbol.get(symbol)
            if previous and previous != req.name:
                raise ModuleNameCollision(
                    f'module names {previous!r} and {req.name!r} both sanitize '
                    f'to Kconfig symbol {symbol}'
                )
            by_symbol[symbol] = req.name

    def get(self, name):
        return self._by_name.get(name)

    def __iter__(self):
        return iter(sorted(self._by_name.values(), key=lambda r: r.name))

    def __len__(self):
        return len(self._by_name)

    def evaluate(self, enabled_symbols):
        """Return (required, missing) lists from a set of enabled Kconfig names.

        enabled_symbols contains unprefixed names such as
        ZEPHYR_HAL_TDK_MODULE_REQUIRED.
        """
        required = []
        missing = []
        for req in self:
            if req.requirement_symbol not in enabled_symbols:
                continue
            required.append(req)
            if not req.present and req.kconfig_symbol not in enabled_symbols:
                missing.append(req)
        return required, missing

    def to_document(self, required, missing):
        return {
            'schema_version': MODULE_REQUIREMENTS_SCHEMA_VERSION,
            'required': [req.to_dict() for req in required],
            'missing': [req.name for req in missing],
        }


def requirement_kconfig_snippet(requirement):
    name = requirement.name
    symbol = requirement.requirement_symbol
    return (
        f'config {symbol}\n'
        f'\tbool\n'
        f'\thelp\n'
        f"\t  Selected when the current configuration requires Zephyr "
        f"module '{name}'.\n"
    )


def generate_requirement_kconfig(requirement_set):
    if not len(requirement_set):
        return '# No known module requirement symbols.\n'
    parts = [
        '# Generated module requirement symbols.\n',
        '# These exist even when the corresponding module repository is absent.\n',
    ]
    for req in requirement_set:
        parts.append(requirement_kconfig_snippet(req))
    return '\n'.join(parts) + '\n'


def logical_name_from_userdata(userdata, default):
    if not isinstance(userdata, dict):
        return default
    zephyr = userdata.get('zephyr')
    if isinstance(zephyr, dict) and zephyr.get('module'):
        return zephyr['module']
    return default


def parse_west_yml_projects(west_yml):
    """Return (logical_name, west_project_name) pairs from a west.yml file.

    This is a fallback used when no west workspace is available. It reads
    project names only; URLs and revisions are never treated as fetch
    instructions.
    """
    west_yml = Path(west_yml)
    if not west_yml.is_file():
        return []
    with west_yml.open(encoding='utf-8') as f:
        data = yaml.load(f.read(), Loader=SafeLoader) or {}
    projects = data.get('manifest', {}).get('projects', []) or []
    pairs = []
    for project in projects:
        if not isinstance(project, dict) or not project.get('name'):
            continue
        name = project['name']
        pairs.append((logical_name_from_userdata(project.get('userdata'), name),
                      name))
    return pairs


def parse_dotconfig_enabled(dotconfig):
    """Return the set of enabled (unprefixed) Kconfig symbol names."""
    enabled = set()
    path = Path(dotconfig)
    if not path.is_file():
        return enabled
    prefix = 'CONFIG_'
    with path.open(encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(prefix) or not line.endswith('=y'):
                continue
            enabled.add(line[len(prefix):-2])
    return enabled


def write_json_atomic(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    text = json.dumps(data, indent=2) + '\n'
    tmp.write_text(text, encoding='utf-8')
    tmp.replace(path)


try:
    import jsonschema
    from jsonschema.exceptions import best_match

    SCHEMA = yaml.load(METADATA_SCHEMA, Loader=SafeLoader)
    VALIDATOR_CLASS = jsonschema.validators.validator_for(SCHEMA)
    VALIDATOR_CLASS.check_schema(SCHEMA)
    VALIDATOR = VALIDATOR_CLASS(SCHEMA)
except ImportError:
    jsonschema = None


def validate_setting(setting, module_path, filename=None):
    if setting is not None:
        if filename is not None:
            checkfile = Path(module_path) / setting / filename
        else:
            checkfile = Path(module_path) / setting
        if not checkfile.resolve().is_file():
            return False
    return True


def process_module(module, require_yaml_validation=True):
    module_path = PurePath(module)

    # The input is a module if zephyr/module.{yml,yaml} is a valid yaml file
    # or if both zephyr/CMakeLists.txt and zephyr/Kconfig are present.

    for module_yml in [module_path / MODULE_YML_PATH,
                       module_path / MODULE_YML_PATH.with_suffix('.yaml')]:
        if Path(module_yml).is_file():
            with Path(module_yml).open('rb') as f:
                meta = yaml.load(f.read(), Loader=SafeLoader)

            if jsonschema is not None:
                errors = list(VALIDATOR.iter_errors(meta))

                if errors:
                    sys.exit(
                        'ERROR: Malformed module YAML file: '
                        f'{module_yml.as_posix()}\n'
                        f'{best_match(errors).message} in {best_match(errors).json_path}'
                    )
            elif require_yaml_validation:
                sys.exit('Missing jsonschema dependency')

            meta['name'] = meta.get('name', module_path.name)
            meta['name-sanitized'] = sanitize_module_name(meta['name'])
            return meta

    if Path(module_path.joinpath('zephyr/CMakeLists.txt')).is_file() and \
       Path(module_path.joinpath('zephyr/Kconfig')).is_file():
        return {'name': module_path.name,
                'name-sanitized': sanitize_module_name(module_path.name),
                'build': {'cmake': 'zephyr', 'kconfig': 'zephyr/Kconfig'}}

    return None


def process_cmake(module, meta):
    section = meta.get('build', dict())
    module_path = PurePath(module)
    module_yml = module_path.joinpath('zephyr/module.yml')

    cmake_extern = section.get('cmake-ext', False)
    if cmake_extern:
        return('\"{}\":\"{}\":\"{}\"\n'
               .format(meta['name'],
                       module_path.as_posix(),
                       "${ZEPHYR_" + meta['name-sanitized'].upper() + "_CMAKE_DIR}"))

    cmake_setting = section.get('cmake', None)
    if not validate_setting(cmake_setting, module, 'CMakeLists.txt'):
        sys.exit('ERROR: "cmake" key in {} has folder value "{}" which '
                 'does not contain a CMakeLists.txt file.'
                 .format(module_yml.as_posix(), cmake_setting))

    cmake_path = os.path.join(module, cmake_setting or 'zephyr')
    cmake_file = os.path.join(cmake_path, 'CMakeLists.txt')
    if os.path.isfile(cmake_file):
        return('\"{}\":\"{}\":\"{}\"\n'
               .format(meta['name'],
                       module_path.as_posix(),
                       Path(cmake_path).resolve().as_posix()))
    else:
        return('\"{}\":\"{}\":\"\"\n'
               .format(meta['name'],
                       module_path.as_posix()))


def process_sysbuildcmake(module, meta):
    section = meta.get('build', dict())
    module_path = PurePath(module)
    module_yml = module_path.joinpath('zephyr/module.yml')

    cmake_extern = section.get('sysbuild-cmake-ext', False)
    if cmake_extern:
        return('\"{}\":\"{}\":\"{}\"\n'
               .format(meta['name'],
                       module_path.as_posix(),
                       "${SYSBUILD_" + meta['name-sanitized'].upper() + "_CMAKE_DIR}"))

    cmake_setting = section.get('sysbuild-cmake', None)
    if not validate_setting(cmake_setting, module, 'CMakeLists.txt'):
        sys.exit('ERROR: "cmake" key in {} has folder value "{}" which '
                 'does not contain a CMakeLists.txt file.'
                 .format(module_yml.as_posix(), cmake_setting))

    if cmake_setting is None:
        return ""

    cmake_path = os.path.join(module, cmake_setting or 'zephyr')
    cmake_file = os.path.join(cmake_path, 'CMakeLists.txt')
    if os.path.isfile(cmake_file):
        return('\"{}\":\"{}\":\"{}\"\n'
               .format(meta['name'],
                       module_path.as_posix(),
                       Path(cmake_path).resolve().as_posix()))
    else:
        return('\"{}\":\"{}\":\"\"\n'
               .format(meta['name'],
                       module_path.as_posix()))


def process_settings(module, meta):
    section = meta.get('build', dict())
    build_settings = section.get('settings', None)
    out_text = ""

    if build_settings is not None:
        for root in ['board', 'dts', 'snippet', 'soc', 'arch', 'module_ext', 'sca']:
            setting = build_settings.get(root+'_root', None)
            if setting is not None:
                root_path = PurePath(module) / setting
                out_text += f'"{root.upper()}_ROOT":'
                out_text += f'"{root_path.as_posix()}"\n'

    return out_text


def get_blob_status(path, sha256):
    if not path.is_file():
        return BLOB_NOT_PRESENT
    with path.open('rb') as f:
        m = hashlib.sha256()
        m.update(f.read())
        if sha256.lower() == m.hexdigest():
            return BLOB_PRESENT
        else:
            return BLOB_OUTDATED


def process_blobs(module, meta):
    blobs = []
    mblobs = meta.get('blobs', None)
    if not mblobs:
        return blobs

    blobs_path = Path(module) / MODULE_BLOBS_PATH
    for blob in mblobs:
        blob['module'] = meta.get('name', None)
        blob['abspath'] = blobs_path / Path(blob['path'])
        blob['license-abspath'] = Path(module) / Path(blob['license-path'])
        blob['status'] = get_blob_status(blob['abspath'], blob['sha256'])
        blob['click-through'] = blob.get('click-through', False)
        blobs.append(blob)

    return blobs


def kconfig_module_opts(name_sanitized, blobs, taint_blobs):
    snippet = [f'config ZEPHYR_{name_sanitized.upper()}_MODULE',
               '	bool',
               '	default y']

    if taint_blobs:
        snippet += ['	select TAINT_BLOBS']

    if blobs:
        snippet += [f'\nconfig ZEPHYR_{name_sanitized.upper()}_MODULE_BLOBS',
                    '	bool']
        if taint_blobs:
            snippet += ['	default y']

    return snippet


def kconfig_snippet(meta, path, kconfig_file=None, blobs=False, taint_blobs=False, sysbuild=False):
    name = meta['name']
    name_sanitized = meta['name-sanitized']

    snippet = [f'menu "{name} ({path.as_posix()})"']

    snippet += [f'osource "{kconfig_file.resolve().as_posix()}"' if kconfig_file
                else f'osource "$(SYSBUILD_{name_sanitized.upper()}_KCONFIG)"' if sysbuild is True
                else f'osource "$(ZEPHYR_{name_sanitized.upper()}_KCONFIG)"']

    snippet += kconfig_module_opts(name_sanitized, blobs, taint_blobs)

    snippet += ['endmenu\n']

    return '\n'.join(snippet)


def process_kconfig_module_dir(module, meta, cmake_output):
    module_path = PurePath(module)
    name_sanitized = meta['name-sanitized']

    if cmake_output is False:
        return f'ZEPHYR_{name_sanitized.upper()}_MODULE_DIR={module_path.as_posix()}\n'
    return f'list(APPEND kconfig_env_dirs ZEPHYR_{name_sanitized.upper()}_MODULE_DIR={module_path.as_posix()})\n'


def process_kconfig(module, meta):
    blobs = process_blobs(module, meta)
    taint_blobs = any(b['status'] != BLOB_NOT_PRESENT for b in blobs)
    section = meta.get('build', dict())
    module_path = PurePath(module)
    module_yml = module_path.joinpath('zephyr/module.yml')
    kconfig_extern = section.get('kconfig-ext', False)

    if kconfig_extern:
        return kconfig_snippet(meta, module_path, blobs=blobs, taint_blobs=taint_blobs)

    kconfig_setting = section.get('kconfig', None)
    if not validate_setting(kconfig_setting, module):
        sys.exit('ERROR: "kconfig" key in {} has value "{}" which does '
                 'not point to a valid Kconfig file.'
                 .format(module_yml, kconfig_setting))

    kconfig_file = os.path.join(module, kconfig_setting or 'zephyr/Kconfig')
    if os.path.isfile(kconfig_file):
        return kconfig_snippet(meta, module_path, Path(kconfig_file),
                               blobs=blobs, taint_blobs=taint_blobs)
    else:
        name_sanitized = meta['name-sanitized']
        return '\n'.join(kconfig_module_opts(name_sanitized, blobs, taint_blobs)) + '\n'


def process_sysbuildkconfig(module, meta):
    section = meta.get('build', dict())
    module_path = PurePath(module)
    module_yml = module_path.joinpath('zephyr/module.yml')
    kconfig_extern = section.get('sysbuild-kconfig-ext', False)
    name_sanitized = meta['name-sanitized']

    if kconfig_extern:
        return kconfig_snippet(meta, module_path, sysbuild=True)

    kconfig_setting = section.get('sysbuild-kconfig', None)
    if not validate_setting(kconfig_setting, module):
        sys.exit('ERROR: "kconfig" key in {} has value "{}" which does '
                 'not point to a valid Kconfig file.'
                 .format(module_yml, kconfig_setting))

    if kconfig_setting is not None:
        kconfig_file = os.path.join(module, kconfig_setting)
        if os.path.isfile(kconfig_file):
            return kconfig_snippet(meta, module_path, Path(kconfig_file))

    return (f'config ZEPHYR_{name_sanitized.upper()}_MODULE\n'
            f'   bool\n'
            f'   default y\n')


def process_twister(module, meta):

    out = ""
    tests = meta.get('tests', [])
    samples = meta.get('samples', [])
    boards = meta.get('boards', [])

    for pth in tests + samples:
        if pth:
            dir = os.path.join(module, pth)
            out += '-T\n{}\n'.format(PurePath(os.path.abspath(dir))
                                     .as_posix())

    for pth in boards:
        if pth:
            dir = os.path.join(module, pth)
            out += '--board-root\n{}\n'.format(PurePath(os.path.abspath(dir))
                                               .as_posix())

    return out

def is_valid_git_revision(revision):
    """
    Returns True if the given string is a valid git revision hash (40 hex digits).
    """
    if not isinstance(revision, str):
        return False
    return bool(re.fullmatch(r'[0-9a-fA-F]{40}', revision))

def _create_meta_project(project_path):
    def git_revision(path):
        rc = subprocess.Popen(['git', 'rev-parse', '--is-inside-work-tree'],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,
                              cwd=path).wait()
        if rc == 0:
            # A git repo.
            popen = subprocess.Popen(['git', 'rev-parse', 'HEAD'],
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE,
                                     cwd=path)
            stdout, stderr = popen.communicate()
            stdout = stdout.decode('utf-8')

            if not (popen.returncode or stderr):
                revision = stdout.rstrip()

                rc = subprocess.Popen(['git', 'diff-index', '--quiet', 'HEAD',
                                       '--'],
                                      stdout=None,
                                      stderr=None,
                                      cwd=path).wait()
                if rc:
                    return revision + '-dirty', True
                return revision, False
        return "unknown", False

    def git_remote(path):
        popen = subprocess.Popen(['git', 'remote'],
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,
                                 cwd=path)
        stdout, stderr = popen.communicate()
        stdout = stdout.decode('utf-8')

        remotes_name = []
        if not (popen.returncode or stderr):
            remotes_name = stdout.rstrip().split('\n')

        remote_url = None

        # If more than one remote, do not return any remote
        if len(remotes_name) == 1:
            remote = remotes_name[0]
            popen = subprocess.Popen(['git', 'remote', 'get-url', remote],
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE,
                                     cwd=path)
            stdout, stderr = popen.communicate()
            stdout = stdout.decode('utf-8')

            if not (popen.returncode or stderr):
                remote_url = stdout.rstrip()

        return remote_url

    def git_tags(path, revision):
        if not revision or len(revision) == 0:
            return None

        popen = subprocess.Popen(['git', '-P', 'tag', '--points-at', revision],
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,
                                 cwd=path)
        stdout, stderr = popen.communicate()
        stdout = stdout.decode('utf-8')

        tags = None
        if not (popen.returncode or stderr):
            tags = stdout.rstrip().splitlines()

        return tags

    workspace_dirty = False
    path = PurePath(project_path).as_posix()

    revision, dirty = git_revision(path)
    workspace_dirty |= dirty
    remote = git_remote(path)
    tags = git_tags(path, revision)

    meta_project = {'path': path,
                    'revision': revision}

    if remote:
        meta_project['remote'] = remote

    if tags:
        meta_project['tags'] = tags

    return meta_project, workspace_dirty


def _get_meta_project(meta_projects_list, project_path):
    projects = [ prj for prj in meta_projects_list[1:] if prj["path"] == project_path ]

    return projects[0] if len(projects) == 1 else None


def process_meta(zephyr_base, west_projs, modules, extra_modules=None,
                 propagate_state=False):
    # Process zephyr_base, projects, and modules and create a dictionary
    # with meta information for each input.
    #
    # The dictionary will contain meta info in the following lists:
    # - zephyr:        path and revision
    # - modules:       name, path, and revision
    # - west-projects: path and revision
    #
    # returns the dictionary with said lists

    meta = {'zephyr': None, 'modules': None, 'workspace': None}

    zephyr_project, zephyr_dirty = _create_meta_project(zephyr_base)
    zephyr_off = zephyr_project.get("remote") is None

    workspace_dirty = zephyr_dirty
    workspace_extra = extra_modules is not None
    workspace_off = zephyr_off

    if zephyr_off and is_valid_git_revision(zephyr_project['revision']):
        zephyr_project['revision'] += '-off'

    meta['zephyr'] = zephyr_project
    meta['workspace'] = {}

    if west_projs is not None:
        from west.manifest import MANIFEST_REV_BRANCH
        projects = west_projs['projects']
        meta_projects = []

        manifest_path = projects[0].posixpath

        # Special treatment of manifest project
        # Git information (remote/revision) are not provided by west for the Manifest (west.yml)
        # To mitigate this, we check if we don't use the manifest from the zephyr repository or an other project.
        # If it's from zephyr, reuse zephyr information
        # If it's from an other project, ignore it, it will be added later
        # If it's not found, we extract data manually (remote/revision) from the directory

        manifest_project = None
        manifest_dirty = False
        manifest_off = False

        if zephyr_base == manifest_path:
            manifest_project = zephyr_project
            manifest_dirty = zephyr_dirty
            manifest_off = zephyr_off
        elif not [ prj for prj in projects[1:] if prj.posixpath == manifest_path ]:
            manifest_project, manifest_dirty = _create_meta_project(
                projects[0].posixpath)
            manifest_off = manifest_project.get("remote") is None
            if manifest_off and is_valid_git_revision(manifest_project['revision']):
                manifest_project["revision"] +=  "-off"

        if manifest_project:
            workspace_off |= manifest_off
            workspace_dirty |= manifest_dirty
            meta_projects.append(manifest_project)

        # Iterates on all projects except the first one (manifest)
        for project in projects[1:]:
            meta_project, dirty = _create_meta_project(project.posixpath)
            workspace_dirty |= dirty
            meta_projects.append(meta_project)

            off = False
            if not meta_project.get("remote") or project.sha(MANIFEST_REV_BRANCH) != meta_project['revision'].removesuffix("-dirty"):
                off = True
            if not meta_project.get('remote') or project.url != meta_project['remote']:
                # Force manifest URL and set commit as 'off'
                meta_project['url'] = project.url
                off = True

            if off:
                if is_valid_git_revision(meta_project['revision']):
                    meta_project['revision'] += '-off'
                workspace_off |= off

            # If manifest is in project, updates related variables
            if project.posixpath == manifest_path:
                manifest_dirty |= dirty
                manifest_off |= off
                manifest_project = meta_project

        meta.update({'west': {'manifest': west_projs['manifest_path'],
                              'projects': meta_projects}})
        meta['workspace'].update({'off': workspace_off})

    # Iterates on all modules
    meta_modules = []
    for module in modules:
        # Check if modules is not in projects
        # It allows to have the "-off" flag since `modules` variable` does not provide URL/remote
        meta_module = _get_meta_project(meta_projects, module.project)

        if not meta_module:
            meta_module, dirty = _create_meta_project(module.project)
            workspace_dirty |= dirty

        meta_module['name'] = module.meta.get('name')

        if module.meta.get('security'):
            meta_module['security'] = module.meta.get('security')
        meta_modules.append(meta_module)

    meta['modules'] = meta_modules

    meta['workspace'].update({'dirty': workspace_dirty,
                              'extra': workspace_extra})

    if propagate_state:
        zephyr_revision = zephyr_project['revision']
        if is_valid_git_revision(zephyr_revision):
            if workspace_dirty and not zephyr_dirty:
                zephyr_revision += '-dirty'
            if workspace_extra:
                zephyr_revision += '-extra'
            if workspace_off and not zephyr_off:
                zephyr_revision += '-off'
        zephyr_project.update({'revision': zephyr_revision})

        if west_projs is not None:
            manifest_revision = manifest_project['revision']
            if is_valid_git_revision(manifest_revision):
                if workspace_dirty and not manifest_dirty:
                    manifest_revision += '-dirty'
                if workspace_extra:
                    manifest_revision += '-extra'
                if workspace_off and not manifest_off:
                    manifest_revision += '-off'
            manifest_project.update({'revision': manifest_revision})

    return meta


def west_projects(manifest=None, active_only=True):
    manifest_path = None
    projects = []
    # West is imported here, as it is optional
    # (and thus maybe not installed)
    # if user is providing a specific modules list.
    try:
        from west.manifest import Manifest
    except ImportError:
        # West is not installed, so don't return any projects.
        return None

    # If west *is* installed, we need all of the following imports to
    # work. West versions that are excessively old may fail here:
    # west.configuration.MalformedConfig was
    # west.manifest.MalformedConfig until west v0.14.0, for example.
    # These should be hard errors.
    from west.manifest import \
        ManifestImportFailed, MalformedManifest, ManifestVersionError
    from west.configuration import MalformedConfig
    from west.util import WestNotFound
    from west.version import __version__ as WestVersion

    from packaging import version
    try:
        if not manifest:
            manifest = Manifest.from_file()
        if version.parse(WestVersion) >= version.parse('0.9.0') and active_only:
            projects = [p for p in manifest.get_projects([])
                        if manifest.is_active(p)]
        else:
            projects = manifest.get_projects([])
        manifest_path = manifest.abspath
        return {'manifest_path': manifest_path, 'projects': projects,
                'manifest': manifest}
    except (ManifestImportFailed, MalformedManifest,
            ManifestVersionError, MalformedConfig) as e:
        sys.exit(f'ERROR: {e}')
    except WestNotFound:
        # Only accept WestNotFound, meaning we are not in a west
        # workspace. Such setup is allowed, as west may be installed
        # but the project is not required to use west.
        pass
    return None


Module = namedtuple('Module', ['project', 'meta', 'depends'])


def sort_modules(all_modules_by_name):
    """Topologically sort modules, distinguishing missing deps from cycles.

    all_modules_by_name maps logical module name -> Module.
    Returns a list of Module in dependency order.
    """
    # Working copies so the original depends lists stay intact.
    remaining = {
        name: list(module.depends)
        for name, module in all_modules_by_name.items()
    }
    dep_modules = []
    start_modules = []
    sorted_modules = []

    for name, module in all_modules_by_name.items():
        if not remaining[name]:
            start_modules.append(module)
        else:
            dep_modules.append(module)

    while start_modules:
        node = start_modules.pop(0)
        sorted_modules.append(node)
        node_name = node.meta['name']
        to_remove = []
        for module in dep_modules:
            module_name = module.meta['name']
            if node_name in remaining[module_name]:
                remaining[module_name].remove(node_name)
                if not remaining[module_name]:
                    start_modules.append(module)
                    to_remove.append(module)
        for module in to_remove:
            dep_modules.remove(module)

    if dep_modules:
        missing = []
        cyclic = []
        for module in dep_modules:
            unresolved = remaining[module.meta['name']]
            missing_deps = [d for d in unresolved if d not in all_modules_by_name]
            if missing_deps:
                missing.append((module.project, missing_deps))
            else:
                cyclic.append((module.project, unresolved))
        if missing:
            raise MissingModuleDependency(missing)
        raise CyclicModuleDependency(cyclic)

    return sorted_modules


def parse_modules(zephyr_base, manifest=None, west_projs=None, modules=None,
                  extra_modules=None, require_yaml_validation=True):

    if modules is None:
        west_projs = west_projs or west_projects(manifest)
        modules = ([p.posixpath for p in west_projs['projects']]
                   if west_projs else [])

    if extra_modules is None:
        extra_modules = []
        for var in ['EXTRA_ZEPHYR_MODULES', 'ZEPHYR_EXTRA_MODULES']:
            extra_module = os.environ.get(var, None)
            if not extra_module:
                continue
            extra_modules.extend(PurePosixPath(p) for p in extra_module.split(';') if p)

    all_modules_by_name = {}

    for project in modules + extra_modules:
        # Avoid including Zephyr base project as module.
        if project == zephyr_base:
            continue

        meta = process_module(project, require_yaml_validation)
        if meta:
            depends = list(meta.get('build', {}).get('depends', []))
            all_modules_by_name[meta['name']] = Module(project, meta, depends)

        elif project in extra_modules:
            sys.exit(f'{project}, given in ZEPHYR_EXTRA_MODULES, '
                     'is not a valid zephyr module')

    try:
        return sort_modules(all_modules_by_name)
    except (MissingModuleDependency, CyclicModuleDependency) as e:
        sys.exit(str(e))


def collect_requireable_modules(zephyr_base, manifest=None, west_projs=None,
                                modules=None, extra_modules=None,
                                require_yaml_validation=True):
    """Collect logical module names that can be required, even if absent.

    Presence is determined from modules that are actually available to
    the build. Names come from:

    1. ZEPHYR_BASE/west.yml project names, as a west-less fallback
    2. All resolved west projects (active and inactive) when a workspace
       exists. Downstream overrides win because they are the resolved
       definition.
    3. Present modules (module.yml names), which mark the requirement
       satisfied.
    """
    reqs = ModuleRequirementSet()

    west_yml = Path(zephyr_base) / 'west.yml' if zephyr_base else None
    if west_yml:
        for logical, project_name in parse_west_yml_projects(west_yml):
            reqs.add(ModuleRequirement(
                name=logical,
                present=False,
                kconfig_symbol=module_kconfig_symbol(logical),
                requirement_symbol=module_requirement_symbol(logical),
                west_project=project_name,
            ))

    all_west = west_projects(manifest, active_only=False)
    if all_west:
        for project in all_west['projects']:
            userdata = getattr(project, 'userdata', None)
            logical = logical_name_from_userdata(userdata, project.name)
            reqs.add(ModuleRequirement(
                name=logical,
                present=False,
                kconfig_symbol=module_kconfig_symbol(logical),
                requirement_symbol=module_requirement_symbol(logical),
                west_project=project.name,
            ))

    present = parse_modules(zephyr_base, manifest, west_projs, modules,
                            extra_modules, require_yaml_validation)
    for module in present:
        name = module.meta['name']
        reqs.add(ModuleRequirement(
            name=name,
            present=True,
            kconfig_symbol=module_kconfig_symbol(name),
            requirement_symbol=module_requirement_symbol(name),
        ))

    reqs.validate_symbol_collisions()
    return reqs


def evaluate_module_requirements(requirement_set, dotconfig):
    enabled = parse_dotconfig_enabled(dotconfig)
    return requirement_set.evaluate(enabled)

def write_if_different(file, data):
    if Path(file).is_file():
        with open(file, encoding="utf-8") as fp:
            if fp.read() == data:
                return

    with open(file, 'w', encoding="utf-8") as fp:
        fp.write(data)

def main():
    parser = argparse.ArgumentParser(description='''
    Process a list of projects and create Kconfig / CMake include files for
    projects which are also a Zephyr module''', allow_abbrev=False)

    parser.add_argument('--kconfig-out',
                        help="""File to write with resulting KConfig import
                             statements.""")
    parser.add_argument('--twister-out',
                        help="""File to write with resulting twister
                             parameters.""")
    parser.add_argument('--cmake-out',
                        help="""File to write with resulting <name>:<path>
                             values to use for including in CMake""")
    parser.add_argument('--sysbuild-kconfig-out',
                        help="""File to write with resulting KConfig import
                             statements.""")
    parser.add_argument('--sysbuild-cmake-out',
                        help="""File to write with resulting <name>:<path>
                             values to use for including in CMake""")
    parser.add_argument('--meta-out',
                        help="""Write a build meta YaML file containing a list
                             of Zephyr modules and west projects.
                             If a module or project is also a git repository
                             the current SHA revision will also be written.""")
    parser.add_argument('--meta-state-propagate', action='store_true',
                        help="""Propagate state of modules and west projects
                             to the suffix of the Zephyr SHA and if west is
                             used, to the suffix of the manifest SHA""")
    parser.add_argument('--settings-out',
                        help="""File to write with resulting <name>:<value>
                             values to use for including in CMake""")
    parser.add_argument('-m', '--modules', nargs='+',
                        help="""List of modules to parse instead of using `west
                             list`""")
    parser.add_argument('-x', '--extra-modules', nargs='+',
                        help='List of extra modules to parse')
    parser.add_argument('-z', '--zephyr-base',
                        help='Path to zephyr repository')
    parser.add_argument('--requirements-kconfig-out',
                        help="""File to write with Kconfig requirement
                             symbols for known modules, including modules
                             that are not currently present.""")
    parser.add_argument('--requirements-map-out',
                        help="""JSON file mapping logical module names to
                             Kconfig symbols and west project names.""")
    parser.add_argument('--evaluate-requirements', action='store_true',
                        help="""Evaluate CONFIG_*_MODULE_REQUIRED symbols
                             from --dotconfig against --requirements-map-out
                             and write --requirements-result-out.""")
    parser.add_argument('--dotconfig',
                        help='Kconfig .config file to evaluate')
    parser.add_argument('--requirements-result-out',
                        help='JSON file written by --evaluate-requirements')
    args = parser.parse_args()

    if args.evaluate_requirements:
        if not args.dotconfig or not args.requirements_map_out:
            parser.error('--evaluate-requirements requires --dotconfig and '
                         '--requirements-map-out')
        if not args.requirements_result_out:
            parser.error('--evaluate-requirements requires --requirements-result-out')
        with open(args.requirements_map_out, encoding='utf-8') as fp:
            mapping = json.load(fp)
        reqs = ModuleRequirementSet()
        for entry in mapping.get('modules', []):
            reqs.add(ModuleRequirement(
                name=entry['name'],
                present=bool(entry.get('present')),
                kconfig_symbol=entry['kconfig_symbol'],
                requirement_symbol=entry['requirement_symbol'],
                west_project=entry.get('west_project'),
            ))
        required, missing = evaluate_module_requirements(reqs, args.dotconfig)
        write_json_atomic(args.requirements_result_out,
                          reqs.to_document(required, missing))
        return

    kconfig_module_dirs = ""
    kconfig_module_dirs_cmake = "set(kconfig_env_dirs)\n"
    kconfig = ""
    cmake = ""
    sysbuild_kconfig = ""
    sysbuild_cmake = ""
    twister = ""
    settings = '''\
# WARNING. THIS FILE IS AUTO-GENERATED. DO NOT MODIFY!
#
# This file contains build system settings derived from your modules.
#
# Modules may be set via ZEPHYR_MODULES, ZEPHYR_EXTRA_MODULES,
# and/or the west manifest file.
#
# See the Modules guide for more information.
'''

    west_projs = west_projects()
    modules = parse_modules(args.zephyr_base, None, west_projs,
                            args.modules, args.extra_modules)

    if args.requirements_kconfig_out or args.requirements_map_out:
        reqs = collect_requireable_modules(
            args.zephyr_base, None, west_projs, args.modules, args.extra_modules)
        if args.requirements_kconfig_out:
            write_if_different(args.requirements_kconfig_out,
                               generate_requirement_kconfig(reqs))
        if args.requirements_map_out:
            write_json_atomic(args.requirements_map_out, {
                'schema_version': MODULE_REQUIREMENTS_SCHEMA_VERSION,
                'modules': [
                    {
                        'name': req.name,
                        'kconfig_symbol': req.kconfig_symbol,
                        'requirement_symbol': req.requirement_symbol,
                        'present': req.present,
                        **({'west_project': req.west_project}
                           if req.west_project is not None else {}),
                    }
                    for req in reqs
                ],
            })

    for module in modules:
        kconfig_module_dirs += process_kconfig_module_dir(module.project, module.meta, False)
        kconfig_module_dirs_cmake += process_kconfig_module_dir(module.project, module.meta, True)
        kconfig += process_kconfig(module.project, module.meta)
        cmake += process_cmake(module.project, module.meta)
        sysbuild_kconfig += process_sysbuildkconfig(
            module.project, module.meta)
        sysbuild_cmake += process_sysbuildcmake(module.project, module.meta)
        settings += process_settings(module.project, module.meta)
        twister += process_twister(module.project, module.meta)

    if args.kconfig_out or args.sysbuild_kconfig_out:
        if args.kconfig_out:
            kconfig_module_dirs_out = PurePath(args.kconfig_out).parent / 'kconfig_module_dirs.env'
            kconfig_module_dirs_cmake_out = PurePath(args.kconfig_out).parent / \
                                            'kconfig_module_dirs.cmake'
        elif args.sysbuild_kconfig_out:
            kconfig_module_dirs_out = PurePath(args.sysbuild_kconfig_out).parent / \
                                      'kconfig_module_dirs.env'
            kconfig_module_dirs_cmake_out = PurePath(args.sysbuild_kconfig_out).parent / \
                                      'kconfig_module_dirs.cmake'

        write_if_different(kconfig_module_dirs_out, kconfig_module_dirs)
        write_if_different(kconfig_module_dirs_cmake_out, kconfig_module_dirs_cmake)

    if args.kconfig_out:
        write_if_different(args.kconfig_out, kconfig)

    if args.cmake_out:
        write_if_different(args.cmake_out, cmake)

    if args.sysbuild_kconfig_out:
        write_if_different(args.sysbuild_kconfig_out, sysbuild_kconfig)

    if args.sysbuild_cmake_out:
        write_if_different(args.sysbuild_cmake_out, sysbuild_cmake)

    if args.settings_out:
        write_if_different(args.settings_out, settings)

    if args.twister_out:
        write_if_different(args.twister_out, twister)

    if args.meta_out:
        meta = process_meta(args.zephyr_base, west_projs, modules,
                            args.extra_modules, args.meta_state_propagate)

        # Ignore references and insert data instead
        yaml.Dumper.ignore_aliases = lambda self, data: True
        write_if_different(args.meta_out, yaml.dump(meta))


if __name__ == "__main__":
    main()
