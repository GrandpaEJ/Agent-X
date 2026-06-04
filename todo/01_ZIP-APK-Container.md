# TODO 01: ZIP / APK Container Module

**Files**: `src/zip.c`, `include/zip.h`  
**Depends on**: Nothing (standalone)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Build a ZIP reader/writer that handles APK-specific constraints (alignment, signing block gap). Decide: vendor `miniz` or write from scratch.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 1.1 Decision: miniz vs custom
- [x] Evaluate miniz (vendor/miniz/miniz.h) — single-file, Apache 2.0, well-tested
- [ ] If using miniz: write thin wrapper `zip.c` that maps our API to miniz
- [ ] If custom: implement ZIP parser from spec (LFH, CDFH, EOCD)

### 1.2 Core API — Reading
- [ ] `zip_open(const char *path)` — mmap or read file, locate EOCD, build entry index
- [ ] `zip_close(void *archive)` — free all resources
- [ ] `zip_get_num_entries(void *archive)`
- [ ] `zip_get_entry_name(void *archive, int index)`
- [ ] `zip_get_entry_size(void *archive, int index)`
- [ ] `zip_entry_is_compressed(void *archive, int index)`
- [ ] `zip_extract_entry(void *archive, int index, size_t *out_size)` — decompress if needed

### 1.3 Core API — Writing
- [ ] `zip_create(const char *path)` — create new ZIP
- [ ] `zip_add_entry(void *archive, const char *name, const void *data, size_t size, int compress)`
- [ ] `zip_close_write(void *archive)` — flush CDFH + EOCD

### 1.4 APK-Specific
- [ ] `zip_extract_all(void *archive, const char *out_dir)` — extract all entries to disk
- [ ] `zip_create_from_dir(const char *dir, const char *out_zip)` — repack directory to ZIP

### 1.5 zipalign
- [ ] `zipalign(const char *in_zip, const char *out_zip, int alignment)` — ensure all stored entries are alignment-aligned
- [ ] Implement LFH padding calculation: $padding = (4 - (current\_offset \pmod 4)) \pmod 4$
- [ ] Write padding bytes of `0x00` in LFH `Extra field` and adjust `extra_field_len` in LFH
- [ ] Update central directory offsets to point to the correct aligned LFH start

### 1.6 APK Signing Block Awareness
- [ ] Detect APK Signing Block by scanning backward from EOCD for magic `APK Sig Block 42` (16 bytes)
- [ ] Parse and skip the block size prefixes/suffixes to isolate raw zip central directory
- [ ] Preserve signing block gap and content intact when rebuilding/rewriting container

## Verification

```bash
# Test reading
./agent-x zip_read test.apk                 # list entries
./agent-x zip_extract test.apk /tmp/out     # extract all

# Test writing + alignment
./agent-x zip_create test_repack.apk /tmp/out_dir
./agent-x zipalign test_repack.apk

# Compare with unzip -l
unzip -l test.apk | wc -l
unzip -l test_repack.apk | wc -l   # should match
```
