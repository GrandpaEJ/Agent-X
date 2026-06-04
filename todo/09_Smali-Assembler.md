# TODO 09: Smali Assembler (Smali → DEX)

**Files**: `src/smali.c`, `include/smali.h`  
**Depends on**: 03 (DEX Reader — for reference, format knowledge)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Assemble Smali source files back into DEX bytecode. This is the hardest component — a full compiler with lexer, parser, symbol table, and binary emitter.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 9.1 Smali Lexer
- [ ] Tokenize directives: `.class`, `.super`, `.source`, `.implements`, `.annotation`, `.field`, `.method`, `.end`, `.registers`, `.locals`, `.param`, `.prologue`, `.line`, `.catch`, `.catchall`, `.array-data`, `.packed-switch`, `.sparse-switch`
- [ ] Tokenize types: `I`, `Z`, `Ljava/lang/String;`, `[I`, etc.
- [ ] Tokenize registers: `v0`-`v65535`, `p0`-`p65535`
- [ ] Tokenize opcodes: all ~250 Dalvik mnemonics
- [ ] Tokenize labels: `:cond_0`, `:try_start_1`
- [ ] Tokenize literals: integers, strings, floats, method/field/type references
- [ ] Tokenize annotations: visibility, element-value pairs

### 9.2 Smali Parser
- [ ] Parse class header: `.class <access> <name>`, `.super`, `.source`, `.implements`
- [ ] Parse annotation blocks (nested, with values)
- [ ] Parse field declarations: `.field <access> <name>:<type> [= <value>]`
- [ ] Parse method declarations: `.method <access> <name>(<params>)<return>`
- [ ] Parse method body:
  - `.registers` / `.locals`
  - `.param` / `.line` / `.prologue`
  - Instruction stream with labels
  - `.catch` / `.catchall` blocks
  - `.packed-switch` / `.sparse-switch` tables
  - `.array-data`

### 9.3 Instruction Encoder
- [ ] Encode each parsed instruction back to binary format:
  - `10x` → 2 bytes
  - `12x` → low/high nibble packing
  - `35c` → register list + method ref
  - `3rc` → range format
  - `31t` → switch/data payload
- [ ] Handle all ~256 opcodes with correct format
- [ ] Validate register ranges fit in bitfield

### 9.4 Symbol Table Builder
- [ ] Collect all string constants → string pool (deduplicate, sort)
- [ ] Collect all type references → type pool (deduplicate, sort by string index)
- [ ] Collect all method references → method pool (deduplicate, sort by class→name→proto)
- [ ] Collect all field references → field pool (same sort)
- [ ] Collect all proto signatures → proto pool (sort by return→params)
- [ ] Maintain mapping between parsed AST references and pool indices

### 9.5 Label Resolution
- [ ] First pass: record label → byte offset mapping
- [ ] Second pass: resolve branch targets
  - `if-eqz v0, :cond_0` → compute relative offset in 16-bit units: $offset\_diff = (label\_offset - instruction\_offset) / 2$
  - `goto :label` → 8/16/32 bit depending on distance
  - Encode offsets into bytecode and verify bounds limitations
- [ ] Handle forward references (label defined after branch)

### 9.6 DEX Writer
- [ ] Layout the DEX file:
  1. Header (placeholder offsets)
  2. String IDs (4 bytes each → offset into data area)
  3. Type IDs (4 bytes each → string index)
  4. Proto IDs (12 bytes each)
  5. Field IDs (8 bytes each)
  6. Method IDs (8 bytes each)
  7. Class defs (32 bytes each)
  8. Data area: string data, code items, class data, debug info, map list
- [ ] Write code items for each method
- [ ] Write class_data_item (LEB128-encoded field/method lists)
- [ ] Write debug_info_item (line number table, parameter names)
- [ ] Write map_list at end of data area
- [ ] Sort indexing tables strictly (lexicographically for strings, index-sorted for types, protos, fields, methods)
- [ ] Compute Adler-32 checksum
- [ ] Compute SHA-1 signature
- [ ] Fill in header offsets and sizes

### 9.7 Error Handling
- [ ] Report line numbers for syntax errors
- [ ] Report unresolved references
- [ ] Report register out of range
- [ ] Report missing `.registers` or `.locals`

### 9.8 Public API
- [ ] `smali_assemble_file(const char *smali_text, size_t len) → uint8_t*` (single class)
- [ ] `smali_assemble_dir(const char *smali_dir, const char *out_dex)` (full directory)
- [ ] `smali_get_errors() → const char**`

## Verification

```bash
# Assemble a single Smali file
./agent-x smali_assemble HelloWorld.smali -o classes.dex

# Round-trip test: DEX → Smali → DEX
./agent-x dex_to_smali original.dex /tmp/smali
./agent-x smali_assemble /tmp/smali -o rebuilt.dex
diff <(sha256sum original.dex) <(sha256sum rebuilt.dex)
# Note: checksums won't match exactly (debug info, ordering differences),
# but dexdump output should be functionally equivalent.

# Compare with official smali/baksmali
java -jar smali.jar /tmp/smali -o official.dex
dexdump -d rebuilt.dex > /tmp/rebuilt.txt
dexdump -d official.dex > /tmp/official.txt
diff /tmp/rebuilt.txt /tmp/official.txt
```
