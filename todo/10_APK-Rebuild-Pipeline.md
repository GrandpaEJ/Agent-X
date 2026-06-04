# TODO 10: APK Rebuild Pipeline

**Files**: `src/rebuild.c`, `include/rebuild.h`  
**Depends on**: 01 (ZIP), 02 (AXML), 05 (ARSC), 08 (Sign), 09 (Smali)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Complete the APKTool `b` (build) command: take a decoded APK directory and produce a signed, aligned APK.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 10.1 Smali Compilation
- [ ] Scan `smali/` directory for `.smali` files
- [ ] Scan `smali_classes2/`, `smali_classes3/`, etc. for multi-DEX
- [ ] Assemble each directory → `classes.dex`, `classes2.dex`, etc.
- [ ] Handle empty smali directories (pass-through if no modifications)

### 10.2 Resource Rebuild
- [ ] Read `res/values/*.xml` and rebuild `resources.arsc`:
  - [ ] Parse XML key-value pairs (strings.xml, colors.xml, dimens.xml, etc.)
  - [ ] Assign resource IDs (starting at 0x7F010000)
  - [ ] Build type string pool, key string pool, package string pool
  - [ ] Build TypeSpec + TypeConfig chunks for each config variant
- [ ] Rebuild layout XMLs → binary AXML:
  - [ ] Parse text XML with lightweight XML parser
  - [ ] Build string pool
  - [ ] Emit AXML chunks
- [ ] Copy: 9-patch images, raw assets, drawables untouched

### 10.3 ZIP Assembly
- [ ] Collect all files to include:
  - `classes.dex` (stored, uncompressed)
  - `AndroidManifest.xml` (binary AXML)
  - `resources.arsc` (stored)
  - `res/` (compressed: XML, drawables; stored: raw)
  - `lib/` (stored: .so files need alignment)
  - `assets/` (compressed)
  - `kotlin/` (compressed)
- [ ] Track which files need 4-byte alignment
- [ ] Write ZIP with proper compression flags

### 10.4 zipalign
- [ ] Run `zipalign(4)` on the generated ZIP
- [ ] Ensure all stored entries align to 4 bytes

### 10.5 Signing
- [ ] Generate signing key on first build (stored in `build_key.jks`)
- [ ] Sign with v1 (JAR) + v2 (APK Signing Block)
- [ ] Handle existing signatures (strip `META-INF/` before re-signing)

### 10.6 Public API
- [ ] `rebuild_apk(const char *input_dir, const char *output_apk, int sign) → int`

## Verification

```bash
# Full rebuild
./agent-x rebuild_apk /tmp/decoded_app -o rebuilt.apk --sign

# Verify the APK is valid
unzip -l rebuilt.apk
apksigner verify rebuilt.apk

# Install and test (on device/emulator)
adb install rebuilt.apk
```
