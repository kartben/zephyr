# Copyright (c) 2026 The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Loader for the Zephyr security hardening database.

The database (scripts/kconfig/hardening.yaml by default) is a YAML file
validated against scripts/schemas/hardening-schema.yaml. It contains named
hardening profiles and per-Kconfig-symbol rules, where each rule recommends
either an exact value or an integer min/max constraint, together with a
rationale.

This module is shared between the 'hardenconfig' build target
(scripts/kconfig/hardenconfig.py) and the compliance check
(scripts/ci/check_compliance.py) so that both always agree on how the
database is parsed and interpreted.
"""

from pathlib import Path

import jsonschema
import yaml
from jsonschema.exceptions import best_match

try:
    # Use the C LibYAML parser if available, rather than the Python parser.
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader  # type: ignore

DEFAULT_DATABASE_PATH = Path(__file__).parent / 'hardening.yaml'

SCHEMA_PATH = Path(__file__).parents[1] / 'schemas' / 'hardening-schema.yaml'
with open(SCHEMA_PATH, 'rb') as f:
    HARDENING_SCHEMA = yaml.load(f.read(), Loader=SafeLoader)

VALIDATOR_CLASS = jsonschema.validators.validator_for(HARDENING_SCHEMA)
VALIDATOR_CLASS.check_schema(HARDENING_SCHEMA)
VALIDATOR = VALIDATOR_CLASS(HARDENING_SCHEMA)

# Profile every rule belongs to when it does not list any explicitly.
DEFAULT_PROFILE = 'base'


class HardeningDatabaseError(Exception):
    """Raised when the hardening database is malformed."""


def _normalize_value(value):
    """Normalize a rule 'value' to its .config string representation.

    YAML resolves 'true'/'false' (and 'yes'/'no', 'on'/'off') to booleans
    while 'y'/'n' stay strings, and integers may be written bare. Normalizing
    here makes all those spellings compare correctly against
    kconfiglib's Symbol.str_value.
    """
    if isinstance(value, bool):
        return 'y' if value else 'n'
    return str(value)


def _normalize_rule(rule):
    return {
        'value': _normalize_value(rule['value']) if 'value' in rule else None,
        'min': rule.get('min'),
        'max': rule.get('max'),
        'rationale': rule['rationale'].strip(),
        'references': rule.get('references', []),
        'profiles': rule.get('profiles', [DEFAULT_PROFILE]),
    }


def load_database(paths=None):
    """Load, schema-validate and merge hardening database file(s).

    ``paths`` is an iterable of YAML file paths, all using the same schema;
    later files may add profiles and rules, and override same-named ones.
    When ``paths`` is None the in-tree database is loaded.

    Returns a dict with 'profiles' (as in the YAML) and 'rules' (mapping
    symbol name to a normalized rule dict with keys 'value', 'min', 'max',
    'rationale', 'references' and 'profiles').

    Raises HardeningDatabaseError on unreadable, unparseable or
    schema-invalid input.
    """
    if paths is None:
        paths = [DEFAULT_DATABASE_PATH]

    profiles = {}
    rules = {}
    for path in paths:
        try:
            with open(path, 'rb') as db_file:
                data = yaml.load(db_file.read(), Loader=SafeLoader)
        except OSError as e:
            raise HardeningDatabaseError(f'{path}: {e}') from e
        except yaml.YAMLError as e:
            raise HardeningDatabaseError(f'{path}: invalid YAML: {e}') from e

        errors = list(VALIDATOR.iter_errors(data))
        if errors:
            err = best_match(errors)
            raise HardeningDatabaseError(
                f'{path}: {err.message} in {err.json_path}')

        profiles.update(data['profiles'])
        for name, rule in data['rules'].items():
            rules[name] = _normalize_rule(rule)

    return {'profiles': profiles, 'rules': rules}


def profile_closure(profiles, name):
    """Return the set of profile names 'name' transitively extends,
    including 'name' itself.

    Raises HardeningDatabaseError on an unknown profile or an 'extends'
    cycle.
    """
    closure = []
    while name is not None:
        if name in closure:
            cycle = ' -> '.join(closure + [name])
            raise HardeningDatabaseError(
                f"profile 'extends' cycle: {cycle}")
        if name not in profiles:
            known = ', '.join(sorted(profiles))
            raise HardeningDatabaseError(
                f"unknown profile '{name}' (known profiles: {known})")
        closure.append(name)
        name = profiles[name].get('extends')
    return set(closure)


def rules_for_profile(database, profile):
    """Return the rules active in 'profile', following 'extends'."""
    active = profile_closure(database['profiles'], profile)
    return {
        name: rule
        for name, rule in database['rules'].items()
        if active.intersection(rule['profiles'])
    }


def check_profile_integrity(database):
    """Return a list of error strings for profile-related inconsistencies:
    rules tagged with undefined profiles, undefined 'extends' targets, or
    'extends' cycles.
    """
    errors = []
    profiles = database['profiles']

    for name in profiles:
        try:
            profile_closure(profiles, name)
        except HardeningDatabaseError as e:
            errors.append(str(e))

    for symbol, rule in database['rules'].items():
        for tag in rule['profiles']:
            if tag not in profiles:
                errors.append(
                    f"rule {symbol}: unknown profile '{tag}'")

    return errors


def recommended_str(rule):
    """Human-readable recommendation for a rule."""
    if rule['value'] is not None:
        return rule['value']
    if rule['min'] is not None and rule['max'] is not None:
        return f">={rule['min']}, <={rule['max']}"
    if rule['min'] is not None:
        return f">={rule['min']}"
    return f"<={rule['max']}"


def evaluate_rule(rule, symbol):
    """Evaluate a rule against a kconfiglib Symbol (or None if the symbol
    does not exist in the Kconfig tree). Returns 'PASS', 'FAIL' or 'NA'.
    """
    if symbol is None:
        return 'NA'

    current = symbol.str_value
    if rule['value'] is not None:
        return 'PASS' if current == rule['value'] else 'FAIL'

    try:
        # Base 0 handles both int and hex symbols.
        current_int = int(current, 0)
    except ValueError:
        return 'FAIL'

    if rule['min'] is not None and current_int < rule['min']:
        return 'FAIL'
    if rule['max'] is not None and current_int > rule['max']:
        return 'FAIL'
    return 'PASS'
