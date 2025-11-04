#!/usr/bin/env python3
#
# Copyright (c) 2018 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0


"""This script will parse the serial console log file and create the required
gcda files.
"""

import argparse
import os
import re


def retrieve_data(input_file):
    extracted_coverage_info = {}
    capture_data = False
    reached_end = False
    
    with open(input_file, 'r') as fp:
        for line in fp:  # Line-by-line iteration instead of readlines()
            # Use faster string containment check instead of regex
            if "GCOV_COVERAGE_DUMP_START" in line:
                capture_data = True
                continue
            if "GCOV_COVERAGE_DUMP_END" in line:
                reached_end = True
                break
            
            # Early exit if not capturing
            if not capture_data:
                continue

            # Optimize parsing with single find operation
            if line.startswith("*") and "<" in line:
                delimiter_idx = line.find("<")
                if delimiter_idx > 1:  # Must have content before delimiter
                    file_name = line[1:delimiter_idx]  # Remove leading "*"
                    hex_dump = line[delimiter_idx + 1:].rstrip('\n')  # Remove trailing newline
                    
                    if hex_dump:  # Only store non-empty hex data
                        extracted_coverage_info[file_name] = hex_dump

    if not reached_end:
        print("incomplete data captured from %s" % input_file)
    return extracted_coverage_info


def create_gcda_files(extracted_coverage_info):
    if args.verbose:
        print(f"Generating {len(extracted_coverage_info)} gcda files")
    
    for filename, hexdump_val in extracted_coverage_info.items():
        if args.verbose:
            print(filename)
        
        # Handle kobject_hash special case
        if "kobject_hash" in filename:
            gcno_filename = filename[:-4] + "gcno"
            try:
                os.remove(gcno_filename)
            except OSError:
                pass  # File doesn't exist or permission denied
            continue

        try:
            # Write file atomically using temporary file
            temp_filename = f"{filename}.tmp"
            with open(temp_filename, 'wb') as fp:
                fp.write(bytes.fromhex(hexdump_val))
            os.replace(temp_filename, filename)
        except (ValueError, OSError) as e:
            print(f"Error creating gcda file {filename}: {e}")
            # Clean up temp file if it exists
            try:
                os.remove(temp_filename)
            except OSError:
                pass


def parse_args():
    global args
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False)
    parser.add_argument("-i", "--input", required=True,
                        help="Input dump data")
    parser.add_argument("-v", "--verbose", action="count", default=0,
                        help="Verbose Output")
    args = parser.parse_args()


def main():
    parse_args()
    input_file = args.input

    extracted_coverage_info = retrieve_data(input_file)
    create_gcda_files(extracted_coverage_info)


if __name__ == '__main__':
    main()
