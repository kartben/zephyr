#!/usr/bin/env python3
#
# Copyright 2025 Analog Devices
#
# SPDX-License-Identifier: Apache-2.0
"""
Pure Python CTF (Common Trace Format) parser.

This module provides a portable alternative to the babeltrace2 (bt2) library
for parsing CTF trace data. It supports the subset of CTF 1.8 used by Zephyr's
instrumentation subsystem.
"""

import os
import re
import struct
from typing import Dict, List, Any, Optional, Tuple


class CTFMetadataParser:
    """Parse CTF metadata (TSDL) files."""

    def __init__(self, metadata_file: str):
        """Initialize the metadata parser.

        Args:
            metadata_file: Path to the metadata file
        """
        self.metadata_file = metadata_file
        self.events = {}
        self.byte_order = 'le'  # Default to little endian
        self._parse_metadata()

    def _parse_metadata(self):
        """Parse the metadata file and extract event definitions."""
        with open(self.metadata_file, 'r') as f:
            content = f.read()

        # Extract byte order
        byte_order_match = re.search(r'byte_order\s*=\s*(le|be)', content)
        if byte_order_match:
            self.byte_order = byte_order_match.group(1)

        # Extract event definitions
        # Match event blocks: event { ... }; with proper brace matching
        # Use a custom function to find matching braces
        idx = 0
        while True:
            event_start = content.find('event', idx)
            if event_start == -1:
                break
            
            # Find the opening brace
            brace_start = content.find('{', event_start)
            if brace_start == -1:
                break
            
            # Find the matching closing brace
            brace_count = 1
            pos = brace_start + 1
            while pos < len(content) and brace_count > 0:
                if content[pos] == '{':
                    brace_count += 1
                elif content[pos] == '}':
                    brace_count -= 1
                pos += 1
            
            if brace_count == 0:
                # Check if followed by ;
                if pos < len(content) and content[pos:pos+1].strip().startswith(';'):
                    event_def = content[brace_start+1:pos-1]
                    event_info = self._parse_event_definition(event_def)
                    if event_info:
                        self.events[event_info['id']] = event_info
            
            idx = pos

    def _parse_event_definition(self, event_def: str) -> Optional[Dict[str, Any]]:
        """Parse a single event definition.

        Args:
            event_def: Event definition string from metadata

        Returns:
            Dictionary containing event information or None
        """
        # Extract event name
        name_match = re.search(r'name\s*=\s*([^;]+);', event_def)
        if not name_match:
            return None
        name = name_match.group(1).strip()

        # Extract event ID
        id_match = re.search(r'id\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)', event_def)
        if not id_match:
            return None
        event_id = int(id_match.group(1), 0)

        # Extract fields
        fields = []
        fields_match = re.search(r'fields\s*:=\s*struct\s*\{([^}]+)\}', event_def, re.DOTALL)
        if fields_match:
            fields_str = fields_match.group(1)
            # Parse field definitions: type name[size];
            field_pattern = r'(\w+)\s+(\w+)(?:\[(\d+)\])?;'
            for field_match in re.finditer(field_pattern, fields_str):
                field_type = field_match.group(1)
                field_name = field_match.group(2)
                field_size = int(field_match.group(3)) if field_match.group(3) else None
                
                # Extract bit size for bit fields
                bit_size = None
                if field_type.startswith('uint') and field_type.endswith('_t'):
                    size_str = field_type[4:-2]  # Extract number from uintX_t
                    if size_str.isdigit():
                        bit_size = int(size_str)
                
                fields.append({
                    'name': field_name,
                    'type': field_type,
                    'array_size': field_size,
                    'bit_size': bit_size
                })

        return {
            'id': event_id,
            'name': name,
            'fields': fields
        }


class CTFDataReader:
    """Read CTF binary data files."""

    def __init__(self, data_file: str, metadata_parser: CTFMetadataParser):
        """Initialize the data reader.

        Args:
            data_file: Path to the binary data file
            metadata_parser: Parsed metadata
        """
        self.data_file = data_file
        self.metadata = metadata_parser
        self.byte_order = '<' if metadata_parser.byte_order == 'le' else '>'
        self.bit_buffer = 0
        self.bit_buffer_size = 0

    def _read_bitfield(self, data: bytes, offset: int, bit_size: int) -> Tuple[int, int]:
        """Read a bit field from the data.

        Args:
            data: Binary data buffer
            offset: Current byte offset
            bit_size: Number of bits to read

        Returns:
            Tuple of (value, new_offset)
        """
        # If we don't have enough bits in buffer, read next byte
        while self.bit_buffer_size < bit_size:
            if offset >= len(data):
                return 0, offset
            byte_val = struct.unpack('B', data[offset:offset + 1])[0]
            self.bit_buffer = (self.bit_buffer << 8) | byte_val
            self.bit_buffer_size += 8
            offset += 1

        # Extract the requested bits
        shift = self.bit_buffer_size - bit_size
        value = (self.bit_buffer >> shift) & ((1 << bit_size) - 1)
        self.bit_buffer_size -= bit_size
        self.bit_buffer &= (1 << self.bit_buffer_size) - 1

        return value, offset

    def _read_field(self, data: bytes, offset: int, field: Dict[str, Any]) -> Tuple[Any, int]:
        """Read a single field from binary data.

        Args:
            data: Binary data buffer
            offset: Current offset in buffer
            field: Field definition

        Returns:
            Tuple of (field_value, new_offset)
        """
        field_type = field['type']
        field_name = field['name']
        array_size = field.get('array_size')
        bit_size = field.get('bit_size')

        # Type to struct format mapping
        type_map = {
            'uint8_t': ('B', 8),
            'int8_t': ('b', 8),
            'uint16_t': ('H', 16),
            'int16_t': ('h', 16),
            'uint32_t': ('I', 32),
            'int32_t': ('i', 32),
            'uint64_t': ('Q', 64),
            'int64_t': ('q', 64),
            'string_t': ('s', None),
            'ctf_bounded_string_t': ('s', None),
        }

        # Handle bit fields (uint1_t through uint7_t for sub-byte fields)
        if bit_size and bit_size < 8:
            value, offset = self._read_bitfield(data, offset, bit_size)
            return value, offset

        # Handle strings
        if field_type in ['string_t', 'ctf_bounded_string_t'] and array_size:
            # Reset bit buffer when reading aligned fields
            self.bit_buffer = 0
            self.bit_buffer_size = 0
            
            string_data = data[offset:offset + array_size]
            # Find null terminator
            null_idx = string_data.find(b'\x00')
            if null_idx != -1:
                value = string_data[:null_idx].decode('utf-8', errors='ignore')
            else:
                value = string_data.decode('utf-8', errors='ignore')
            return value, offset + array_size

        # Handle regular numeric types
        if field_type in type_map and type_map[field_type][1]:
            # Reset bit buffer when reading aligned fields
            self.bit_buffer = 0
            self.bit_buffer_size = 0
            
            fmt, bits = type_map[field_type]
            size = bits // 8
            if offset + size > len(data):
                return None, offset
            value = struct.unpack(self.byte_order + fmt, data[offset:offset + size])[0]
            return value, offset + size

        # Unknown type, skip
        return None, offset

    def read_events(self) -> List[Dict[str, Any]]:
        """Read all events from the data file.

        Returns:
            List of event dictionaries
        """
        events = []

        with open(self.data_file, 'rb') as f:
            data = f.read()

        offset = 0
        while offset < len(data):
            # Reset bit buffer for each event
            self.bit_buffer = 0
            self.bit_buffer_size = 0
            
            # Read event header
            # For instrumentation CTF: event_header has id (uint8_t) at start
            # Check if we have enough data
            if offset + 1 > len(data):
                break

            # Read event ID
            event_id = struct.unpack('B', data[offset:offset + 1])[0]
            offset += 1

            # Check if this is a known event
            if event_id not in self.metadata.events:
                # Unknown event, skip or break
                break

            event_def = self.metadata.events[event_id]

            # Read event fields
            event_data = {
                'id': event_id,
                'name': event_def['name'],
                'fields': {}
            }

            for field in event_def['fields']:
                value, offset = self._read_field(data, offset, field)
                event_data['fields'][field['name']] = value

            events.append(event_data)

        return events


class CTFTraceParser:
    """High-level CTF trace parser."""

    def __init__(self, trace_dir: str):
        """Initialize the trace parser.

        Args:
            trace_dir: Directory containing metadata and data files
        """
        self.trace_dir = trace_dir
        metadata_file = os.path.join(trace_dir, 'metadata')
        data_file = os.path.join(trace_dir, 'data')

        if not os.path.exists(metadata_file):
            raise FileNotFoundError(f"Metadata file not found: {metadata_file}")
        if not os.path.exists(data_file):
            raise FileNotFoundError(f"Data file not found: {data_file}")

        self.metadata_parser = CTFMetadataParser(metadata_file)
        self.data_reader = CTFDataReader(data_file, self.metadata_parser)

    def parse(self) -> List[Dict[str, Any]]:
        """Parse the trace and return events.

        Returns:
            List of event dictionaries
        """
        return self.data_reader.read_events()


# Babeltrace2 API compatibility layer
class _EventMessageConst:
    """Mimics bt2._EventMessageConst for compatibility."""
    
    def __init__(self, event_data: Dict[str, Any]):
        self.event = _Event(event_data)


class _Event:
    """Mimics bt2 Event for compatibility."""
    
    def __init__(self, event_data: Dict[str, Any]):
        self._data = event_data
        self.name = event_data['name']
        self.id = event_data['id']
        self.payload_field = _PayloadField(event_data['fields'])


class _PayloadField:
    """Mimics bt2 payload field for compatibility."""
    
    def __init__(self, fields: Dict[str, Any]):
        self._fields = fields
    
    def get(self, field_name: str):
        """Get a field value."""
        value = self._fields.get(field_name)
        if value is None:
            return _FieldValue(None)
        return _FieldValue(value)


class _FieldValue:
    """Mimics bt2 field value for compatibility."""
    
    def __init__(self, value: Any):
        self._value = value
        self.real = value if isinstance(value, (int, float)) else 0


class TraceCollectionMessageIterator:
    """Mimics bt2.TraceCollectionMessageIterator for compatibility with existing code."""
    
    def __init__(self, trace_dir: str):
        """Initialize iterator with trace directory.
        
        Args:
            trace_dir: Directory containing CTF metadata and data files
        """
        self.parser = CTFTraceParser(trace_dir)
        self.events = self.parser.parse()
        self.index = 0
    
    def __iter__(self):
        """Return iterator."""
        return self
    
    def __next__(self):
        """Get next message."""
        if self.index >= len(self.events):
            raise StopIteration
        
        event_data = self.events[self.index]
        self.index += 1
        
        # Wrap in compatibility layer
        return _EventMessageConst(event_data)
