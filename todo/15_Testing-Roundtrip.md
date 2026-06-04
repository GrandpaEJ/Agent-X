# TODO 15: Testing & Round-Trip Verification

**Files**: `tests/` directory, test scripts  
**Depends on**: All prior phases  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files) (test scripts + test fixtures)  

## Objective

Build a comprehensive test suite to verify correctness of every module. Round-trip tests ensure that decode→modify→rebuild produces valid APKs.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 15.1 Unit Test Framework
- [ ] Create `tests/` directory with test fixtures:
  - `tests/fixtures/hello.apk` — minimal APK (Hello World)
  - `tests/fixtures/complex.apk` — APK with resources, libs, multi-DEX
  - `tests/fixtures/sample.dex` — standalone DEX
  - `tests/fixtures/sample.arsc` — standalone resource table
  - `tests/fixtures/sample.axml` — standalone binary XML
  - `tests/fixtures/sample.elf` — standalone ELF (.so)
- [ ] Create `tests/run_tests.sh` — automated test runner

### 15.2 ZIP Tests
- [ ] Read test APK, verify entry count matches `unzip -l`
- [ ] Extract specific entry, verify CRC matches
- [ ] Create ZIP from directory, verify round-trip

### 15.3 AXML Tests
- [ ] Decode sample AndroidManifest.xml, compare with apktool output
- [ ] Verify string pool parsing matches reference
- [ ] Test edge cases: empty namespace, missing attributes, malformed input

### 15.4 DEX Tests
- [ ] Parse sample DEX, verify header fields match `dexdump -f`
- [ ] Verify string count, type count match reference
- [ ] Disassemble all methods, compare with `baksmali` output
- [ ] Round-trip: DEX → Smali → DEX, verify structural equivalence

### 15.5 ARSC Tests
- [ ] Parse sample resources.arsc, compare with `aapt dump resources`
- [ ] Resolve known resource IDs to expected values

### 15.6 ELF Tests
- [ ] Parse sample .so, verify symbol count matches `readelf -s`
- [ ] Verify architecture detection

### 15.7 Full APK Round-Trip
- [ ] Decode test APK → directory
- [ ] Rebuild directory → new APK
- [ ] Sign new APK
- [ ] Verify with `apksigner verify`
- [ ] Re-decode rebuilt APK, compare structural elements
- [ ] **Critical**: Verify that the app installs and runs (on emulator)

### 15.8 Edge Case Tests
- [ ] Multi-DEX APK (classes2.dex, classes3.dex)
- [ ] APK with no resources (no resources.arsc)
- [ ] APK with no native libs
- [ ] Obfuscated APK (ProGuard/R8 minified)
- [ ] APK with Kotlin metadata
- [ ] APK with split APK configurations
- [ ] DEX v035 vs v039 format

### 15.9 Performance Tests
- [ ] Benchmark: time to parse a 100MB+ APK
- [ ] Benchmark: memory usage during large APK analysis
- [ ] Memory leak check (valgrind for C / `-fsanitize=address`)

## Verification

```bash
# Run all tests
./tests/run_tests.sh

# Individual module tests
./tests/test_zip.sh
./tests/test_axml.sh
./tests/test_dex.sh
./tests/test_arsc.sh
./tests/test_elf.sh
./tests/test_smali.sh

# Full round-trip
./tests/test_roundtrip.sh

# Memory check
zig cc -fsanitize=address -o agent-x-test src/*.c vendor/*/*.c
./agent-x-test analyze_apk tests/fixtures/complex.apk
```
