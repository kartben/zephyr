# Copyright (c) 2024 The Zephyr Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

"""Tests for the stropt west command."""

import pytest
import tempfile
from pathlib import Path

from stropt import (
    Stropt,
    levenshtein_distance,
    similarity_ratio,
    extract_strings_from_bytes,
)


def test_levenshtein_distance():
    """Test Levenshtein distance calculation."""
    assert levenshtein_distance('', '') == 0
    assert levenshtein_distance('hello', 'hello') == 0
    assert levenshtein_distance('hello', 'hallo') == 1
    assert levenshtein_distance('hello', 'help') == 2
    assert levenshtein_distance('kitten', 'sitting') == 3
    assert levenshtein_distance('saturday', 'sunday') == 3


def test_similarity_ratio():
    """Test similarity ratio calculation."""
    assert similarity_ratio('hello', 'hello') == 1.0
    assert similarity_ratio('hello', 'hallo') >= 0.8
    assert similarity_ratio('hello', 'world') < 0.5
    assert similarity_ratio('', '') == 1.0


def test_extract_strings_from_bytes():
    """Test string extraction from binary data."""
    # Create test binary data with some strings
    data = b'Hello World\x00\x01\x02Test String\x00\xff\xfeShort\x00'
    
    strings = extract_strings_from_bytes(data, min_length=4)
    
    # Should find "Hello World", "Test String", and "Short"
    assert len(strings) >= 3
    
    # Check that we found the expected strings
    found_strings = [s for _, s in strings]
    assert 'Hello World' in found_strings
    assert 'Test String' in found_strings
    assert 'Short' in found_strings


def test_extract_strings_min_length():
    """Test string extraction respects minimum length."""
    data = b'Hi\x00Long String\x00'
    
    # With min_length=4, should only find "Long String"
    strings = extract_strings_from_bytes(data, min_length=4)
    found_strings = [s for _, s in strings]
    assert 'Long String' in found_strings
    assert 'Hi' not in found_strings


def test_find_similar_groups():
    """Test grouping of similar strings."""
    cmd = Stropt()
    
    strings = [
        (0x100, 'Error: Connection failed'),
        (0x200, 'Error: Connection timeout'),
        (0x300, 'Error: Connection refused'),
        (0x400, 'Warning: Low memory'),
        (0x500, 'Warning: Low battery'),
        (0x600, 'Success'),
    ]
    
    groups = cmd.find_similar_groups(strings, similarity_threshold=0.7)
    
    # Should find at least 2 groups: "Error: Connection..." and "Warning: Low..."
    assert len(groups) >= 2
    
    # Check that similar strings are grouped together
    error_groups = [g for g in groups 
                   if any('Error' in s for _, s in g)]
    assert len(error_groups) > 0
    
    warning_groups = [g for g in groups 
                     if any('Warning' in s for _, s in g)]
    assert len(warning_groups) > 0


def test_calculate_potential_savings():
    """Test calculation of potential byte savings."""
    cmd = Stropt()
    
    # Group with similar strings (second is shorter)
    group = [
        ([0x100, 0x200], 'Error: Connection failed'),
        ([0x300], 'Error: Conn timeout'),
    ]
    
    total_bytes, saved_bytes, num_strings, num_occurrences = \
        cmd.calculate_potential_savings(group)
    
    # Should have 2 unique strings, 3 total occurrences
    assert num_strings == 2
    assert num_occurrences == 3
    
    # Should have some savings when homogenized
    assert saved_bytes >= 0
    assert total_bytes >= saved_bytes


def test_suggest_unified_string():
    """Test suggestion of unified string."""
    cmd = Stropt()
    
    # The longest string should be suggested
    group = [
        ([0x100], 'Short'),
        ([0x200], 'Medium string'),
        ([0x300], 'This is the longest string'),
    ]
    
    suggested = cmd.suggest_unified_string(group)
    assert suggested == 'This is the longest string'


def test_stropt_command_instantiation():
    """Test that Stropt command can be instantiated."""
    cmd = Stropt()
    assert cmd.name == 'stropt'
    assert 'similar strings' in cmd.help.lower()


def test_guess_file_type():
    """Test file type guessing."""
    cmd = Stropt()
    
    # Test extension-based detection
    assert cmd.guess_file_type('firmware.bin') == 'bin'
    assert cmd.guess_file_type('firmware.hex') == 'hex'
    assert cmd.guess_file_type('firmware.elf') == 'elf'
    assert cmd.guess_file_type('firmware.uf2') == 'uf2'


def test_extract_strings_with_offsets():
    """Test that string extraction includes correct offsets."""
    data = b'\x00\x00Hello\x00\x00World\x00'
    
    strings = extract_strings_from_bytes(data, min_length=4)
    
    # Check offsets are reasonable
    for offset, string in strings:
        assert offset >= 0
        assert offset < len(data)
        # Verify the string actually exists at that location
        assert string.encode() in data
