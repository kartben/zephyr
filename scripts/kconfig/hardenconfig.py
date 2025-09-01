#!/usr/bin/env python3

# Copyright (c) 2019-2024 Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import yaml
import os

from kconfiglib import standard_kconfig
from tabulate import tabulate


def hardenconfig(kconf):
    kconf.load_config()

    hardened_kconf_filename = os.path.join(
        os.environ['ZEPHYR_BASE'], 'scripts', 'kconfig', 'hardened.yaml'
    )

    options = compare_with_hardened_conf(kconf, hardened_kconf_filename)

    display_results(options)


class Option:
    def __init__(self, name, recommended, current=None, symbol=None, explanation=None):
        self.name = name
        self.recommended = recommended
        self.current = current
        self.symbol = symbol
        self.explanation = explanation

        if current is None:
            self.result = 'NA'
        elif recommended == current:
            self.result = 'PASS'
        else:
            self.result = 'FAIL'


def compare_with_hardened_conf(kconf, hardened_kconf_filename):
    options = []

    with open(hardened_kconf_filename, 'r') as yamlfile:
        hardened_config = yaml.safe_load(yamlfile)

        for name, config in hardened_config.items():
            if isinstance(config, dict):
                recommended = config.get('recommended')
                explanation = config.get('explanation')
            else:
                # Handle legacy format where value is directly specified
                recommended = config
                explanation = None

            try:
                symbol = kconf.syms[name]
                current = symbol.str_value
            except KeyError:
                symbol = None
                current = None

            options.append(
                Option(name=name, current=current, recommended=recommended,
                      symbol=symbol, explanation=explanation)
            )

    for node in kconf.node_iter():
        for select in node.selects:
            if (
                kconf.syms["EXPERIMENTAL"] in select
                or kconf.syms["DEPRECATED"] in select
                or kconf.syms["NOT_SECURE"] in select
            ):
                options.append(
                    Option(
                        name=node.item.name,
                        current=node.item.str_value,
                        recommended='n',
                        symbol=node.item,
                        explanation="Automatically flagged as insecure due to EXPERIMENTAL/DEPRECATED/NOT_SECURE dependency"
                    )
                )

    return options


def display_results(options):
    table_data = []
    headers = ['Name', 'Current', 'Recommended', 'Check result', 'Explanation']

    # results, only printing options that have failed for now. It simplify the readability.
    # TODO: add command line option to show all results
    for opt in options:
        if opt.result == 'FAIL' and opt.symbol.visibility != 0:
            explanation = opt.explanation if opt.explanation else "No explanation provided"
            table_data.append([f'CONFIG_{opt.name}', opt.current, opt.recommended, opt.result, explanation])

    if table_data:
        print(tabulate(table_data, headers=headers, tablefmt='grid'))
    print()


def main():
    hardenconfig(standard_kconfig())


if __name__ == '__main__':
    main()
