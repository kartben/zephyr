# String Optimization West Command

## Overview

The `string-optimize` west command analyzes string literals in Zephyr firmware to identify similar strings that could be deduplicated or homogenized to reduce firmware size.

## Features

- Uses **Levenshtein distance** algorithm to detect similar strings
- Extracts strings from ELF binaries, object files, or both
- Configurable similarity threshold (0.0-1.0)
- Filters strings by minimum length
- Outputs results in text or JSON format
- Estimates potential byte savings from string homogenization

## Usage

```bash
west string-optimize -d <build-directory> [OPTIONS]
```

### Required Arguments

- `-d, --build-dir DIR` - Path to the build directory containing compiled artifacts

### Optional Arguments

- `-t, --threshold FLOAT` - Similarity threshold (0.0-1.0). Default: 0.7
  - 1.0 = identical strings only
  - 0.0 = all strings compared
  
- `-m, --min-length INT` - Minimum string length to analyze. Default: 10
  - Filters out short strings that are less likely to benefit from optimization
  
- `-f, --format {text,json}` - Output format. Default: text
  - `text`: Human-readable report
  - `json`: Machine-parseable JSON output
  
- `-o, --output FILE` - Output file path. Default: stdout
  
- `--source {elf,objects,both}` - Source of strings. Default: elf
  - `elf`: Extract strings from final ELF binary
  - `objects`: Extract strings from object files (earlier in build process)
  - `both`: Extract from both sources

## Examples

### Basic Usage

Analyze strings in a build directory with default settings:

```bash
west string-optimize -d build/
```

### High Similarity Only

Find only very similar strings (>90% similar):

```bash
west string-optimize -d build/ -t 0.9
```

### Analyze Object Files

Extract strings from object files instead of the final ELF:

```bash
west string-optimize -d build/ --source objects
```

### Save JSON Report

Generate a JSON report for automated processing:

```bash
west string-optimize -d build/ -f json -o string-report.json
```

### Comprehensive Analysis

Analyze all strings from both ELF and object files:

```bash
west string-optimize -d build/ --source both -t 0.7 -m 5 -o report.txt
```

## Sample Output

```
================================================================================
String Optimization Analysis Report
================================================================================

Total strings analyzed: 247
Similarity threshold: 0.7
Minimum string length: 10

Found 12 pairs of similar strings:

1. Similarity: 92.31%
   String 1: "Failed to initialize I2C device"
   String 2: "Failed to initialize SPI device"
   Potential savings: ~32 bytes
   Suggestion: These strings are very similar. Consider using a single string literal.

2. Similarity: 82.14%
   String 1: "Error: Cannot open file /dev/tty"
   String 2: "Error: Cannot open file /dev/null"
   Potential savings: ~31 bytes
   Suggestion: These strings are very similar. Consider using a single string literal.

...

================================================================================
Total potential savings: ~340 bytes
================================================================================
```

## How It Works

1. **String Extraction**: The tool extracts printable ASCII strings from:
   - `.rodata`, `.data`, and `.text` sections of ELF files
   - Object files using the `strings` command (if available)

2. **Similarity Analysis**: For each pair of strings:
   - Calculates Levenshtein distance (edit distance)
   - Converts to similarity ratio (1.0 = identical, 0.0 = completely different)
   - Filters pairs below the similarity threshold

3. **Optimization Suggestions**: For each similar pair:
   - Estimates potential byte savings
   - Suggests refactoring approaches based on similarity level

## Interpreting Results

- **High similarity (>90%)**: Strings are nearly identical, likely typos or slight variations. Strong candidates for consolidation.

- **Medium similarity (70-90%)**: Strings follow similar patterns but have significant differences. May benefit from refactoring to use a common format string.

- **Low similarity (<70%)**: Strings share some common substrings but are mostly different. May not be worth optimizing unless very common.

## Limitations

- Only detects similar strings, does not automatically refactor code
- String extraction from minimal ELF files may be incomplete
- Does not account for const string deduplication by the compiler/linker
- Savings estimates are approximate and don't account for alignment

## Tips

1. Start with high threshold (0.9) to find obvious duplicates
2. Lower threshold gradually to find more candidates
3. Use `--source objects` to analyze strings before link-time optimization
4. Compare results with linker map file to see actual string locations
5. Focus on error messages and log strings, which tend to be similar

## Contributing

To improve string extraction or add new features:

- Main implementation: `scripts/west_commands/string_optimize.py`
- Tests: `scripts/tests/test_string_optimize.py`
- Registration: `scripts/west-commands.yml`
