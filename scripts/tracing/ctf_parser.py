#!/usr/bin/env python3
#
# Copyright (c) 2025 Contributors
#
# SPDX-License-Identifier: Apache-2.0
"""
Pure Python CTF (Common Trace Format) parser.

This module provides functionality to parse CTF binary trace data without
requiring the babeltrace2 (bt2) Python bindings, making it more portable
across different operating systems including Mac and Windows.
"""

import struct
import re
from typing import Dict, List, Tuple, Any, Optional, IO
from dataclasses import dataclass
from enum import Enum


class ByteOrder(Enum):
    """Byte order for binary data."""
    LITTLE_ENDIAN = 'le'
    BIG_ENDIAN = 'be'


@dataclass
class EventField:
    """Represents a field in a CTF event."""
    name: str
    type_name: str
    size: int  # in bytes
    is_array: bool = False
    array_size: int = 0
    is_string: bool = False
    bit_size: int = 0  # for bit fields (0 means not a bit field)


@dataclass
class EventDefinition:
    """Definition of a CTF event type."""
    name: str
    event_id: int
    fields: List[EventField]


@dataclass
class CTFEvent:
    """A parsed CTF event."""
    name: str
    event_id: int
    timestamp: int
    payload: Dict[str, Any]


class CTFMetadataParser:
    """Parser for CTF metadata files (TSDL format)."""
    
    def __init__(self, metadata_file: str):
        """Initialize the metadata parser.
        
        Args:
            metadata_file: Path to the metadata file
        """
        self.metadata_file = metadata_file
        self.byte_order = ByteOrder.LITTLE_ENDIAN
        self.events: Dict[int, EventDefinition] = {}
        self._parse()
    
    def _parse(self):
        """Parse the metadata file."""
        with open(self.metadata_file, 'r') as f:
            content = f.read()
        
        # Parse byte order from trace declaration
        byte_order_match = re.search(r'byte_order\s*=\s*(\w+)', content)
        if byte_order_match:
            bo = byte_order_match.group(1)
            if bo == 'be':
                self.byte_order = ByteOrder.BIG_ENDIAN
        
        # Parse all event definitions
        event_pattern = r'event\s*{[^}]*name\s*=\s*(\w+);[^}]*id\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+);[^}]*fields\s*:=\s*struct\s*{([^}]*)}[^}]*}'
        
        for match in re.finditer(event_pattern, content, re.MULTILINE | re.DOTALL):
            event_name = match.group(1)
            event_id_str = match.group(2)
            event_id = int(event_id_str, 0)  # Parse hex or decimal
            fields_str = match.group(3)
            
            fields = self._parse_fields(fields_str)
            self.events[event_id] = EventDefinition(
                name=event_name,
                event_id=event_id,
                fields=fields
            )
    
    def _parse_fields(self, fields_str: str) -> List[EventField]:
        """Parse field definitions from a struct.
        
        Args:
            fields_str: String containing field definitions
            
        Returns:
            List of EventField objects
        """
        fields = []
        
        # Match field patterns: type name; or type name[size];
        field_pattern = r'(\w+)\s+(\w+)(?:\[(\d+)\])?\s*;'
        
        for match in re.finditer(field_pattern, fields_str):
            type_name = match.group(1)
            field_name = match.group(2)
            array_size_str = match.group(3)
            
            is_array = array_size_str is not None
            array_size = int(array_size_str) if array_size_str else 0
            
            # Determine field size based on type
            size = self._get_type_size(type_name)
            is_string = type_name in ['ctf_bounded_string_t', 'string_t']
            
            # Extract bit size from type name (e.g., uint3_t -> 3 bits)
            bit_size = 0
            if type_name.startswith('uint') and type_name.endswith('_t'):
                try:
                    bit_size = int(type_name[4:-2])
                except ValueError:
                    bit_size = 0
            
            fields.append(EventField(
                name=field_name,
                type_name=type_name,
                size=size,
                is_array=is_array,
                array_size=array_size,
                is_string=is_string,
                bit_size=bit_size
            ))
        
        return fields
    
    def _get_type_size(self, type_name: str) -> int:
        """Get the size in bytes for a type.
        
        Args:
            type_name: Name of the type
            
        Returns:
            Size in bytes
        """
        type_sizes = {
            'uint8_t': 1,
            'int8_t': 1,
            'uint16_t': 2,
            'int16_t': 2,
            'uint32_t': 4,
            'int32_t': 4,
            'uint64_t': 8,
            'int64_t': 8,
            'ctf_bounded_string_t': 1,  # Size per character
            'string_t': 1,  # Size per character
            # Bit field types - these are packed into bytes
            'uint1_t': 0,  # Bit field - handled specially
            'uint2_t': 0,  # Bit field - handled specially
            'uint3_t': 0,  # Bit field - handled specially
            'uint5_t': 0,  # Bit field - handled specially
        }
        return type_sizes.get(type_name, 4)


class CTFStreamParser:
    """Parser for CTF binary stream data."""
    
    def __init__(self, metadata_parser: CTFMetadataParser):
        """Initialize the stream parser.
        
        Args:
            metadata_parser: Parsed metadata
        """
        self.metadata = metadata_parser
        self.endian = '<' if metadata_parser.byte_order == ByteOrder.LITTLE_ENDIAN else '>'
    
    def parse_stream(self, stream_data: bytes) -> List[CTFEvent]:
        """Parse a binary CTF stream.
        
        Args:
            stream_data: Binary data to parse
            
        Returns:
            List of parsed events
        """
        events = []
        offset = 0
        
        while offset < len(stream_data):
            # Parse event header (just event ID)
            if offset + 1 > len(stream_data):
                break
            
            event_id = struct.unpack_from('B', stream_data, offset)[0]
            offset += 1
            
            # Get event definition
            event_def = self.metadata.events.get(event_id)
            if not event_def:
                # Unknown event, skip - but we can't know how many bytes to skip
                # so we have to stop parsing
                break
            
            # Parse event payload
            payload = {}
            timestamp = 0
            bit_buffer = 0
            bit_buffer_size = 0
            
            try:
                for field in event_def.fields:
                    if field.bit_size > 0:
                        # Handle bit field
                        if bit_buffer_size < field.bit_size:
                            # Need to read more bytes
                            if offset >= len(stream_data):
                                raise struct.error("Not enough data")
                            byte_val = struct.unpack_from('B', stream_data, offset)[0]
                            offset += 1
                            bit_buffer |= (byte_val << bit_buffer_size)
                            bit_buffer_size += 8
                        
                        # Extract the bits for this field
                        mask = (1 << field.bit_size) - 1
                        value = bit_buffer & mask
                        bit_buffer >>= field.bit_size
                        bit_buffer_size -= field.bit_size
                    else:
                        # Regular field
                        # Flush any remaining bits
                        if bit_buffer_size > 0:
                            bit_buffer = 0
                            bit_buffer_size = 0
                        
                        value, bytes_read = self._parse_field(stream_data, offset, field)
                        offset += bytes_read
                    
                    payload[field.name] = value
                    
                    # Extract timestamp if present
                    if field.name == 'timestamp':
                        timestamp = value
                        
            except struct.error:
                # Not enough data, stop parsing
                break
            
            events.append(CTFEvent(
                name=event_def.name,
                event_id=event_id,
                timestamp=timestamp,
                payload=payload
            ))
        
        return events
    
    def _parse_field(self, data: bytes, offset: int, field: EventField) -> Tuple[Any, int]:
        """Parse a single field from the binary data.
        
        Args:
            data: Binary data
            offset: Current offset in data
            field: Field definition
            
        Returns:
            Tuple of (parsed value, bytes consumed)
        """
        if field.is_array:
            if field.is_string:
                # String field (array of characters)
                size = field.array_size
                raw_bytes = data[offset:offset + size]
                # Find null terminator and decode
                null_pos = raw_bytes.find(b'\x00')
                if null_pos >= 0:
                    value = raw_bytes[:null_pos].decode('utf-8', errors='replace')
                else:
                    value = raw_bytes.decode('utf-8', errors='replace')
                return value, size
            else:
                # Array of values
                values = []
                bytes_read = 0
                for _ in range(field.array_size):
                    val, size = self._parse_single_value(data, offset + bytes_read, field)
                    values.append(val)
                    bytes_read += size
                return values, bytes_read
        else:
            return self._parse_single_value(data, offset, field)
    
    def _parse_single_value(self, data: bytes, offset: int, field: EventField) -> Tuple[Any, int]:
        """Parse a single value from binary data.
        
        Args:
            data: Binary data
            offset: Current offset
            field: Field definition
            
        Returns:
            Tuple of (value, bytes consumed)
        """
        type_formats = {
            'uint8_t': ('B', 1),
            'int8_t': ('b', 1),
            'uint16_t': ('H', 2),
            'int16_t': ('h', 2),
            'uint32_t': ('I', 4),
            'int32_t': ('i', 4),
            'uint64_t': ('Q', 8),
            'int64_t': ('q', 8),
        }
        
        fmt_char, size = type_formats.get(field.type_name, ('I', 4))
        fmt = f'{self.endian}{fmt_char}'
        value = struct.unpack_from(fmt, data, offset)[0]
        return value, size


# For compatibility, provide a class that can be used with isinstance
class _EventMessageConst:
    """Placeholder class for bt2._EventMessageConst compatibility."""
    pass


class CTFTraceIterator:
    """Iterator for CTF trace messages - compatible interface with bt2."""
    
    def __init__(self, trace_dir: str):
        """Initialize the trace iterator.
        
        Args:
            trace_dir: Directory containing 'metadata' and 'data' files
        """
        import os
        
        metadata_file = os.path.join(trace_dir, 'metadata')
        data_file = os.path.join(trace_dir, 'data')
        
        # Parse metadata
        self.metadata_parser = CTFMetadataParser(metadata_file)
        
        # Read and parse data
        with open(data_file, 'rb') as f:
            stream_data = f.read()
        
        self.stream_parser = CTFStreamParser(self.metadata_parser)
        self.events = self.stream_parser.parse_stream(stream_data)
        self.index = 0
    
    def __iter__(self):
        """Return iterator."""
        return self
    
    def __next__(self):
        """Get next message."""
        if self.index >= len(self.events):
            raise StopIteration
        
        event = self.events[self.index]
        self.index += 1
        
        # Return a message-like object compatible with bt2 API
        return EventMessage(event)


class EventMessage(_EventMessageConst):
    """Wrapper for CTF events to provide bt2-compatible interface."""
    
    def __init__(self, ctf_event: CTFEvent):
        """Initialize message.
        
        Args:
            ctf_event: Parsed CTF event
        """
        self.event = Event(ctf_event)


class Event:
    """Wrapper for CTF event to provide bt2-compatible interface."""
    
    def __init__(self, ctf_event: CTFEvent):
        """Initialize event.
        
        Args:
            ctf_event: Parsed CTF event
        """
        self.name = ctf_event.name
        self.id = ctf_event.event_id
        self.payload_field = PayloadField(ctf_event.payload)


class PayloadField:
    """Wrapper for event payload to provide bt2-compatible interface."""
    
    def __init__(self, payload: Dict[str, Any]):
        """Initialize payload field.
        
        Args:
            payload: Dictionary of field values
        """
        self.payload = payload
    
    def get(self, field_name: str) -> Optional['FieldValue']:
        """Get a field value.
        
        Args:
            field_name: Name of the field
            
        Returns:
            Field value wrapper or None
        """
        value = self.payload.get(field_name)
        if value is None:
            return None
        return FieldValue(value)


class FieldValue:
    """Wrapper for field values to provide bt2-compatible interface."""
    
    def __init__(self, value: Any):
        """Initialize field value.
        
        Args:
            value: The actual value
        """
        self._value = value
    
    @property
    def real(self):
        """Get the real numeric value (for bt2 compatibility)."""
        if isinstance(self._value, (int, float)):
            return self._value
        return self._value
    
    def __int__(self):
        """Convert to int."""
        return int(self._value)
    
    def __str__(self):
        """Convert to string."""
        return str(self._value)
    
    def __repr__(self):
        """String representation."""
        return repr(self._value)


# Provide a function to check if an object is an EventMessage (for isinstance checks)
def is_event_message(obj) -> bool:
    """Check if object is an EventMessage.
    
    Args:
        obj: Object to check
        
    Returns:
        True if obj is an EventMessage
    """
    return isinstance(obj, EventMessage)
