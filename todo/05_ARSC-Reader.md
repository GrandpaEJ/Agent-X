# TODO 05: ARSC Reader (resources.arsc)

**Files**: `src/arsc.c`, `include/arsc.h`  
**Depends on**: 01 (ZIP — to extract resources.arsc)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Parse Android's binary resource table (`resources.arsc`). Resolve resource IDs (e.g. `0x7F030001`) to readable names and values. Essential for decoding layout references and understanding app resources.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 5.1 Top-Level Table Parser
- [ ] Parse RES_TABLE header (chunk type 0x0002): package count
- [ ] Read global string pool (shared strings across packages)

### 5.2 Package Chunk (0x0200)
- [ ] Parse package header: package ID, package name
- [ ] Read type string pool (type names: "string", "drawable", "layout", etc.)
- [ ] Read key string pool (entry names: "app_name", "activity_main", etc.)
- [ ] Parse type spec chunks (`0x0202`) and dynamic configuration structures (`0x0201` type config) to support multi-locale resources

### 5.3 Type Spec Chunk (0x0202)
- [ ] Parse per-type spec: type ID, entry count, flags for each entry
- [ ] Flags indicate which config variants exist for each resource

### 5.4 Type Config Chunk (0x0201)
- [ ] Parse config header (48+ bytes): locale, screen density, SDK, orientation, etc.
- [ ] Read entry offset array (indices into resource entries)
- [ ] Handle sparse entry tables (0xFFFF = no entry)

### 5.5 Entry Values
- [ ] Parse RES_TABLE_ENTRY (0x0008):
  - [ ] Simple values: `{type, data}` — string ref, int, bool, color, dimension, etc.
  - [ ] Complex values (bags): parent ID, item count, key-value pairs
- [ ] Decode typed value format:
  - `0x01`: Reference (another resource ID)
  - `0x03`: String (index into package string pool)
  - `0x05`: Boolean
  - `0x10`: Integer (decimal)
  - `0x11`: Integer (hex)
  - `0x12`: Color (24-bit RGB / 32-bit ARGB)
  - `0x1D`: Dimension (dp, sp, px, pt, etc.)
  - `0x1E`: Float

### 5.6 Resource Resolution
- [ ] `arsc_resolve_id(arsc_ctx*, uint32_t res_id) → char*`
  - Split `0xPPTTEEEE` → package, type, entry
  - Look up: `@package:type/entry_name`
  - Example: `0x7F030001` → `@string/app_name`
- [ ] `arsc_get_value(arsc_ctx*, uint32_t res_id) → char*`
  - Return formatted value as string

### 5.7 Public API
- [ ] `arsc_parse(const uint8_t *data, size_t size) → arsc_ctx*`
- [ ] `arsc_get_package_count(arsc_ctx*)`
- [ ] `arsc_get_type_count(arsc_ctx*, int pkg)`
- [ ] `arsc_dump_table(arsc_ctx*) → char*` (JSON or formatted text)

## Verification

```bash
# Dump resource table
./agent-x dump_arsc resources.arsc

# Resolve specific resource IDs
./agent-x arsc_resolve resources.arsc 0x7F030001
./agent-x arsc_resolve resources.arsc 0x01040001

# Compare with aapt
aapt d resources test.apk | head -50
```
