# TODO 07: ELF Parser

**Files**: `src/elf.c`, `include/elf.h`  
**Depends on**: Nothing (standalone)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Parse ELF binaries (`.so` files in APK `lib/`, plus standalone Linux binaries). Extract symbols, sections, strings, architecture info. Foundation for native code analysis.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 7.1 Header Parsing
- [ ] Validate magic (`\x7FELF`)
- [ ] Determine 32-bit vs 64-bit (EI_CLASS)
- [ ] Determine endianness (EI_DATA)
- [ ] Read: type, machine, entry point, program header offset/size, section header offset/size

### 7.2 Section Headers
- [ ] Parse section header table from `shoff`
- [ ] Read section names from `.shstrtab`
- [ ] Identify key sections: `.text`, `.rodata`, `.data`, `.bss`, `.dynsym`, `.dynstr`, `.plt`, `.got`

### 7.3 Symbol Table (.dynsym / .symtab)
- [ ] Parse symbol entries: name, address, size, type (FUNC, OBJECT), binding (LOCAL, GLOBAL)
- [ ] Resolve symbol names from `.dynstr` / `.strtab`
- [ ] `elf_get_symbol_count(elf_ctx*)`
- [ ] `elf_get_symbol_name(elf_ctx*, int) → const char*`
- [ ] `elf_get_symbol_address(elf_ctx*, int) → uint64_t`

### 7.4 Dynamic Section
- [ ] Parse `.dynamic` entries (DT_NEEDED, DT_SONAME, DT_INIT, DT_FINI)
- [ ] List shared library dependencies

### 7.5 String Extraction
- [ ] `elf_extract_strings(elf_ctx*, int min_len, int *count) → char**`
- [ ] Scan `.rodata` and merge strings
- [ ] Scan entire binary for readable ASCII/UTF-8 sequences

### 7.6 Architecture & Features
- [ ] Detect: ARM, Thumb, AArch64, x86, x64, RISC-V
- [ ] Detect: position-independent (PIE/PIC), stripped, has debug info
- [ ] Detect: has .init_array/.fini_array, has thread-local storage

### 7.7 Program Headers
- [ ] Parse PT_LOAD segments (virtual address, file offset, size)
- [ ] Parse PT_DYNAMIC
- [ ] Parse PT_GNU_STACK (executable stack detection)
- [ ] Parse PT_GNU_RELRO (relocation read-only)

### 7.8 Public API
- [ ] `elf_parse(const uint8_t *data, size_t size) → elf_ctx*`
- [ ] `elf_dump_info(elf_ctx*) → char*` (formatted report)
- [ ] `elf_get_arch(elf_ctx*) → enum` (ARCH_ARM, ARCH_ARM64, ARCH_X86, ...)

## Verification

```bash
# Analyze native library from APK
./agent-x analyze_elf test.apk/lib/arm64-v8a/libnative.so

# Check against readelf
readelf -h test.apk/lib/arm64-v8a/libnative.so
readelf -s test.apk/lib/arm64-v8a/libnative.so | head -20
```
