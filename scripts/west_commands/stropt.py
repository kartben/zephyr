#!/usr/bin/env python3
# Copyright (c) 2024 The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""
West command to diagnose similar strings in binary firmware and suggest optimizations.

This command extracts strings from binary firmware, identifies similar strings using
Levenshtein distance, and suggests optimizations by homogenizing them.
"""

import re
import struct
from collections import defaultdict
from pathlib import Path
from textwrap import dedent

from west.commands import WestCommand

try:
    from elftools.elf.elffile import ELFFile
    from intelhex import IntelHex
    MISSING_REQUIREMENTS = False
except ImportError:
    MISSING_REQUIREMENTS = True


def levenshtein_distance(s1, s2):
    """
    Calculate the Levenshtein distance between two strings.

    The Levenshtein distance is the minimum number of single-character edits
    (insertions, deletions, or substitutions) required to change one string
    into the other.

    Args:
        s1: First string
        s2: Second string

    Returns:
        Integer representing the edit distance
    """
    if len(s1) < len(s2):
        return levenshtein_distance(s2, s1)

    if len(s2) == 0:
        return len(s1)

    previous_row = range(len(s2) + 1)
    for i, c1 in enumerate(s1):
        current_row = [i + 1]
        for j, c2 in enumerate(s2):
            # Cost of insertions, deletions, or substitutions
            insertions = previous_row[j + 1] + 1
            deletions = current_row[j] + 1
            substitutions = previous_row[j] + (c1 != c2)
            current_row.append(min(insertions, deletions, substitutions))
        previous_row = current_row

    return previous_row[-1]


def similarity_ratio(s1, s2):
    """
    Calculate similarity ratio between two strings (0.0 to 1.0).

    Args:
        s1: First string
        s2: Second string

    Returns:
        Float representing similarity (1.0 = identical, 0.0 = completely different)
    """
    max_len = max(len(s1), len(s2))
    if max_len == 0:
        return 1.0
    distance = levenshtein_distance(s1, s2)
    return 1.0 - (distance / max_len)


def extract_strings_from_bytes(data, min_length=4):
    """
    Extract printable ASCII strings from binary data.

    Args:
        data: Binary data bytes
        min_length: Minimum string length to extract (default: 4)

    Returns:
        List of tuples (offset, string)
    """
    strings = []
    # Match sequences of printable ASCII characters
    pattern = rb'[\x20-\x7e]{' + str(min_length).encode() + rb',}'

    for match in re.finditer(pattern, data):
        offset = match.start()
        string = match.group(0).decode('ascii')
        strings.append((offset, string))

    return strings


# Based on scripts/build/uf2conv.py
def convert_from_uf2(buf):
    """
    Convert UF2 format to raw binary data.

    Args:
        buf: UF2 format binary data

    Returns:
        Raw binary data
    """
    UF2_MAGIC_START0 = 0x0A324655  # First magic number ('UF2\n')
    UF2_MAGIC_START1 = 0x9E5D5157  # Second magic number
    PADDING_SIZE = 4  # Word alignment
    numblocks = len(buf) // 512
    curraddr = None
    outp = []

    for blockno in range(numblocks):
        ptr = blockno * 512
        block = buf[ptr:ptr + 512]
        hd = struct.unpack(b'<IIIIIIII', block[0:32])
        if hd[0] != UF2_MAGIC_START0 or hd[1] != UF2_MAGIC_START1:
            continue
        if hd[2] & 1:
            # NO-flash flag set; skip block
            continue
        datalen = hd[4]
        if datalen > 476:
            continue
        newaddr = hd[3]
        if curraddr is None:
            curraddr = newaddr
        padding = newaddr - curraddr
        if padding < 0 or padding > 10*1024*1024 or padding % PADDING_SIZE != 0:
            continue
        while padding > 0:
            padding -= PADDING_SIZE
            outp.extend(b'\x00' * PADDING_SIZE)
        outp.append(block[32 : 32 + datalen])
        curraddr = newaddr + datalen

    return b''.join(outp)


class Stropt(WestCommand):
    """West command for string optimization analysis in binary firmware."""

    EXTENSIONS = ['bin', 'hex', 'elf', 'uf2']

    def __init__(self):
        super().__init__(
            'stropt',
            'diagnose similar strings in binary firmware',
            dedent('''
            Analyze binary firmware to find similar strings and suggest
            optimizations by homogenizing them to reduce binary size.

            This command extracts strings from the firmware binary, compares
            them using Levenshtein distance, and groups similar strings together.
            '''))

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            description=self.description)

        parser.add_argument(
            'file',
            type=str,
            help='Binary firmware file to analyze')

        parser.add_argument(
            '--file-type',
            type=str,
            choices=self.EXTENSIONS,
            help='File type (auto-detected if not specified)')

        parser.add_argument(
            '--min-length',
            type=int,
            default=4,
            help='Minimum string length to extract (default: 4)')

        parser.add_argument(
            '--similarity',
            type=float,
            default=0.7,
            help='Similarity threshold (0.0-1.0, default: 0.7)')

        parser.add_argument(
            '--min-savings',
            type=int,
            default=10,
            help='Minimum bytes saved to report a group (default: 10)')

        parser.add_argument(
            '--max-groups',
            type=int,
            default=20,
            help='Maximum number of groups to display (default: 20)')

        return parser

    def guess_file_type(self, file_path):
        """Guess file type from extension."""
        suffix = Path(file_path).suffix.lower()
        if suffix in ['.bin']:
            return 'bin'
        elif suffix in ['.hex']:
            return 'hex'
        elif suffix in ['.elf']:
            return 'elf'
        elif suffix in ['.uf2']:
            return 'uf2'
        else:
            # Try to detect by reading the file
            with open(file_path, 'rb') as f:
                magic = f.read(4)
                if magic == b'\x7fELF':
                    return 'elf'
                # UF2 magic: 'UF2\n' (0x0A324655 in little-endian)
                elif magic == b'UF2\n':
                    return 'uf2'
            return 'bin'

    def get_image_data(self, file_path, file_type):
        """Load binary data from file based on type."""
        with open(file_path, 'rb') as f:
            if file_type == 'elf':
                elf = ELFFile(f)
                # Extract all loadable segments
                data = bytearray()
                for segment in elf.iter_segments():
                    if segment['p_type'] == 'PT_LOAD':
                        data.extend(segment.data())
                return bytes(data)

            elif file_type == 'hex':
                ih = IntelHex(f)
                # Convert to binary
                min_addr = ih.minaddr()
                max_addr = ih.maxaddr()
                return ih.tobinarray(start=min_addr, end=max_addr).tobytes()

            elif file_type == 'uf2':
                buf = f.read()
                return convert_from_uf2(buf)

            else:  # bin
                return f.read()

    def find_similar_groups(self, strings, similarity_threshold):
        """
        Group similar strings together.

        Args:
            strings: List of (offset, string) tuples
            similarity_threshold: Minimum similarity ratio (0.0-1.0)

        Returns:
            List of groups, where each group is a list of (offset, string) tuples
        """
        # Remove duplicates but keep track of all occurrences
        string_occurrences = defaultdict(list)
        for offset, string in strings:
            string_occurrences[string].append(offset)

        unique_strings = list(string_occurrences.keys())
        visited = set()
        groups = []

        for i, s1 in enumerate(unique_strings):
            if s1 in visited:
                continue

            group = [(string_occurrences[s1], s1)]
            visited.add(s1)

            for _j, s2 in enumerate(unique_strings[i+1:], i+1):
                if s2 in visited:
                    continue

                sim = similarity_ratio(s1, s2)
                if sim >= similarity_threshold:
                    group.append((string_occurrences[s2], s2))
                    visited.add(s2)

            # Only include groups with more than one unique string
            if len(group) > 1:
                groups.append(group)

        return groups

    def calculate_potential_savings(self, group):
        """
        Calculate potential byte savings for a group of similar strings.

        The savings come from recognizing that similar strings could be refactored
        to use a common base string with format specifiers or by consolidation.

        For example:
        - "Error: Connection failed" (26 bytes)
        - "Error: Connection timeout" (27 bytes)
        - "Error: Connection refused" (27 bytes)

        Could become:
        - "Error: Connection %s" (20 bytes) + "failed" (7) + "timeout" (8) + "refused" (8)
        - Total: 20 + 7 + 8 + 8 = 43 bytes vs 80 bytes original

        Args:
            group: List of (occurrences, string) tuples

        Returns:
            Tuple of (total_bytes, saved_bytes, num_strings, num_occurrences)
        """
        total_bytes = 0
        num_strings = len(group)
        num_occurrences = 0

        # Calculate total bytes currently used
        for occurrences, string in group:
            count = len(occurrences)
            string_len = len(string) + 1  # +1 for null terminator
            total_bytes += string_len * count
            num_occurrences += count

        # Find the longest common prefix among all strings
        if num_strings < 2:
            return total_bytes, 0, num_strings, num_occurrences

        strings = [s for _, s in group]

        # Find longest common prefix
        common_prefix = strings[0]
        for s in strings[1:]:
            while not s.startswith(common_prefix) and len(common_prefix) > 0:
                common_prefix = common_prefix[:-1]

        # Find longest common suffix
        common_suffix = strings[0]
        for s in strings[1:]:
            while not s.endswith(common_suffix) and len(common_suffix) > 0:
                common_suffix = common_suffix[:-1]

        # Avoid overlap between prefix and suffix
        if len(common_prefix) + len(common_suffix) > min(len(s) for s in strings):
            common_suffix = ''

        # Calculate savings from factoring out common parts
        # Base string (with format specifier): prefix + "%s" + suffix
        base_len = len(common_prefix) + 2 + len(common_suffix) + 1  # +1 for null

        # Variable parts for each string
        variable_bytes = sum(
            len(s) - len(common_prefix) - len(common_suffix) + 1  # +1 for null
            for s in strings
        )

        # Optimized total: base string stored once + variable parts
        optimized_bytes = base_len + variable_bytes

        # Add overhead for references (pointer per occurrence, assume 4 bytes each)
        reference_overhead = num_occurrences * 4
        optimized_bytes += reference_overhead

        saved_bytes = total_bytes - optimized_bytes

        return total_bytes, saved_bytes, num_strings, num_occurrences

    def suggest_unified_string(self, group):
        """
        Suggest a unified string for a group of similar strings.

        Args:
            group: List of (occurrences, string) tuples

        Returns:
            Suggested unified string
        """
        # Use the longest string as the base
        longest = max(group, key=lambda x: len(x[1]))
        return longest[1]

    def do_run(self, args, _):
        if MISSING_REQUIREMENTS:
            self.die('one or more Python dependencies were missing; '
                    'see the getting started guide for details on '
                    'how to fix (pyelftools, intelhex required)')

        # Determine file type
        file_type = args.file_type
        if not file_type:
            file_type = self.guess_file_type(args.file)
            self.dbg(f'Auto-detected file type: {file_type}')

        # Load binary data
        self.inf(f'Analyzing {args.file}...')
        try:
            image_data = self.get_image_data(args.file, file_type)
        except Exception as e:
            self.die(f'Failed to load file: {e}')

        # Extract strings
        self.inf(f'Extracting strings (min length: {args.min_length})...')
        strings = extract_strings_from_bytes(image_data, args.min_length)
        self.inf(f'Found {len(strings)} strings')

        if len(strings) == 0:
            self.inf('No strings found in the binary')
            return

        # Find similar groups
        self.inf(f'Analyzing similarity (threshold: {args.similarity})...')
        groups = self.find_similar_groups(strings, args.similarity)

        if len(groups) == 0:
            self.inf('No similar strings found')
            return

        # Sort groups by potential savings
        group_analysis = []
        for group in groups:
            total_bytes, saved_bytes, num_strings, num_occurrences = \
                self.calculate_potential_savings(group)

            if saved_bytes >= args.min_savings:
                group_analysis.append({
                    'group': group,
                    'total_bytes': total_bytes,
                    'saved_bytes': saved_bytes,
                    'num_strings': num_strings,
                    'num_occurrences': num_occurrences,
                })

        group_analysis.sort(key=lambda x: x['saved_bytes'], reverse=True)

        # Display results
        total_potential_savings = sum(g['saved_bytes'] for g in group_analysis)
        self.inf(f'\nFound {len(group_analysis)} groups of similar strings')
        self.inf(f'Total potential savings: {total_potential_savings} bytes\n')

        # Display top groups
        max_display = min(args.max_groups, len(group_analysis))
        for idx, analysis in enumerate(group_analysis[:max_display], 1):
            group = analysis['group']
            saved_bytes = analysis['saved_bytes']
            num_strings = analysis['num_strings']
            num_occurrences = analysis['num_occurrences']

            self.inf(f'Group {idx}: {num_strings} similar strings, '
                    f'{num_occurrences} total occurrences')
            self.inf(f'  Potential savings: {saved_bytes} bytes')

            # Show the strings in the group
            for occurrences, string in group:
                preview = string[:50] + '...' if len(string) > 50 else string
                # Show offsets where this string appears
                offset_list = ', '.join(f'0x{off:x}' for off in occurrences[:5])
                if len(occurrences) > 5:
                    offset_list += f', ... ({len(occurrences) - 5} more)'
                self.inf(f'    [{len(occurrences)}x] "{preview}"')
                self.inf(f'        Offsets: {offset_list}')

            # Suggest unified string
            suggested = self.suggest_unified_string(group)
            preview = suggested[:50] + '...' if len(suggested) > 50 else suggested
            self.inf(f'  Suggested unified string: "{preview}"\n')

        if len(group_analysis) > max_display:
            remaining = len(group_analysis) - max_display
            remaining_savings = sum(g['saved_bytes']
                                   for g in group_analysis[max_display:])
            self.inf(f'... and {remaining} more groups '
                    f'({remaining_savings} bytes potential savings)')
