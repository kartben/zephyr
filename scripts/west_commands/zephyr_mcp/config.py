# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from zephyr_ext_common import ZEPHYR_BASE


@dataclass
class ServerConfig:
    '''Settings of one "west mcp" server instance.'''

    topdir: Path
    zephyr_base: Path = ZEPHYR_BASE
    roots: list[Path] = field(default_factory=list)
    allow_hardware: bool = False
    log_dir: Path | None = None
    manifest: object = None
    config: object = None

    def __post_init__(self):
        self.topdir = Path(self.topdir).resolve()
        self.zephyr_base = Path(self.zephyr_base).resolve()
        # The workspace is always allowed; --root adds directories to it.
        roots = [self.topdir] + [Path(root).resolve() for root in self.roots]
        self.roots = list(dict.fromkeys(roots))
        if self.log_dir is None:
            self.log_dir = Path(tempfile.mkdtemp(prefix='west-mcp-'))
        else:
            self.log_dir = Path(self.log_dir).resolve()
            self.log_dir.mkdir(parents=True, exist_ok=True)

    @classmethod
    def from_command(cls, command, args):
        return cls(
            topdir=command.topdir,
            roots=[Path(root) for root in args.root or []],
            allow_hardware=args.allow_hardware,
            log_dir=args.log_dir,
            manifest=command.manifest,
            config=command.config,
        )
