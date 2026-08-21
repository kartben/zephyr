#!/usr/bin/env python3

# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

'''On-disk cache for the hardware listing helpers.

Listing boards, SoCs or shields means parsing and schema validating every
board.yml, soc.yml or shield.yml in the tree. That costs a second or two and is
repeated by every CMake configure, every twister run and every documentation
build, even though the answer almost never changes.

Each listing is therefore split in two phases:

  1. Discovery walks the roots and returns the files the listing will read.
     This is cheap and always runs, so files that appear or disappear are always
     noticed.
  2. Loading parses those files. Its result is stored under a key derived from
     the discovered paths, their contents, the arguments that select what to
     list, and the source of the modules that do the parsing.

Deriving the key from the files the loaders actually read is what makes
invalidation reliable: there is no dependency list to keep up to date, because a
file that is not an input cannot invalidate an entry and one that is always
does. Editing a .dts or .dtsi, for example, changes nothing here -- devicetree
is not parsed by any of the listing helpers. Hashing the loader sources means a
change to the parsing logic or to a schema invalidates every entry too, so there
is no version number to remember to bump.

The cache is an optimization and never a source of truth: an entry that cannot
be read or unpickled is treated as a miss, and a cache directory that cannot be
written to simply means no caching.

Environment:

  ZEPHYR_LIST_CACHE_DISABLE  set to a non-empty value to always recompute.
  ZEPHYR_LIST_CACHE_DIR      directory to store entries in, overriding the
                             per-user cache directory.
'''

import contextlib
import hashlib
import os
import pickle
import platform
import sys
import tempfile
from pathlib import Path

# Entries are named '<namespace>-<digest>.pickle'. Keeping a few per namespace
# lets a cache survive switching back and forth between branches.
MAX_ENTRIES_PER_NAMESPACE = 8

_SCRIPTS_DIR = Path(__file__).parent

# Cached values are Python objects built by this code, so any change to it must
# invalidate every entry.
_CODE_INPUTS = (
    _SCRIPTS_DIR / 'list_cache.py',
    _SCRIPTS_DIR / 'list_boards.py',
    _SCRIPTS_DIR / 'list_hardware.py',
    _SCRIPTS_DIR / 'list_shields.py',
    _SCRIPTS_DIR / 'schemas' / 'board-schema.yaml',
    _SCRIPTS_DIR / 'schemas' / 'soc-schema.yaml',
    _SCRIPTS_DIR / 'schemas' / 'shield-schema.yaml',
)

_MISS = object()


def cached(namespace, inputs, extra, load):
    '''Return load(), reusing a stored result while the inputs are unchanged.

    namespace  short name of the listing, used to group entries.
    inputs     the files load() reads. Missing files are allowed, so that
               creating one is a change like any other.
    extra      values other than file contents that change the result, such as
               a name the listing filters on. Paths do not belong here: they
               are already part of the key, recorded exactly as the loader sees
               them.
    load       called on a miss to produce a picklable result.
    '''
    entry = _entry_path(namespace, inputs, extra)
    if entry is None:
        return load()

    value = _read(entry)
    if value is not _MISS:
        return value

    value = load()
    _write(entry, value, namespace)
    return value


def _entry_path(namespace, inputs, extra):
    # None disables caching for this call.
    directory = cache_dir()
    if directory is None:
        return None

    digest = hashlib.sha256()

    def feed(data):
        # Length prefixed so that no combination of inputs can produce the
        # digest of a different combination.
        digest.update(f'{len(data)}:'.encode())
        digest.update(data)

    for part in extra:
        feed(repr(part).encode())

    for path in (*_CODE_INPUTS, *inputs):
        feed(str(path).encode())
        try:
            feed(Path(path).read_bytes())
        except OSError:
            feed(b'<unreadable>')

    return directory / f'{namespace}-{digest.hexdigest()}.pickle'


def cache_dir():
    '''Directory holding the cache entries, or None if caching is disabled.'''
    if os.environ.get('ZEPHYR_LIST_CACHE_DISABLE'):
        return None

    override = os.environ.get('ZEPHYR_LIST_CACHE_DIR')
    if override:
        return Path(override)

    base = _user_cache_dir()
    return base / 'zephyr' / 'list' if base else None


def _user_cache_dir():
    # Same locations as find_appropriate_cache_directory() in
    # cmake/modules/user_cache.cmake.
    try:
        if platform.system() == 'Darwin':
            return Path.home() / 'Library' / 'Caches'
        if platform.system() == 'Windows':
            local_app_data = os.environ.get('LOCALAPPDATA')
            return Path(local_app_data) / '.cache' if local_app_data else None
        return Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache')
    except RuntimeError:
        # No home directory to expand.
        return None


def _read(entry):
    try:
        with entry.open('rb') as f:
            value = pickle.load(f)
    except Exception:
        # A stale, truncated or otherwise unusable entry is just a miss. The
        # cache must never be able to break a build.
        return _MISS

    # Reading counts as a use, so that entries in active use are not the ones
    # dropped by _prune().
    with contextlib.suppress(OSError):
        os.utime(entry)

    return value


def _write(entry, value, namespace):
    tmp = None
    try:
        entry.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        # Written and renamed into place so that a reader, possibly one of the
        # many parallel twister builds, never sees a half written entry.
        with tempfile.NamedTemporaryFile(dir=entry.parent, delete=False) as f:
            tmp = Path(f.name)
            pickle.dump(value, f, protocol=pickle.HIGHEST_PROTOCOL)
        tmp.replace(entry)
        tmp = None
        _prune(entry.parent, namespace)
    except OSError:
        # A cache directory that cannot be written to simply means no caching.
        pass
    except Exception as e:
        # Anything else means the value itself cannot be stored, which is worth
        # knowing about since the listing would otherwise silently never cache.
        print(f'warning: could not cache {entry.name}: {e}', file=sys.stderr)
    finally:
        if tmp is not None:
            tmp.unlink(missing_ok=True)


def _prune(directory, namespace):
    entries = []
    for entry in directory.glob(f'{namespace}-*.pickle'):
        # Missing entries have been removed by a concurrent prune.
        with contextlib.suppress(OSError):
            entries.append((entry.stat().st_mtime_ns, entry))

    for _, entry in sorted(entries)[:-MAX_ENTRIES_PER_NAMESPACE]:
        entry.unlink(missing_ok=True)
