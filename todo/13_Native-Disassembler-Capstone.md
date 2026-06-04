# TODO 13: Native Code Disassembler

**Files**: `src/disasm.c`, `include/disasm.h`  
**Depends on**: 07 (ELF — to extract .text section)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files) (wrapper) or ~3000-5000 (custom)  

## Objective

Disassemble native code from `.so` libraries. Decision: use Capstone (fast, complete, C library) or build custom minimal disassembler.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 13.1 Decision: Capstone vs Custom
- [x] Evaluate Capstone: `vendor/capstone/` — single-header, ~500 KB binary size impact
- [ ] If Capstone: write thin wrapper
- [ ] If custom: implement per-architecture decoders

### 13.2 Capstone Integration (Recommended)
- [ ] Vendor Capstone: `git clone --depth 1 https://github.com/capstone-engine/capstone vendor/capstone`
- [ ] Add to Makefile: compile capstone sources
- [ ] `disasm_init(arch_t arch, mode_t mode) → csh`
- [ ] `disasm_buffer(csh handle, const uint8_t *code, size_t size, uint64_t address) → char*`
- [ ] `disasm_close(csh handle)`

### 13.3 Custom ARM/Thumb Disassembler (Optional, for size)
- [ ] Implement ARM instruction decoder: ~400 instructions
- [ ] Implement Thumb decoder: ~300 instructions (subset)
- [ ] Handle data processing, load/store, branch, coprocessor
- [ ] Handle Thumb → ARM interworking (BLX, BX)

### 13.4 Custom AArch64 Disassembler (Optional)
- [ ] Implement base instruction set: ~500 instructions
- [ ] Handle load/store, arithmetic, branch, system, SIMD basics

### 13.5 Custom x86/x64 Disassembler (Optional)
- [ ] Variable-length instruction decoding (1-15 bytes)
- [ ] ModRM, SIB, displacement, immediate decoding
- [ ] Handle prefixes (REX, VEX, EVEX — for AVX)

### 13.6 Integration with ELF Parser
- [ ] Extract `.text` section from ELF
- [ ] Determine architecture from ELF header (EM_ARM, EM_AARCH64, EM_386, EM_X86_64)
- [ ] Disassemble at symbol boundaries
- [ ] Annotate with symbol names

### 13.7 Public API
- [ ] `disassemble_binary(const uint8_t *code, size_t size, int arch, const char *sym_name) → char*`

## Verification

```bash
# Disassemble native library
./agent-x disassemble libnative.so arch=arm64

# Compare with objdump
objdump -d libnative.so | head -50
```
