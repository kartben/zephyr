#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
Unit tests for string_optimize module
"""

import sys
import os

# Add scripts/west_commands to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'west_commands'))

from string_optimize import levenshtein_distance, similarity_ratio


def test_levenshtein_distance():
    """Test Levenshtein distance calculation."""
    print("Testing Levenshtein distance...")

    # Test identical strings
    assert levenshtein_distance("hello", "hello") == 0, "Identical strings should have distance 0"

    # Test completely different strings
    assert levenshtein_distance("abc", "xyz") == 3, "Different strings should have correct distance"

    # Test single character difference
    assert levenshtein_distance("kitten", "sitten") == 1, "Single substitution"

    # Test empty string
    assert levenshtein_distance("", "hello") == 5, "Empty string distance"
    assert levenshtein_distance("hello", "") == 5, "Empty string distance"

    # Test similar strings
    d = levenshtein_distance("Error: Failed to open file", "Error: Failed to open socket")
    print(f"  Distance between similar strings: {d}")
    assert d > 0, "Similar strings should have non-zero distance"

    print("✓ Levenshtein distance tests passed")


def test_similarity_ratio():
    """Test similarity ratio calculation."""
    print("\nTesting similarity ratio...")

    # Test identical strings
    assert similarity_ratio("hello", "hello") == 1.0, "Identical strings should have similarity 1.0"

    # Test completely different strings
    sim = similarity_ratio("abc", "xyz")
    print(f"  Similarity of different strings: {sim:.2f}")
    assert sim == 0.0, "Completely different strings should have similarity 0.0"

    # Test similar strings
    sim = similarity_ratio("Error: Failed to open file", "Error: Failed to open socket")
    print(f"  Similarity of similar strings: {sim:.2%}")
    assert sim > 0.7, "Similar strings should have high similarity"

    # Test empty strings
    assert similarity_ratio("", "") == 1.0, "Empty strings should be identical"

    print("✓ Similarity ratio tests passed")


def test_real_world_examples():
    """Test with real-world string examples."""
    print("\nTesting real-world examples...")

    examples = [
        ("Failed to initialize device", "Failed to initialize sensor", True),
        ("Error opening file", "Error opening socket", True),
        ("Connection timeout", "Connection established", True),
        ("The quick brown fox", "Hello world", False),
        ("Bluetooth initialized", "Bluetooth initialization failed", True),
        ("Memory allocation failed", "Memory deallocation failed", True),
    ]

    for s1, s2, should_be_similar in examples:
        sim = similarity_ratio(s1, s2)
        print(f"  '{s1}' vs '{s2}': {sim:.2%}")

        if should_be_similar:
            assert sim > 0.5, f"'{s1}' and '{s2}' should be similar"
        else:
            assert sim < 0.6, f"'{s1}' and '{s2}' should not be similar"

    print("✓ Real-world examples passed")


if __name__ == "__main__":
    print("=" * 80)
    print("String Optimization Module Tests")
    print("=" * 80)

    try:
        test_levenshtein_distance()
        test_similarity_ratio()
        test_real_world_examples()

        print("\n" + "=" * 80)
        print("All tests passed! ✓")
        print("=" * 80)

    except AssertionError as e:
        print(f"\n✗ Test failed: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Unexpected error: {e}")
        import traceback

        traceback.print_exc()
        sys.exit(1)
