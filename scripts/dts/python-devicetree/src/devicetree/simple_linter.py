#!/usr/bin/env python3

import re
from typing import List, Optional
from pathlib import Path


class SimpleDevicetreeLinter:
    """
    A simple devicetree linter that checks for basic style issues.
    """

    def __init__(self, filename: str):
        self.filename = filename
        self.errors: List[str] = []

    def lint(self) -> List[str]:
        """
        Run all linting checks on the devicetree file.
        Returns a list of error messages.
        """
        try:
            with open(self.filename, 'r') as f:
                content = f.read()
                lines = content.split('\n')

                self._check_indentation(lines)
                self._check_node_spacing(lines)
                self._check_property_formatting(lines)
                self._check_node_names(lines)
                self._check_label_names(lines)

        except IOError as e:
            self.errors.append(f"Failed to read file: {e}")

        return self.errors

    def _check_indentation(self, lines: List[str]) -> None:
        """
        Check that indentation is done with tabs, not spaces.
        """
        for i, line in enumerate(lines, 1):
            if line.startswith(' '):
                self.errors.append(f"Line {i}: Uses spaces for indentation instead of tabs")

    def _check_node_spacing(self, lines: List[str]) -> None:
        """
        Check for proper spacing between nodes and properties.
        """
        prev_line = None
        for i, line in enumerate(lines, 1):
            if prev_line is not None:
                # Check for missing blank line between nodes
                if (
                    line.strip().startswith('{')
                    and prev_line.strip()
                    and not prev_line.strip().startswith('{')
                    and not prev_line.strip().startswith('}')
                ):
                    self.errors.append(f"Line {i}: Missing blank line before node")

                # Check for missing blank line between properties and nodes
                if (
                    line.strip().startswith('{')
                    and '=' in prev_line
                    and not prev_line.strip().startswith('}')
                ):
                    self.errors.append(f"Line {i}: Missing blank line between property and node")

            prev_line = line

    def _check_property_formatting(self, lines: List[str]) -> None:
        """
        Check property formatting rules:
        1. Single space around equals sign
        2. Property names use dashes as separators
        """
        for i, line in enumerate(lines, 1):
            if '=' in line:
                # Check spacing around equals sign
                if '  =' in line or '=  ' in line or ' =' not in line:
                    self.errors.append(
                        f"Line {i}: Property should have exactly one space around equals sign"
                    )

                # Check property name format
                prop_name = line.split('=')[0].strip()
                if '_' in prop_name and '-' not in prop_name:
                    self.errors.append(
                        f"Line {i}: Property name '{prop_name}' should use dashes (-) as word separators"
                    )

    def _check_node_names(self, lines: List[str]) -> None:
        """
        Check that node names use dashes as separators.
        """
        for i, line in enumerate(lines, 1):
            if line.strip().startswith('{'):
                # Get the node name from the previous line
                if i > 1:
                    node_name = lines[i - 2].strip()
                    if node_name and '_' in node_name and '-' not in node_name:
                        self.errors.append(
                            f"Line {i-1}: Node name '{node_name}' should use dashes (-) as word separators"
                        )

    def _check_label_names(self, lines: List[str]) -> None:
        """
        Check that labels use underscores as separators.
        """
        for i, line in enumerate(lines, 1):
            if ':' in line and not line.strip().startswith('#'):
                label = line.split(':')[0].strip()
                if '-' in label:
                    self.errors.append(
                        f"Line {i}: Label '{label}' should use underscores (_) as word separators"
                    )


def main():
    import sys

    if len(sys.argv) != 2:
        print("Usage: simple_linter.py <devicetree_file>")
        sys.exit(1)

    linter = SimpleDevicetreeLinter(sys.argv[1])
    errors = linter.lint()

    if errors:
        print(f"\nFound {len(errors)} style issues in {sys.argv[1]}:")
        for error in errors:
            print(f"  {error}")
        sys.exit(1)
    else:
        print(f"No style issues found in {sys.argv[1]}")
        sys.exit(0)


if __name__ == "__main__":
    main()
