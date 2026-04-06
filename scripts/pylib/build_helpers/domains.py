# Copyright (c) 2022 Nordic Semiconductor ASA
# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

'''Domain handling for west extension commands.

This provides parsing of domains yaml file and creation of objects of the
Domain class.
'''

import os
from dataclasses import dataclass
from pathlib import Path

import yaml
import pykwalify.core
import logging

DOMAINS_SCHEMA = '''
## A pykwalify schema for basic validation of the structure of a
## domains YAML file.
##
# The domains.yaml file is a simple list of domains from a multi image build
# along with the default domain to use.
type: map
mapping:
  default:
    required: true
    type: str
  build_dir:
    required: true
    type: str
  domains:
    required: true
    type: seq
    sequence:
      - type: map
        mapping:
          name:
            required: true
            type: str
          build_dir:
            required: true
            type: str
  flash_order:
    required: false
    type: seq
    sequence:
      - type: str
'''

schema = yaml.safe_load(DOMAINS_SCHEMA)
logger = logging.getLogger('build_helpers')
# Configure simple logging backend.
formatter = logging.Formatter('%(name)s - %(levelname)s - %(message)s')
handler = logging.StreamHandler()
handler.setFormatter(formatter)
logger.addHandler(handler)


class Domains:

    def __init__(self, data: dict):
        try:
            pykwalify.core.Core(source_data=data,
                                schema_data=schema).validate()
        except pykwalify.errors.SchemaError as e:
            logger.critical(f'malformed domains.yaml: {e}')
            exit(1)

        self._build_dir = data['build_dir']
        self._domains = {
            d['name']: Domain(d['name'], d['build_dir'])
            for d in data['domains']
        }

        # In the YAML data, the values for "default" and "flash_order"
        # must not name any domains that aren't listed under "domains".
        # Now that self._domains has been initialized, we can leverage
        # the common checks in self.get_domain to verify this.
        self._default_domain = self.get_domain(data['default'])
        self._flash_order = self.get_domains(data.get('flash_order', []))

    @staticmethod
    def from_file(domains_file):
        '''Load domains from a domains.yaml file.
        '''
        try:
            with open(domains_file, 'r') as f:
                domains_yaml = f.read()
        except FileNotFoundError:
            logger.critical(f'domains.yaml file not found: {domains_file}')
            exit(1)

        return Domains.from_yaml(domains_yaml)

    @staticmethod
    def from_yaml(domains_yaml):
        '''Load domains from a string with YAML contents.
        '''
        try:
            domains_yaml = yaml.safe_load(domains_yaml)
            return Domains(domains_yaml)
        except yaml.YAMLError as e:
            logger.critical(f'Invalid domains.yaml: {e}')
            exit(1)

    def get_domains(self, names=None, default_flash_order=False):
        if names is None:
            if default_flash_order:
                return self._flash_order
            return list(self._domains.values())
        return list(map(self.get_domain, names))

    def get_domain(self, name):
        found = self._domains.get(name)
        if not found:
            logger.critical(f'domain "{name}" not found, '
                    f'valid domains are: {", ".join(self._domains)}')
            exit(1)
        return found

    def get_default_domain(self):
        return self._default_domain

    def get_top_build_dir(self):
        return self._build_dir


@dataclass
class Domain:

    name: str
    build_dir: str


def zephyr_cmake_build_dir_for_spdx(build_dir_ref):
    '''Map a west ``-d`` build directory to the Zephyr CMake build dir for SPDX.

    Sysbuild writes ``domains.yaml`` at the top-level build directory. When
    *build_dir_ref* is that directory, returns the default image's
    ``build_dir`` from the file. Otherwise returns *build_dir_ref* unchanged.

    Returns:
        tuple: (zephyr_image_build_dir, is_sysbuild_top) where *is_sysbuild_top*
        is True when *build_dir_ref* was the sysbuild top dir listed in
        ``domains.yaml``.
    '''
    abs_ref = os.path.abspath(build_dir_ref)
    domains_file = Path(abs_ref) / 'domains.yaml'
    if not domains_file.is_file():
        return abs_ref, False

    domains = Domains.from_file(str(domains_file))
    top = os.path.abspath(domains.get_top_build_dir())
    if top != abs_ref:
        return abs_ref, False

    default = domains.get_default_domain()
    return os.path.abspath(default.build_dir), True
