# Pure Python CTF Parser

## Overview

This directory contains `ctf_parser.py`, a pure Python implementation of a CTF (Common Trace Format) parser that does not require the babeltrace2 (bt2) Python bindings. This makes it easier to use Zephyr's tracing and instrumentation tools on macOS, Windows, and Linux systems without needing to install additional dependencies.

## Purpose

The primary purpose of this parser is to provide a fallback for `zaru.py` and `parse_ctf.py` when the `bt2` module is not available. It enables these tools to work across different operating systems without platform-specific dependencies.

## Features

- **Pure Python**: No compiled dependencies required
- **Cross-platform**: Works on Linux, macOS, and Windows
- **CTF 1.8 Support**: Parses CTF metadata (TSDL format) and binary trace data
- **Bit Field Support**: Handles packed bit fields used in Zephyr's instrumentation format
- **Dynamic Headers**: Supports different event header formats
- **Enum Types**: Parses enum type declarations in metadata
- **Compatible API**: Provides a bt2-compatible interface for easy integration

## Supported CTF Features

The parser supports the following CTF features commonly used in Zephyr:

- Little-endian and big-endian byte order
- Event headers with timestamp and/or event ID
- Standard integer types (uint8_t, uint16_t, uint32_t, uint64_t, etc.)
- Packed bit fields (uint1_t, uint2_t, uint3_t, uint5_t, etc.)
- Fixed-size string fields (ctf_bounded_string_t, string_t)
- Array fields
- Enum types for event IDs

## Usage

The parser is automatically used as a fallback in `zaru.py` and `parse_ctf.py` when bt2 is not available:

```python
# This code tries bt2 first, falls back to ctf_parser
try:
    import bt2
    USE_BT2 = True
except ImportError:
    USE_BT2 = False
    from ctf_parser import CTFTraceIterator, _EventMessageConst
    # Create bt2-compatible namespace
    class bt2:
        TraceCollectionMessageIterator = CTFTraceIterator
        _EventMessageConst = _EventMessageConst
```

Direct usage:

```python
from ctf_parser import CTFTraceIterator

# Parse CTF trace from directory containing 'metadata' and 'data' files
iterator = CTFTraceIterator('/path/to/trace_directory')

# Iterate through events
for message in iterator:
    event = message.event
    print(f"Event: {event.name}")
    print(f"  ID: {event.id}")
    
    # Access payload fields
    for field_name in ['thread_id', 'callee', 'timestamp']:
        field = event.payload_field.get(field_name)
        if field:
            print(f"  {field_name}: {field.real}")
```

## Limitations

While the parser supports most CTF features used by Zephyr, it has some limitations compared to babeltrace2:

1. **Performance**: The pure Python implementation is slower than the C-based bt2 library
2. **CTF Scope**: Only supports CTF features actually used by Zephyr (CTF 1.8 subset)
3. **Complex Types**: Does not support complex nested structures or unions beyond what Zephyr uses
4. **Validation**: Less strict validation than bt2 - may accept malformed traces

## Testing

A simple test script is available at `/tmp/test_ctf_parser.py` that validates:
- Metadata parsing (instrumentation and tracing formats)
- Binary stream parsing
- Event field extraction

## Compatibility

The parser provides a bt2-compatible API through wrapper classes:
- `CTFTraceIterator` → `bt2.TraceCollectionMessageIterator`
- `EventMessage` → `bt2._EventMessageConst`
- `Event` → Provides `name`, `id`, and `payload_field` attributes
- `PayloadField` → Provides `get(field_name)` method
- `FieldValue` → Provides `real` property for numeric values

This allows existing code using bt2 to work with minimal or no modifications.

## Files

- `ctf_parser.py`: Main parser implementation
- `README_CTF_PARSER.md`: This documentation file

## Future Improvements

Potential areas for enhancement:
- Support for more complex CTF features if needed
- Performance optimizations (e.g., caching, lazy parsing)
- Better error messages and validation
- Support for CTF 2.0 if Zephyr adopts it

## References

- [CTF Specification](https://diamon.org/ctf/)
- [Babeltrace 2](https://babeltrace.org/)
- [Zephyr Tracing Documentation](https://docs.zephyrproject.org/latest/services/tracing/index.html)
