# Portable CTF Parser

## Overview

The `ctf_parser.py` module provides a pure Python implementation of a CTF (Common Trace Format) parser, eliminating the need for the `bt2` (babeltrace2) Python bindings. This makes the tracing and instrumentation tools portable across all platforms, including Mac and Windows where installing bt2 can be difficult.

## Features

- **Pure Python**: No external dependencies or native code compilation required
- **Cross-platform**: Works on Linux, Mac, and Windows
- **CTF 1.8 Support**: Parses CTF metadata (TSDL) and binary trace data
- **Babeltrace2 API Compatible**: Provides drop-in replacement classes for seamless migration
- **Bit Field Support**: Handles sub-byte fields (uint1_t through uint7_t)
- **Multiple Formats**: Supports both instrumentation and tracing CTF formats

## Usage

### Direct Usage

```python
from ctf_parser import TraceCollectionMessageIterator, _EventMessageConst

# Parse a CTF trace directory (containing metadata and data files)
msg_it = TraceCollectionMessageIterator('/path/to/trace')

for msg in msg_it:
    if isinstance(msg, _EventMessageConst):
        event = msg.event
        print(f"Event: {event.name}")
        
        # Access event fields
        field_value = event.payload_field.get('field_name')
        print(f"Field value: {field_value.real}")
```

### Migration from bt2

The parser provides a compatibility layer that mimics the bt2 API. To migrate existing code:

**Before (with bt2):**
```python
import bt2

msg_it = bt2.TraceCollectionMessageIterator(tmpdir)
for msg in msg_it:
    if isinstance(msg, bt2._EventMessageConst):
        event = msg.event
        # ...
```

**After (with ctf_parser):**
```python
from ctf_parser import TraceCollectionMessageIterator, _EventMessageConst

msg_it = TraceCollectionMessageIterator(tmpdir)
for msg in msg_it:
    if isinstance(msg, _EventMessageConst):
        event = msg.event
        # ... rest of the code remains the same
```

## Supported CTF Features

### Data Types
- Integer types: uint8_t, uint16_t, uint32_t, uint64_t (and signed variants)
- Bit fields: uint1_t through uint7_t
- Strings: string_t, ctf_bounded_string_t
- Arrays: Fixed-size arrays (e.g., `string_t name[20]`)

### Event Structure
- Event headers with timestamp (tracing format)
- Event headers with ID only (instrumentation format)
- Custom event fields
- Nested structures (limited support)

## Limitations

- No support for complex nested structures beyond one level
- Timestamp conversion to ns_from_origin is approximate
- Limited support for advanced CTF features (variants, sequences, etc.)

These limitations are acceptable for Zephyr's use case, which uses a simplified subset of CTF.

## Files Modified

1. **scripts/instrumentation/zaru.py**: Updated to use ctf_parser instead of bt2
2. **scripts/tracing/parse_ctf.py**: Updated to use ctf_parser as fallback if bt2 is not available
3. **scripts/instrumentation/ctf_parser.py**: New portable parser implementation

## Testing

Run the unit tests:
```bash
cd scripts/instrumentation
python3 -m pytest test_ctf_parser.py  # (if tests are added)
```

Or test manually with a trace:
```bash
# Assuming you have a CTF trace in ./ctf/ directory
python3 -c "from ctf_parser import TraceCollectionMessageIterator; \
            it = TraceCollectionMessageIterator('./ctf'); \
            print(f'Found {len(list(it.events))} events')"
```

## Benefits Over bt2

1. **No installation required**: Works immediately without apt-get or system packages
2. **Portable**: Same code works on all platforms
3. **Lightweight**: Pure Python, no native dependencies
4. **Easy to debug**: Python source code is readable and modifiable
5. **Fast enough**: Performance is adequate for typical Zephyr trace sizes

## Future Improvements

Potential enhancements that could be added:
- Support for CTF variants and sequences
- More accurate timestamp conversions
- Streaming mode for very large traces
- Better error messages for malformed CTF data
- Validation mode to check CTF compliance
