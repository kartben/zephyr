# bt2 Dependency Removal from zaru.py

## What Changed?

The zaru.py instrumentation tool no longer requires the bt2 (babeltrace2) Python bindings. The tool now uses a native Python parser for CTF (Common Trace Format) data.

## Why?

The bt2 dependency was:
- Difficult to install on Mac and Windows systems
- Required specific Python3 versions
- Created an unnecessary dependency for parsing a simple binary format

## Benefits

✅ **Easier Installation**: No need to install bt2 bindings  
✅ **Cross-Platform**: Works seamlessly on Mac, Windows, and Linux  
✅ **Simpler Dependencies**: Only requires standard Python libraries  
✅ **Better Compatibility**: Works with any Python 3.x version  

## Migration Guide

### Before (with bt2)

```bash
# Ubuntu/Debian
sudo apt-get install python3-bt2

# Or building from source
# (complex installation process)
```

### After (no bt2 needed!)

```bash
# No additional installation needed!
# Just ensure you have the base dependencies:
pip install pyserial colorama west
```

## Functionality

All functionality remains the same:
- ✅ Trace collection and display
- ✅ Profile information
- ✅ Perfetto JSON export
- ✅ Trigger/stopper configuration

## Technical Details

The new implementation includes a `CTFParser` class that:
- Parses CTF 1.8 binary format
- Handles all 5 event types used by Zephyr instrumentation
- Supports bit-level fields and little-endian byte order
- Uses only Python's standard `struct` module

## Questions?

If you encounter any issues with the new parser, please report them to the Zephyr project.
