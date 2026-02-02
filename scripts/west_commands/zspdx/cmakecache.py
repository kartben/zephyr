# Copyright (c) 2021 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

"""
CMake Cache File Parser

This module provides functionality to parse CMake cache files (CMakeCache.txt).
CMake cache files store CMake variables with their types and values in the format:
    VARIABLE_NAME:TYPE=VALUE

Where:
- VARIABLE_NAME: The name of the CMake variable
- TYPE: The variable type (STRING, BOOL, FILEPATH, PATH, INTERNAL, etc.)
- VALUE: The variable's value

This parser extracts variable names and values, discarding type information.
"""

from west import log


def parseCMakeCacheFile(filePath):
    """
    Parse a CMake cache file and return a dict of variable names to values.

    Args:
        filePath: Path to the CMakeCache.txt file

    Returns:
        dict: Dictionary mapping variable names to their values (types are discarded)
    """
    log.dbg(f"parsing CMake cache file at {filePath}")
    cache_dict = {}
    try:
        with open(filePath) as f:
            # CMake cache files are typically short, so we read all lines at once
            lines = f.readlines()

            # Parse each line looking for variable definitions
            for line in lines:
                stripped_line = line.strip()
                
                # Skip empty lines
                if stripped_line == "":
                    continue
                
                # Skip comment lines (both // and # style comments)
                if stripped_line.startswith("#") or stripped_line.startswith("//"):
                    continue

                # Parse variable definition: VARIABLE_NAME:TYPE=VALUE
                # First split on ':' to separate name from type:value
                parts_by_colon = stripped_line.split(":", maxsplit=1)
                if len(parts_by_colon) != 2:
                    # Malformed line without ':' separator, skip it
                    continue
                
                # Then split on '=' to separate type from value
                parts_by_equals = parts_by_colon[1].split("=", maxsplit=1)
                if len(parts_by_equals) != 2:
                    # Malformed line without '=' separator, skip it
                    continue
                
                # Store variable name and value (discarding the type)
                variable_name = parts_by_colon[0]
                variable_value = parts_by_equals[1]
                cache_dict[variable_name] = variable_value

            return cache_dict

    except OSError as e:
        log.err(f"Error loading {filePath}: {str(e)}")
        return {}
