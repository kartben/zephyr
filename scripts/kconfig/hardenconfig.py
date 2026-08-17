#!/usr/bin/env python3

# Copyright (c) 2019-2024 Intel Corporation
# Copyright (c) 2026 The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Check the current configuration against Zephyr's security hardening
database (scripts/kconfig/hardening.yaml) and report deviations, with the
rationale for each recommendation.

Environment variables (typically set through CMake cache variables of the
same name, e.g. 'west build -t hardenconfig -- -DHARDENCONFIG_PROFILE=base'):

- HARDENCONFIG_PROFILE: hardening profile to check against (default: strict)
- HARDENCONFIG_SHOW_ALL: also show passing and non-applicable options
- HARDENCONFIG_STRICT: exit with an error code if any check fails
- HARDENCONFIG_JSON: path to additionally write results to, as JSON
- HARDENCONFIG_EXTRA_SOURCES: semicolon-separated list of additional
  hardening database YAML files; later files may add profiles and rules,
  and override same-named rules
"""

import json
import os
import sys
import textwrap

import hardeninglib
from kconfiglib import Symbol, standard_kconfig
from tabulate import tabulate

DEFAULT_PROFILE = 'strict'

# Rationale for options flagged through Kconfig marker symbols rather than
# through the hardening database.
MARKER_RATIONALE = {
    'EXPERIMENTAL': 'Selects EXPERIMENTAL: the implementation is at an '
                    'experimental stage.',
    'DEPRECATED': 'Selects DEPRECATED: the feature is deprecated.',
    'NOT_SECURE': 'Selects NOT_SECURE: the feature is inherently not '
                  'secure.',
}


def env_flag(name):
    return os.environ.get(name, '') not in ('', 'n', '0')


class Option:
    def __init__(self, name, recommended, rationale, result, current=None,
                 symbol=None, references=()):
        self.name = name
        self.recommended = recommended
        self.rationale = rationale
        self.result = result
        self.current = current
        self.symbol = symbol
        self.references = list(references)

    @property
    def visible(self):
        return self.symbol is not None and self.symbol.visibility != 0


def hardenconfig(kconf):
    kconf.load_config()

    paths = [hardeninglib.DEFAULT_DATABASE_PATH]
    extra_sources = os.environ.get('HARDENCONFIG_EXTRA_SOURCES', '')
    paths.extend(p for p in extra_sources.split(';') if p)

    profile = os.environ.get('HARDENCONFIG_PROFILE', '') or DEFAULT_PROFILE

    try:
        database = hardeninglib.load_database(paths)
        errors = hardeninglib.check_profile_integrity(database)
        if errors:
            raise hardeninglib.HardeningDatabaseError('\n'.join(errors))
        rules = hardeninglib.rules_for_profile(database, profile)
    except hardeninglib.HardeningDatabaseError as e:
        sys.exit(f'hardenconfig: invalid hardening database: {e}')

    options = compare_with_hardening_database(kconf, rules)

    json_path = os.environ.get('HARDENCONFIG_JSON', '')
    if json_path:
        write_json(json_path, profile, options)

    n_fail = display_results(options, profile)

    if env_flag('HARDENCONFIG_STRICT') and n_fail:
        sys.exit(1)


def compare_with_hardening_database(kconf, rules):
    options = []

    for name, rule in rules.items():
        symbol = kconf.syms.get(name)
        current = symbol.str_value if symbol is not None else None
        options.append(Option(name=name,
                              recommended=hardeninglib.recommended_str(rule),
                              rationale=rule['rationale'],
                              result=hardeninglib.evaluate_rule(rule, symbol),
                              current=current,
                              symbol=symbol,
                              references=rule['references']))

    # Independently of the database, flag options marked in Kconfig itself
    # as experimental, deprecated or not secure.
    off_rule = {'value': 'n', 'min': None, 'max': None}
    markers = {marker: kconf.syms[marker] for marker in MARKER_RATIONALE}
    seen = set(rules)
    for node in kconf.node_iter():
        if not isinstance(node.item, Symbol) or node.item.name in seen:
            continue
        for select in node.selects:
            for marker, marker_sym in markers.items():
                if marker_sym in select:
                    seen.add(node.item.name)
                    options.append(Option(
                        name=node.item.name,
                        recommended='n',
                        rationale=MARKER_RATIONALE[marker],
                        result=hardeninglib.evaluate_rule(off_rule, node.item),
                        current=node.item.str_value,
                        symbol=node.item))
                    break
            if node.item.name in seen:
                break

    return options


def write_json(path, profile, options):
    results = [{
        'name': f'CONFIG_{opt.name}',
        'current': opt.current,
        'recommended': opt.recommended,
        'result': opt.result,
        'visible': opt.visible,
        'rationale': opt.rationale,
        'references': opt.references,
    } for opt in options]
    with open(path, 'w') as out:
        json.dump({'profile': profile, 'results': results}, out, indent=2)
        out.write('\n')


def display_results(options, profile):
    table_data = []
    headers = ['Name', 'Current', 'Recommended', 'Check result', 'Rationale']

    show_all = env_flag('HARDENCONFIG_SHOW_ALL')
    n_fail = 0
    for opt in options:
        if opt.result == 'FAIL' and opt.visible:
            n_fail += 1
        elif not show_all:
            continue
        table_data.append([f'CONFIG_{opt.name}', opt.current, opt.recommended,
                           opt.result, textwrap.fill(opt.rationale, width=50)])

    print(f'Hardening report for profile: {profile}')
    if table_data:
        print(tabulate(table_data, headers=headers, tablefmt='grid'))
    if n_fail:
        print(f'{n_fail} option(s) deviate from the hardening recommendations.')
    else:
        print('No deviations from the hardening recommendations.')
    print()
    return n_fail


def main():
    hardenconfig(standard_kconfig(__doc__))


if __name__ == '__main__':
    main()
