# Copyright (c) 2026 The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""west strings command for analyzing similar strings in firmware."""

import argparse
import os
import re
from dataclasses import dataclass, field
from pathlib import Path

from west.commands import WestCommand

try:
    from elftools.elf.elffile import ELFFile
    MISSING_REQUIREMENTS = False
except ImportError:
    MISSING_REQUIREMENTS = True


STRINGS_DESCRIPTION = """\
Analyze strings in compiled object files to find similar strings that could
be unified to reduce firmware size.

This command extracts strings from object files (.o) in the build directory,
computes similarity using Levenshtein distance, and suggests optimizations
by grouping similar strings together.

Example usage:
    west strings --build-dir build
    west strings -d build --threshold 0.85 --min-length 10
"""


@dataclass
class StringInfo:
    """Information about a string found in an object file."""
    value: str
    object_file: str
    source_file: str = ""
    target_name: str = ""
    section: str = ".rodata"


@dataclass
class StringGroup:
    """A group of similar strings."""
    strings: list = field(default_factory=list)
    representative: str = ""

    @property
    def potential_savings(self) -> int:
        """Estimate bytes that could be saved by unifying strings."""
        if len(self.strings) <= 1:
            return 0
        # Sum of all string lengths minus the longest one (which would remain)
        lengths = [len(s.value) + 1 for s in self.strings]  # +1 for null terminator
        return sum(lengths) - max(lengths)


def levenshtein_distance(s1: str, s2: str) -> int:
    """Compute the Levenshtein (edit) distance between two strings."""
    if len(s1) < len(s2):
        return levenshtein_distance(s2, s1)

    if len(s2) == 0:
        return len(s1)

    previous_row = range(len(s2) + 1)
    for i, c1 in enumerate(s1):
        current_row = [i + 1]
        for j, c2 in enumerate(s2):
            # j+1 instead of j since previous_row and current_row are one character longer
            insertions = previous_row[j + 1] + 1
            deletions = current_row[j] + 1
            substitutions = previous_row[j] + (c1 != c2)
            current_row.append(min(insertions, deletions, substitutions))
        previous_row = current_row

    return previous_row[-1]


def similarity_ratio(s1: str, s2: str) -> float:
    """Compute similarity ratio between two strings (1.0 = identical)."""
    if not s1 and not s2:
        return 1.0
    if not s1 or not s2:
        return 0.0
    max_len = max(len(s1), len(s2))
    distance = levenshtein_distance(s1, s2)
    return 1.0 - (distance / max_len)


class UnionFind:
    """Union-Find data structure for grouping similar strings."""

    def __init__(self, n: int):
        self.parent = list(range(n))
        self.rank = [0] * n

    def find(self, x: int) -> int:
        if self.parent[x] != x:
            self.parent[x] = self.find(self.parent[x])
        return self.parent[x]

    def union(self, x: int, y: int):
        px, py = self.find(x), self.find(y)
        if px == py:
            return
        if self.rank[px] < self.rank[py]:
            px, py = py, px
        self.parent[py] = px
        if self.rank[px] == self.rank[py]:
            self.rank[px] += 1


def extract_strings_from_section(data: bytes, min_length: int = 4) -> list[str]:
    """Extract null-terminated strings from a section's data."""
    strings = []
    current = []

    for byte in data:
        if byte == 0:  # null terminator
            if current:
                try:
                    s = bytes(current).decode('utf-8')
                    if len(s) >= min_length and is_printable_string(s):
                        strings.append(s)
                except UnicodeDecodeError:
                    pass
                current = []
        elif 32 <= byte < 127:  # printable ASCII
            current.append(byte)
        else:
            # Non-printable byte breaks the string
            current = []

    return strings


def is_printable_string(s: str) -> bool:
    """Check if string looks like a real string (not random data)."""
    if not s:
        return False
    # Must have at least some letters or be a format string
    has_alpha = any(c.isalpha() for c in s)
    is_format = '%' in s
    # Filter out strings that are mostly punctuation/symbols
    alpha_ratio = sum(1 for c in s if c.isalnum() or c.isspace()) / len(s)
    return (has_alpha or is_format) and alpha_ratio > 0.5


def extract_strings_from_object(obj_path: str, min_length: int) -> list[StringInfo]:
    """Extract strings from rodata sections of an object file."""
    strings = []

    try:
        with open(obj_path, 'rb') as f:
            elf = ELFFile(f)

            for section in elf.iter_sections():
                # Look for rodata sections (may have various names, with or without leading dot)
                name = section.name
                if 'rodata' in name.lower():
                    data = section.data()
                    for s in extract_strings_from_section(data, min_length):
                        strings.append(StringInfo(
                            value=s,
                            object_file=obj_path,
                            section=name
                        ))
    except Exception:
        # Skip files that can't be parsed
        pass

    return strings


def extract_strings_from_elf(elf_path: str, min_length: int) -> set[str]:
    """Extract all strings from the final ELF file's rodata sections."""
    strings = set()

    try:
        with open(elf_path, 'rb') as f:
            elf = ELFFile(f)

            for section in elf.iter_sections():
                name = section.name
                # Check rodata sections in final ELF (may or may not have leading dot)
                if 'rodata' in name.lower():
                    data = section.data()
                    for s in extract_strings_from_section(data, min_length):
                        strings.add(s)
    except Exception:
        pass

    return strings


def find_final_elf(build_dir: str) -> str | None:
    """Find the final zephyr.elf file in the build directory."""
    elf_path = Path(build_dir) / 'zephyr' / 'zephyr.elf'
    if elf_path.exists():
        return str(elf_path)
    return None


def find_object_files(build_dir: str) -> list[str]:
    """Find all object files in the build directory."""
    obj_files = []
    build_path = Path(build_dir)

    # Look in CMakeFiles subdirectories for object files
    # Zephyr uses .obj extension, other builds might use .o
    for pattern in ['**/*.c.obj', '**/*.cpp.obj', '**/*.S.obj',
                    '**/*.c.o', '**/*.cpp.o', '**/*.S.o']:
        obj_files.extend(str(p) for p in build_path.glob(pattern))

    return obj_files


def map_object_to_source(obj_path: str, build_dir: str) -> tuple[str, str]:
    """Map an object file path to its source file and target name."""
    # Object files are typically in paths like:
    # build/zephyr/CMakeFiles/zephyr.dir/drivers/gpio/gpio_common.c.obj
    # We extract: source = drivers/gpio/gpio_common.c, target = zephyr

    obj_path_str = str(obj_path)

    # Try to find CMakeFiles pattern (handles both .o and .obj)
    match = re.search(r'CMakeFiles/([^/]+)\.dir/(.+)\.(o|obj)$', obj_path_str)
    if match:
        target_name = match.group(1)
        source_rel = match.group(2)
        return source_rel, target_name

    # Fallback: just use the filename
    return Path(obj_path).stem, "unknown"


def group_similar_strings(strings: list[StringInfo], threshold: float) -> list[StringGroup]:
    """Group strings by similarity using Union-Find."""
    n = len(strings)
    if n == 0:
        return []

    uf = UnionFind(n)

    # Compare all pairs and union similar ones
    for i in range(n):
        for j in range(i + 1, n):
            # Skip if already in same group
            if uf.find(i) == uf.find(j):
                continue
            sim = similarity_ratio(strings[i].value, strings[j].value)
            if sim >= threshold:
                uf.union(i, j)

    # Collect groups
    groups_dict: dict[int, list[StringInfo]] = {}
    for i, s in enumerate(strings):
        root = uf.find(i)
        if root not in groups_dict:
            groups_dict[root] = []
        groups_dict[root].append(s)

    # Filter to groups with more than one string
    groups = []
    for string_list in groups_dict.values():
        if len(string_list) > 1:
            # Sort by string value for consistent output
            string_list.sort(key=lambda x: x.value)
            group = StringGroup(
                strings=string_list,
                representative=string_list[0].value
            )
            groups.append(group)

    # Sort groups by potential savings (highest first)
    groups.sort(key=lambda g: g.potential_savings, reverse=True)

    return groups


def suggest_optimization(strings: list[StringInfo]) -> str:
    """Generate an optimization suggestion for a group of similar strings."""
    values = [s.value for s in strings]

    # Find common prefix
    prefix = os.path.commonprefix(values)
    if len(prefix) > 5:
        return f'Consider common prefix: "{prefix}..." with variable suffix'

    # Check if strings differ by one word
    if len(values) == 2:
        s1, s2 = values[0], values[1]
        words1, words2 = s1.split(), s2.split()
        if len(words1) == len(words2):
            diffs = [(i, w1, w2) for i, (w1, w2) in enumerate(zip(words1, words2)) if w1 != w2]
            if len(diffs) == 1:
                idx, w1, w2 = diffs[0]
                template = words1.copy()
                template[idx] = '%s'
                return f'Unify to: "{" ".join(template)}" with parameter'

    # Generic suggestion
    return 'Consider using a format string with parameters'


class Strings(WestCommand):
    def __init__(self):
        super().__init__(
            'strings',
            'analyze similar strings in firmware to suggest optimizations',
            STRINGS_DESCRIPTION)

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description,
            formatter_class=argparse.RawDescriptionHelpFormatter)

        parser.add_argument(
            '-d', '--build-dir',
            default='build',
            help='Build directory (default: build)')

        parser.add_argument(
            '-t', '--threshold',
            type=float,
            default=0.8,
            help='Similarity threshold (0.0-1.0, default: 0.8)')

        parser.add_argument(
            '-m', '--min-length',
            type=int,
            default=8,
            help='Minimum string length to consider (default: 8)')

        parser.add_argument(
            '-n', '--max-groups',
            type=int,
            default=20,
            help='Maximum number of groups to display (default: 20)')

        parser.add_argument(
            '--all',
            action='store_true',
            help='Show all groups (no limit)')

        parser.add_argument(
            '-v', '--verbose',
            action='store_true',
            help='Show more details')

        return parser

    def do_run(self, args, unknown_args):
        if MISSING_REQUIREMENTS:
            self.die('Missing requirements. Install with: pip install pyelftools')

        build_dir = args.build_dir
        if not os.path.isdir(build_dir):
            self.die(f'Build directory not found: {build_dir}')

        threshold = args.threshold
        if not 0.0 <= threshold <= 1.0:
            self.die('Threshold must be between 0.0 and 1.0')

        min_length = args.min_length
        if min_length < 1:
            self.die('Minimum length must be at least 1')

        self.inf(f'Scanning object files in {build_dir}...')

        # Find all object files
        obj_files = find_object_files(build_dir)
        if not obj_files:
            self.die('No object files found in build directory')

        self.inf(f'Found {len(obj_files)} object files')

        # Extract strings from all object files
        all_strings: list[StringInfo] = []
        for obj_file in obj_files:
            strings = extract_strings_from_object(obj_file, min_length)
            # Add source information
            for s in strings:
                source, target = map_object_to_source(obj_file, build_dir)
                s.source_file = source
                s.target_name = target
            all_strings.extend(strings)

        if not all_strings:
            self.inf('No strings found in object files')
            return

        self.inf(f'Found {len(all_strings)} strings in object files')

        # Find and verify against final ELF
        elf_path = find_final_elf(build_dir)
        if elf_path:
            self.inf(f'Verifying strings against final ELF: {elf_path}')
            elf_strings = extract_strings_from_elf(elf_path, min_length)
            self.inf(f'Found {len(elf_strings)} strings in final ELF')

            # Filter to only strings that appear in the final binary
            all_strings = [s for s in all_strings if s.value in elf_strings]
            self.inf(f'Verified {len(all_strings)} strings appear in final binary')

            if not all_strings:
                self.inf('No strings from object files found in final ELF')
                return
        else:
            self.wrn('Could not find zephyr.elf - reporting all object file strings')

        # Deduplicate identical strings (keep track of all occurrences)
        unique_strings: dict[str, list[StringInfo]] = {}
        for s in all_strings:
            if s.value not in unique_strings:
                unique_strings[s.value] = []
            unique_strings[s.value].append(s)

        # For similarity analysis, use one representative per unique value
        deduped = [infos[0] for infos in unique_strings.values()]
        self.inf(f'Analyzing {len(deduped)} unique strings for similarity...')

        # Group similar strings
        groups = group_similar_strings(deduped, threshold)

        if not groups:
            self.inf('No similar strings found at the given threshold')
            return

        # Display results
        max_groups = len(groups) if args.all else min(len(groups), args.max_groups)
        total_savings = sum(g.potential_savings for g in groups)

        print(f'\n{"=" * 60}')
        print(f'Similar String Groups (threshold: {threshold})')
        print(f'{"=" * 60}\n')

        for i, group in enumerate(groups[:max_groups], 1):
            print(f'Group {i} ({len(group.strings)} strings, '
                  f'potential savings: ~{group.potential_savings} bytes):')

            for s in group.strings:
                # Truncate long strings for display
                display_val = s.value
                if len(display_val) > 60:
                    display_val = display_val[:57] + '...'
                print(f'  "{display_val}"')
                print(f'    └─ {s.source_file} → {s.target_name}')

                # Show duplicates if verbose
                if args.verbose and s.value in unique_strings:
                    occurrences = unique_strings[s.value]
                    if len(occurrences) > 1:
                        print(f'       ({len(occurrences)} occurrences in different files)')

            suggestion = suggest_optimization(group.strings)
            print(f'\n  💡 Suggestion: {suggestion}\n')

        if len(groups) > max_groups:
            print(f'... and {len(groups) - max_groups} more groups (use --all to see all)\n')

        print(f'{"=" * 60}')
        print('Summary:')
        print(f'  Total similar groups: {len(groups)}')
        print(f'  Estimated potential savings: ~{total_savings} bytes')
        print(f'{"=" * 60}')
