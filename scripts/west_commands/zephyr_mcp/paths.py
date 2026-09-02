# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path


def resolve_under_roots(path, roots, cwd=None) -> Path:
    '''Resolve a user-supplied path and check it lies under one of roots.

    Relative paths are taken relative to cwd (default: the first root).
    Symbolic links are resolved before the check. Raises ValueError for
    a path that escapes every root.'''
    resolved = Path(path).expanduser()
    if not resolved.is_absolute():
        resolved = Path(cwd or roots[0]) / resolved
    resolved = resolved.resolve()
    for root in roots:
        if resolved.is_relative_to(Path(root).resolve()):
            return resolved
    allowed = ', '.join(str(root) for root in roots)
    raise ValueError(f'{path} is outside the allowed directories ({allowed})')
