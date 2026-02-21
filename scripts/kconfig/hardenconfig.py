#!/usr/bin/env python3

# Copyright (c) 2019-2024 Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import re

from kconfiglib import standard_kconfig
from tabulate import tabulate

HARDENED_RE = re.compile(r'Hardened value:\s*(.+)')


def hardenconfig(kconf):
    kconf.load_config()

    options = compare_with_hardened_conf(kconf)

    display_results(options)


class Option:
    def __init__(self, name, recommended, current=None, symbol=None):
        self.name = name
        self.recommended = recommended
        self.current = current
        self.symbol = symbol

        if current is None:
            self.result = 'NA'
        elif recommended == current:
            self.result = 'PASS'
        else:
            self.result = 'FAIL'


def compare_with_hardened_conf(kconf):
    options = []
    seen = set()

    for node in kconf.node_iter():
        if not hasattr(node.item, 'name'):
            continue

        if node.help:
            match = HARDENED_RE.search(node.help)
            if match and node.item.name not in seen:
                seen.add(node.item.name)
                recommended = match.group(1).strip()
                options.append(
                    Option(
                        name=node.item.name,
                        current=node.item.str_value,
                        recommended=recommended,
                        symbol=node.item,
                    )
                )

        for select in node.selects:
            if (
                kconf.syms["EXPERIMENTAL"] in select
                or kconf.syms["DEPRECATED"] in select
                or kconf.syms["NOT_SECURE"] in select
            ):
                if node.item.name not in seen:
                    seen.add(node.item.name)
                    options.append(
                        Option(
                            name=node.item.name,
                            current=node.item.str_value,
                            recommended='n',
                            symbol=node.item,
                        )
                    )

    return options


def display_results(options):
    table_data = []
    headers = ['Name', 'Current', 'Recommended', 'Check result']

    # results, only printing options that have failed for now. It simplify the readability.
    # TODO: add command line option to show all results
    for opt in options:
        if opt.result == 'FAIL' and opt.symbol.visibility != 0:
            table_data.append([f'CONFIG_{opt.name}', opt.current, opt.recommended, opt.result])

    if table_data:
        print(tabulate(table_data, headers=headers, tablefmt='grid'))
    print()


def main():
    hardenconfig(standard_kconfig())


if __name__ == '__main__':
    main()
