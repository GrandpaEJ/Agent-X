# TODO 02: AXML Decoder

**Files**: `src/axml.c`, `include/axml.h`  
**Depends on**: 01 (ZIP — to extract AndroidManifest.xml)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Decode Android's binary XML format (AXML) to human-readable XML text. Used for AndroidManifest.xml, layout files, and drawable XMLs.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 2.1 Header Validation
- [ ] Verify magic `0x00080003`
- [ ] Read file size, validate bounds

### 2.2 String Pool Parser (chunk type 0x0001)
- [ ] Parse string pool header (count, style count, flags, string start offset)
- [ ] Handle UTF-8 flag (bit 8) vs UTF-16
- [ ] Parse UTF-8 length prefixes (variable length 1 or 2 bytes) and read raw UTF-8 string data
- [ ] Parse UTF-16 length prefixes (2 bytes) and read raw UTF-16 string data
- [ ] Transcode strings to UTF-8 and store in indexed array for O(1) lookup

### 2.3 Resource ID Map (chunk type 0x0180)
- [ ] Parse resource ID array (maps string index → resource ID like `0x01010003`)
- [ ] Store for attribute name resolution

### 2.4 XML Node Parsers
- [ ] `START_NAMESPACE (0x0100)` — push namespace to stack
- [ ] `END_NAMESPACE (0x0101)` — pop namespace
- [ ] `START_ELEMENT (0x0102)` — read namespace URI, tag name, attribute array
- [ ] `END_ELEMENT (0x0103)` — pop element stack
- [ ] `TEXT / CDATA (0x0104)` — read text content

### 2.5 Attribute Parsing
- [ ] Parse 20-byte attribute struct (ns_idx, name_idx, raw_val_idx, val_size, val_res, val_type, val_data)
- [ ] Resolve `val_type` codes (0x01=ref, 0x03=string, 0x10=int, 0x12=bool, etc.)
- [ ] Format resource references as `@package:type/name` (resolve names using resources.arsc values)
- [ ] Handle self-closing elements and fallback reference hashes `@ref/0xXXXXXXXX` for obfuscated layout sheets

### 2.6 XML Output
- [ ] Track indentation level with element stack
- [ ] Emit namespace declarations
- [ ] Emit start/end tags with proper attributes
- [ ] Handle self-closing tags (no child elements)

### 2.7 Public API
- [ ] `axml_decode(const uint8_t *data, size_t size) → axml_ctx*`
- [ ] `axml_get_xml(axml_ctx *ctx) → const char*`
- [ ] `axml_free(axml_ctx *ctx)`

## Verification

```bash
# Decode AndroidManifest from APK
./agent-x read_axml test.apk/AndroidManifest.xml

# Compare with apktool output
apktool d test.apk -o /tmp/official
diff <(./agent-x read_axml test.apk/AndroidManifest.xml) /tmp/official/AndroidManifest.xml
```
