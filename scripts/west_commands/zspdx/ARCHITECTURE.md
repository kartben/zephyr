# zspdx Architecture

## Overview

The zspdx module generates Software Bill of Materials (SBOM) documents from Zephyr build artifacts. It has been designed with a clean separation of concerns between generic SBOM generation and format-specific serialization.

## Architecture

The module follows a **multi-layer architecture** with clear separation of concerns:

```
┌─────────────────────────────────────────────────────────┐
│                    CLI Layer (spdx.py)                   │
│                  Entry point and config                   │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                  Orchestration (sbom.py)                 │
│         Coordinates walker, adapter, scanner, writer      │
└─────────────────────────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   Walker     │    │    Scanner   │    │    Writer    │
│ (walker.py)  │    │ (scanner.py) │    │ (writer.py)  │
│              │    │              │    │              │
│ Format-      │    │ License      │    │ SPDX 2.x     │
│ agnostic     │    │ detection    │    │ tag-value    │
│ SBOM builder │    │              │    │ serializer   │
└──────────────┘    └──────────────┘    └──────────────┘
        │                                       ▲
        │                                       │
        ▼                                       │
┌──────────────┐                       ┌──────────────┐
│   Generic    │    Conversion         │    SPDX      │
│  Datatypes   │◄──────────────────────│   Adapter    │
│ (generic_*)  │                       │(spdx_adapter)│
└──────────────┘                       └──────────────┘
                                               │
                                               ▼
                                       ┌──────────────┐
                                       │    SPDX      │
                                       │  Datatypes   │
                                       │(datatypes.py)│
                                       └──────────────┘
```

## Components

### 1. Walker (walker.py)
**Purpose**: Traverse CMake build artifacts and source code to collect SBOM information.

**Key Characteristics**:
- **Format-agnostic**: Uses generic datatypes, no SPDX-specific concepts
- **No serialization logic**: Doesn't know about output formats
- **No ID prefixing**: Uses simple identifiers without format-specific prefixes

**Produces**: Generic SBOM structures (GenericDocument, GenericPackage, GenericFile, etc.)

### 2. Generic Data Structures (generic_datatypes.py, generic_ids.py)
**Purpose**: Define format-agnostic SBOM data structures and ID generation.

**Key Characteristics**:
- Generic document/package/file/relationship classes
- No format-specific fields (e.g., no "spdxID", "namespace")
- Simple ID generation without format-specific prefixes
- Can be adapted to any SBOM format (SPDX, CycloneDX, SWID, etc.)

**Classes**:
- `GenericDocument`: Top-level SBOM container
- `GenericPackage`: Software package/component
- `GenericFile`: Individual file in a package
- `GenericRelationship`: Relationship between elements
- `GenericRelationshipData`: Pre-processed relationship data

### 3. SPDX Adapter (spdx_adapter.py)
**Purpose**: Convert generic SBOM structures to SPDX 2.x format.

**Key Characteristics**:
- Adds SPDX-specific ID prefixes ("SPDXRef-", "DocumentRef-")
- Generates SPDX namespaces from namespace prefix + doc name
- Maps generic fields to SPDX-specific fields
- Handles cross-document references

**Main Class**:
- `SPDXAdapter`: Converts entire document tree from generic to SPDX

### 4. SPDX Data Structures (datatypes.py)
**Purpose**: Define SPDX 2.x-specific data structures.

**Key Characteristics**:
- Includes SPDX-specific fields (spdxID, namespace, etc.)
- Used by scanner and writer layers
- Represents the SPDX specification data model

### 5. Scanner (scanner.py)
**Purpose**: Scan files for license information and calculate hashes.

**Works with**: SPDX documents (after conversion from generic)

### 6. Writer (writer.py)
**Purpose**: Serialize SPDX documents to SPDX 2.x tag-value format.

**Works with**: SPDX documents only

### 7. Orchestrator (sbom.py)
**Purpose**: Coordinate the entire SBOM generation pipeline.

**Flow**:
1. Create Walker with generic config
2. Walker produces generic documents
3. SPDX Adapter converts generic → SPDX
4. Scanner adds license/hash information to SPDX docs
5. Writer serializes SPDX docs to files

## Data Flow

```
CMake Build Files
       │
       ▼
   Walker.makeDocuments()
       │
       ▼
Generic Documents
       │
       ▼
SPDXAdapter.convert_all_documents()
       │
       ▼
SPDX Documents
       │
       ▼
scanDocument() (adds licenses/hashes)
       │
       ▼
writeSPDX() (serializes to .spdx files)
       │
       ▼
SPDX 2.x Tag-Value Files
```

## Benefits of This Architecture

### 1. **Separation of Concerns**
- Walker focuses on build artifact analysis
- Adapter focuses on format conversion
- Writer focuses on serialization

### 2. **Format Independence**
- Walker is completely agnostic of output format
- Easy to add support for other SBOM formats (CycloneDX, SWID, etc.)
- Just create a new adapter (e.g., `cyclonedx_adapter.py`)

### 3. **Testability**
- Each layer can be tested independently
- Generic structures are simpler to test
- Format-specific logic is isolated

### 4. **Maintainability**
- Changes to SPDX spec only affect adapter and writer
- Changes to build analysis only affect walker
- Clear boundaries between components

## Adding Support for New Formats

To add support for a new SBOM format (e.g., CycloneDX):

1. Create new datatypes module (e.g., `cyclonedx_datatypes.py`)
2. Create new adapter (e.g., `cyclonedx_adapter.py`) that converts generic → format-specific
3. Create new writer (e.g., `cyclonedx_writer.py`) for serialization
4. Update orchestrator to support the new format

**No changes needed to walker.py** - it remains format-agnostic!

## Example Usage

```python
from zspdx.walker import Walker, WalkerConfig
from zspdx.spdx_adapter import SPDXAdapter
from zspdx.writer import writeSPDX

# 1. Configure and run walker (format-agnostic)
cfg = WalkerConfig()
cfg.buildDir = "/path/to/build"
walker = Walker(cfg)
walker.makeDocuments()

# 2. Convert to SPDX format
adapter = SPDXAdapter(namespacePrefix="http://example.com/sbom")
spdxDocs = adapter.convert_all_documents([
    walker.docBuild,
    walker.docZephyr,
    walker.docApp
])

# 3. Write SPDX documents
for doc in spdxDocs:
    writeSPDX(f"{doc.cfg.name}.spdx", doc)
```

## Migration Notes

### Before Refactoring
- Walker used SPDX-specific datatypes directly
- SPDX IDs generated in walker with "SPDXRef-" prefix
- Namespace generation mixed into walker logic
- Tight coupling between analysis and serialization

### After Refactoring
- Walker uses generic datatypes
- Simple IDs generated without format-specific prefixes
- Adapter handles all format-specific transformations
- Clear separation: analysis → conversion → serialization

## Files

- `walker.py` - Format-agnostic SBOM builder (854 lines)
- `generic_datatypes.py` - Generic SBOM data structures (236 lines)
- `generic_ids.py` - Format-agnostic ID generation (98 lines)
- `spdx_adapter.py` - Generic → SPDX converter (314 lines)
- `datatypes.py` - SPDX-specific data structures (252 lines)
- `sbom.py` - Orchestrator (139 lines)
- `writer.py` - SPDX 2.x serializer (229 lines)
- `scanner.py` - License/hash scanner (works with SPDX docs)
