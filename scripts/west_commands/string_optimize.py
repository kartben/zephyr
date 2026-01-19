#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
West command to analyze and optimize strings in firmware images.

This command analyzes string literals in compiled object files and the final
ELF binary to identify similar strings that could be deduplicated or
homogenized to reduce firmware size.
"""

import re
import subprocess
from pathlib import Path
from textwrap import dedent

from west.commands import WestCommand

try:
    from elftools.elf.elffile import ELFFile

    MISSING_REQUIREMENTS = False
except ImportError:
    MISSING_REQUIREMENTS = True


def levenshtein_distance(s1, s2):
    """
    Calculate the Levenshtein distance between two strings.

    Args:
            s1: First string
            s2: Second string

    Returns:
            int: The Levenshtein distance
    """
    if len(s1) < len(s2):
        return levenshtein_distance(s2, s1)

    if len(s2) == 0:
        return len(s1)

    previous_row = range(len(s2) + 1)
    for i, c1 in enumerate(s1):
        current_row = [i + 1]
        for j, c2 in enumerate(s2):
            # cost of insertions, deletions, or substitutions
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
            float: Similarity ratio (1.0 = identical, 0.0 = completely different)
    """
    max_len = max(len(s1), len(s2))
    if max_len == 0:
        return 1.0
    distance = levenshtein_distance(s1, s2)
    return 1.0 - (distance / max_len)


class StringOptimize(WestCommand):
    """West command for analyzing and optimizing strings in firmware."""

    def __init__(self):
        super().__init__(
            'string-optimize',
            'analyze and optimize strings in firmware',
            dedent(
                '''
            Analyze string literals in firmware to identify similar strings
            that could be deduplicated or homogenized to reduce firmware size.

            This command extracts strings from object files and the ELF binary,
            computes similarity using Levenshtein distance, and suggests
            optimizations.
            '''
            ),
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(self.name, help=self.help, description=self.description)

        parser.add_argument(
            '-d', '--build-dir', required=True, help='Build directory containing compiled artifacts'
        )

        parser.add_argument(
            '-t',
            '--threshold',
            type=float,
            default=0.7,
            help='Similarity threshold (0.0-1.0, default: 0.7)',
        )

        parser.add_argument(
            '-m',
            '--min-length',
            type=int,
            default=10,
            help='Minimum string length to analyze (default: 10)',
        )

        parser.add_argument(
            '-f',
            '--format',
            choices=['text', 'json'],
            default='text',
            help='Output format (default: text)',
        )

        parser.add_argument('-o', '--output', help='Output file (default: stdout)')

        parser.add_argument(
            '--source',
            choices=['elf', 'objects', 'both'],
            default='elf',
            help=(
                'Source of strings: elf (final binary), objects (object files), '
                'or both (default: elf)'
            ),
        )

        return parser

    def do_run(self, args, unknown_args):
        if MISSING_REQUIREMENTS:
            raise RuntimeError('pyelftools is required; install it with: pip install pyelftools')

        self.build_dir = Path(args.build_dir)
        if not self.build_dir.exists():
            self.die(f"Build directory not found: {self.build_dir}")

        self.threshold = args.threshold
        self.min_length = args.min_length
        self.format = args.format
        self.output_file = args.output

        # Extract strings based on source
        # Dictionary mapping strings to list of source files
        string_origins = {}

        if args.source in ['elf', 'both']:
            elf_file = self.find_elf_file()
            if elf_file:
                self.inf(f"Extracting strings from ELF: {elf_file}")
                elf_strings = self.extract_strings_from_elf(elf_file)
                for string in elf_strings:
                    if string not in string_origins:
                        string_origins[string] = []
                    string_origins[string].append(str(elf_file.relative_to(self.build_dir)))
            else:
                self.wrn("No ELF file found in build directory")

        if args.source in ['objects', 'both']:
            obj_files = self.find_object_files()
            if obj_files:
                self.inf(f"Extracting strings from {len(obj_files)} object files")
                for obj_file in obj_files:
                    obj_strings = self.extract_strings_from_object(obj_file)
                    for string in obj_strings:
                        if string not in string_origins:
                            string_origins[string] = []
                        origin = str(obj_file.relative_to(self.build_dir))
                        if origin not in string_origins[string]:
                            string_origins[string].append(origin)
            else:
                self.wrn("No object files found in build directory")

        if not string_origins:
            self.die("No strings found to analyze")

        # Filter strings by minimum length
        filtered_strings = {
            s: origins for s, origins in string_origins.items() if len(s) >= self.min_length
        }
        self.inf(f"Analyzing {len(filtered_strings)} strings (min length: {self.min_length})")

        # Find similar strings
        similar_groups = self.find_similar_strings(list(filtered_strings.keys()))

        # Generate report
        self.generate_report(similar_groups, filtered_strings)

    def find_elf_file(self):
        """Find the main ELF file in the build directory."""
        # Look for zephyr.elf first
        elf_path = self.build_dir / 'zephyr' / 'zephyr.elf'
        if elf_path.exists():
            return elf_path

        # Fall back to searching for any .elf file
        elf_files = list(self.build_dir.glob('**/*.elf'))
        if elf_files:
            return elf_files[0]

        return None

    def find_object_files(self):
        """Find all object files in the build directory."""
        obj_files = list(self.build_dir.glob('**/*.o'))
        return obj_files

    def extract_strings_from_elf(self, elf_path):
        """Extract strings from an ELF file using readelf-like approach."""
        strings = set()

        try:
            with open(elf_path, 'rb') as f:
                elffile = ELFFile(f)

                # Extract from .rodata section
                rodata = elffile.get_section_by_name('.rodata')
                if rodata:
                    strings.update(self.extract_strings_from_data(rodata.data()))

                # Extract from .data section
                data = elffile.get_section_by_name('.data')
                if data:
                    strings.update(self.extract_strings_from_data(data.data()))

                # Extract from .text section (some strings might be there)
                text = elffile.get_section_by_name('.text')
                if text:
                    strings.update(self.extract_strings_from_data(text.data()))

        except Exception as e:
            self.wrn(f"Error extracting strings from {elf_path}: {e}")

        return strings

    def extract_strings_from_object(self, obj_path):
        """Extract strings from an object file using strings command."""
        strings = set()

        try:
            # Use the 'strings' command to extract printable strings
            result = subprocess.run(
                ['strings', '-a', '-n', str(self.min_length), str(obj_path)],
                capture_output=True,
                text=True,
                timeout=30,
            )

            if result.returncode == 0:
                for line in result.stdout.splitlines():
                    line = line.strip()
                    if line:
                        strings.add(line)

        except subprocess.TimeoutExpired:
            self.wrn(f"Timeout extracting strings from {obj_path}")
        except FileNotFoundError:
            self.wrn("'strings' command not found, skipping object file analysis")
        except Exception as e:
            self.wrn(f"Error extracting strings from {obj_path}: {e}")

        return strings

    def extract_strings_from_data(self, data):
        """Extract printable ASCII strings from binary data."""
        strings = set()

        # Find sequences of printable ASCII characters
        pattern = rb'[ -~]{' + str(self.min_length).encode() + rb',}'
        matches = re.findall(pattern, data)

        for match in matches:
            try:
                string = match.decode('ascii')
                strings.add(string)
            except UnicodeDecodeError:
                pass

        return strings

    def find_similar_strings(self, strings):
        """
        Find groups of similar strings.

        Returns a list of tuples (similarity, string1, string2).
        """
        similar_pairs = []
        strings_list = sorted(strings)

        # Compare each pair of strings
        for i in range(len(strings_list)):
            for j in range(i + 1, len(strings_list)):
                s1 = strings_list[i]
                s2 = strings_list[j]

                similarity = similarity_ratio(s1, s2)
                if similarity >= self.threshold and similarity < 1.0:
                    similar_pairs.append((similarity, s1, s2))

        # Sort by similarity (highest first)
        similar_pairs.sort(reverse=True, key=lambda x: x[0])

        return similar_pairs

    def generate_report(self, similar_groups, string_origins):
        """Generate and output the optimization report."""
        if self.format == 'json':
            self.generate_json_report(similar_groups, string_origins)
        else:
            self.generate_text_report(similar_groups, string_origins)

    def generate_text_report(self, similar_groups, string_origins):
        """Generate a text report of findings."""
        output_lines = []

        output_lines.append("=" * 80)
        output_lines.append("String Optimization Analysis Report")
        output_lines.append("=" * 80)
        output_lines.append("")
        output_lines.append(f"Total strings analyzed: {len(string_origins)}")
        output_lines.append(f"Similarity threshold: {self.threshold}")
        output_lines.append(f"Minimum string length: {self.min_length}")
        output_lines.append("")

        if not similar_groups:
            output_lines.append("No similar strings found above the threshold.")
        else:
            output_lines.append(f"Found {len(similar_groups)} pairs of similar strings:")
            output_lines.append("")

            # Calculate potential savings
            total_potential_savings = 0

            for i, (similarity, s1, s2) in enumerate(similar_groups, 1):
                output_lines.append(f"{i}. Similarity: {similarity:.2%}")
                output_lines.append(f"   String 1: \"{s1}\"")
                # Show origin of string 1
                if s1 in string_origins:
                    origins = string_origins[s1]
                    if len(origins) == 1:
                        output_lines.append(f"   Origin: {origins[0]}")
                    else:
                        output_lines.append(f"   Origins: {', '.join(origins[:3])}")
                        if len(origins) > 3:
                            output_lines.append(f"            ... and {len(origins) - 3} more")

                output_lines.append(f"   String 2: \"{s2}\"")
                # Show origin of string 2
                if s2 in string_origins:
                    origins = string_origins[s2]
                    if len(origins) == 1:
                        output_lines.append(f"   Origin: {origins[0]}")
                    else:
                        output_lines.append(f"   Origins: {', '.join(origins[:3])}")
                        if len(origins) > 3:
                            output_lines.append(f"            ... and {len(origins) - 3} more")

                # Estimate potential savings
                # If we homogenize these strings, we save the length of the shorter one
                savings = min(len(s1), len(s2))
                total_potential_savings += savings
                output_lines.append(f"   Potential savings: ~{savings} bytes")

                # Suggest optimization
                if similarity > 0.9:
                    output_lines.append(
                        "   Suggestion: These strings are very similar. "
                        "Consider using a single string literal."
                    )
                else:
                    output_lines.append(
                        "   Suggestion: Consider refactoring to use a common string."
                    )

                output_lines.append("")

            output_lines.append("=" * 80)
            output_lines.append(f"Total potential savings: ~{total_potential_savings} bytes")
            output_lines.append("=" * 80)

        # Output results
        report = '\n'.join(output_lines)

        if self.output_file:
            with open(self.output_file, 'w') as f:
                f.write(report)
            self.inf(f"Report written to {self.output_file}")
        else:
            print(report)

    def generate_json_report(self, similar_groups, string_origins):
        """Generate a JSON report of findings."""
        import json

        report = {
            'summary': {
                'total_strings': len(string_origins),
                'threshold': self.threshold,
                'min_length': self.min_length,
                'similar_pairs': len(similar_groups),
            },
            'similar_strings': [],
        }

        total_potential_savings = 0

        for similarity, s1, s2 in similar_groups:
            savings = min(len(s1), len(s2))
            total_potential_savings += savings

            pair_info = {
                'similarity': round(similarity, 4),
                'string1': s1,
                'string1_origins': string_origins.get(s1, []),
                'string2': s2,
                'string2_origins': string_origins.get(s2, []),
                'potential_savings_bytes': savings,
            }

            report['similar_strings'].append(pair_info)

        report['summary']['total_potential_savings_bytes'] = total_potential_savings

        json_output = json.dumps(report, indent=2)

        if self.output_file:
            with open(self.output_file, 'w') as f:
                f.write(json_output)
            self.inf(f"Report written to {self.output_file}")
        else:
            print(json_output)
