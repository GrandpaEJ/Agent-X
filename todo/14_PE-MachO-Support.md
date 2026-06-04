# TODO 14: PE & Mach-O Parser

**Files**: `src/pe.c`, `include/pe.h`, `src/macho.c`, `include/macho.h`  
**Depends on**: Nothing (standalone, similar structure to ELF parser)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files) each  

## Objective

Add support for Windows PE (.exe, .dll) and macOS/iOS Mach-O (.dylib, .o) binaries. Agent X becomes a cross-platform binary analysis tool.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 14.1 PE Parser (Windows Executables)
- [ ] Validate DOS header + `MZ` magic
- [ ] Read `e_lfanew` to locate PE signature (`PE\0\0`)
- [ ] Parse COFF header (machine, sections, symbols)
- [ ] Parse PE optional header:
  - [ ] Entry point, image base, section alignment
  - [ ] Subsystem (GUI, console, driver)
  - [ ] DLL characteristics (ASLR, NX, DEP)
- [ ] Parse section headers:
  - [ ] `.text` — code
  - [ ] `.rdata` — read-only data
  - [ ] `.data` — initialized data
  - [ ] `.rsrc` — resources
  - [ ] `.reloc` — relocations
- [ ] Parse import table (DLL dependencies, imported functions)
- [ ] Parse export table (exported functions)
- [ ] Parse resource directory (version info, icons, manifests)
- [ ] **Public API**: `pe_parse()`, `pe_get_imports()`, `pe_get_exports()`, `pe_get_sections()`

### 14.2 Mach-O Parser (macOS/iOS)
- [ ] Validate magic: `0xFEEDFACE` (32-bit), `0xFEEDFACF` (64-bit), `0xCAFEBABE` (FAT)
- [ ] Parse header: CPU type/subtype, file type (MH_EXECUTE, MH_DYLIB, MH_OBJECT)
- [ ] Parse load commands:
  - [ ] `LC_SEGMENT_64` — segment/section definitions
  - [ ] `LC_SYMTAB` — symbol table + string table
  - [ ] `LC_DYSYMTAB` — dynamic symbol info
  - [ ] `LC_LOAD_DYLIB` — linked libraries
  - [ ] `LC_CODE_SIGNATURE` — code signing info
  - [ ] `LC_ENCRYPTION_INFO` — encryption info (iOS apps)
- [ ] Parse sections:
  - [ ] `__text` — executable code
  - [ ] `__cstring` — C strings
  - [ ] `__const` — constant data
  - [ ] `__objc_*` — Objective-C runtime metadata
  - [ ] `__swift_*` — Swift runtime metadata
- [ ] Extract Objective-C class list (for iOS RE)
- [ ] Extract Swift protocol/conformance info
- [ ] **Public API**: `macho_parse()`, `macho_get_symbols()`, `macho_get_objc_classes()`, `macho_get_libraries()`

### 14.3 FAT Binary Support (Universal Binaries)
- [ ] Parse FAT header (list of architectures)
- [ ] Support extracting slices by architecture
- [ ] Common on macOS (x86_64 + arm64) and iOS (armv7 + arm64)

## Verification

```bash
# Analyze Windows PE
./agent-x analyze_pe test.exe
# Output: sections, imports (kernel32.dll, etc.), exports

# Analyze macOS/iOS Mach-O
./agent-x analyze_macho test.dylib
# Output: architecture, Objective-C classes, linked libraries

# FAT binary
./agent-x analyze_macho universal_binary
# Output: contains arm64 + x86_64 slices
```
