# TODO 03: DEX Reader

**Files**: `src/dex.c`, `include/dex.h`  
**Depends on**: 01 (ZIP — to extract classes.dex)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Parse DEX file format: validate headers, build index tables for strings, types, protos, fields, methods, and classes. This is the foundation for disassembly and analysis.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 3.1 Header Parsing
- [ ] Define packed struct for DEX header
- [ ] Validate magic (`dex\n035\0` or `dex\n039\0`)
- [ ] Verify Adler-32 checksum (all bytes after checksum field)
- [ ] Verify SHA-1 signature
- [ ] Validate endian tag (`0x12345678`)
- [ ] Read all ID table sizes and offsets
- [ ] Store `dex_ctx` with pointers to each table

### 3.2 String Table
- [ ] `dex_get_string_count(dex_ctx*)` 
- [ ] `dex_get_string(dex_ctx*, int idx) → const char*`
- [ ] Parse MUTF-8 (Modified UTF-8: null bytes encoded as `0xC0 0x80`)
- [ ] Cache strings after first decode

### 3.3 Type Table
- [ ] `dex_get_type_count(dex_ctx*)`
- [ ] `dex_get_type(dex_ctx*, int idx) → const char*` (e.g., `Ljava/lang/String;`)
- [ ] Type IDs are indices into string table — resolve and cache

### 3.4 Proto Table
- [ ] `dex_get_proto_count(dex_ctx*)`
- [ ] `dex_get_proto_shorty(dex_ctx*, int)` — e.g., `(Landroid/os/Bundle;)V`
- [ ] `dex_get_proto_return_type(dex_ctx*, int) → type string`
- [ ] `dex_get_proto_params(dex_ctx*, int) → type string list`
- [ ] Parse proto struct (shorty_idx, return_type_idx, parameters_off)

### 3.5 Field Table
- [ ] `dex_get_field_count(dex_ctx*)`
- [ ] `dex_get_field(dex_ctx*, int) → {class, type, name}`
- [ ] Format: `Lcom/example/Foo;->bar:Ljava/lang/String;`

### 3.6 Method Table
- [ ] `dex_get_method_count(dex_ctx*)`
- [ ] `dex_get_method(dex_ctx*, int) → {class, proto, name}`
- [ ] Format: `Lcom/example/Foo;->doStuff(ILjava/lang/String;)V`

### 3.7 Class Definitions
- [ ] `dex_get_class_count(dex_ctx*)`
- [ ] `dex_get_class(dex_ctx*, int) → {type, access_flags, super, interfaces}`
- [ ] Implement ULEB128/SLEB128 byte-decoding loops for parsing variable-length fields
- [ ] Parse `class_data_item` using LEB128 (fields, methods, virtual methods)

### 3.8 Code Items
- [ ] `dex_get_method_code(dex_ctx*, int method_idx) → dex_code*`
- [ ] Parse code_item: registers, ins, outs, tries_size, debug_info_off, insns_size
- [ ] Return raw `uint16_t*` instruction array + size

### 3.9 Try/Catch
- [ ] Parse try_item array (start_addr, end_addr, catch_handler_off)
- [ ] Parse encoded_catch_handler_list
- [ ] `dex_code_get_tries(dex_code*) → dex_try[]`

### 3.10 Map List
- [ ] Parse map_list at `map_offset` for validation
- [ ] Verify all section types are present and non-overlapping

## Verification

```bash
# Dump DEX info
./agent-x dex_info classes.dex
# Output:
#   Strings: 12453
#   Types: 2341
#   Protos: 1892
#   Fields: 3456
#   Methods: 8921
#   Classes: 567

# Verify against dexdump
dexdump -f classes.dex | head -20
# Compare string count, type count, etc.
```
