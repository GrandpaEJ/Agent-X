# TODO 06: APK Analyzer (Integration Tool)

**Files**: `src/apk.c`, `include/apk.h`  
**Depends on**: 01 (ZIP), 02 (AXML), 03 (DEX), 05 (ARSC)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Wire together ZIP + AXML + DEX + ARSC into a unified `analyze_apk` command. Single entry point that opens an APK, extracts key info, and returns a structured analysis report.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 6.1 APK Metadata Extraction
- [ ] Open APK as ZIP
- [ ] Read `AndroidManifest.xml` via AXML decoder
- [ ] Extract: package name, version code, version name, minSdk, targetSdk, permissions
- [ ] List all activities, services, receivers, providers
- [ ] List all intent filters

### 6.2 DEX Summary
- [ ] For each `classes*.dex`:
  - [ ] Count of classes, methods, fields, strings
  - [ ] Top-level package names (unique prefixes)
  - [ ] Count of native methods (JNI bridge)
- [ ] Multi-DEX detection (`classes2.dex`, `classes3.dex` ...)

### 6.3 Resource Analysis
- [ ] Parse `resources.arsc`
- [ ] Extract: layout list, string table, drawable list
- [ ] Map resource IDs to names for AXML reference resolution

### 6.4 Native Library Analysis
- [ ] List all entries in `lib/*/`
- [ ] Detect architectures present (armeabi-v7a, arm64-v8a, x86, x86_64)
- [ ] Count native libraries per arch

### 6.5 String Extraction
- [ ] Extract all strings from DEX string table
- [ ] Extract all strings from ARSC string pools
- [ ] Filter interesting patterns: URLs, IPs, API endpoints, encryption keys, auth tokens

### 6.6 Report Output
- [ ] JSON formatted report
- [ ] Sections: manifest, dex_summary, resources, native_libs, strings
- [ ] Human-readable text format fallback

### 6.7 Public API
- [ ] `analyze_apk(const char *path) → char*` (JSON report)

## Verification

```bash
# Full APK analysis
./agent-x analyze_apk test.apk

# Verify key fields match reality
./agent-x analyze_apk test.apk | jq '.manifest.package'
./agent-x analyze_apk test.apk | jq '.manifest.permissions'
./agent-x analyze_apk test.apk | jq '.dex_summary[0].class_count'
```
