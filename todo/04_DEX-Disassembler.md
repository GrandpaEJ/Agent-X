# TODO 04: DEX Disassembler (DEX → Smali)

**Files**: `src/dex.c` (append), `include/dex.h` (append)  
**Depends on**: 03 (DEX Reader)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Disassemble Dalvik bytecode to Smali text format. This is the core of APKTool's `d` command. Requires implementing the full Dalvik opcode table (~256 opcodes) and resolving all references.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 4.1 Opcode Table
- [ ] Define `dex_insn` struct with decoded fields (opcode, fmt, vA-vG, raw_B, literal, target)
- [ ] Create opcode dispatch table: `opcode_handler handlers[256]`
- [ ] Implement handle function for each instruction **format** (not each opcode):
  - `10x` — nop
  - `12x` — move, move-object, array-length, etc.
  - `11n` — const/4
  - `21c` — const-string, const-class, const-type
  - `22c` — instance-of, new-array, iget, iput, sget, sput, check-cast
  - `35c` — invoke-virtual, invoke-direct, invoke-static, invoke-interface
  - `3rc` — invoke-virtual/range, invoke-direct/range, etc.
  - `21t` — if-eqz, if-nez, etc.
  - `22t` — if-eq, if-ne, etc.
  - `10t` — goto
  - `20t` — goto/16
  - `30t` — goto/32
  - `31t` — packed-switch, sparse-switch, fill-array-data
  - `23x` — cmp-* instructions
  - `12x-3` — unary ops (neg-int, not-long, int-to-long, etc.)
  - `23x-3` — binary ops (add-int, sub-long, mul-float, etc.)

### 4.2 Reference Resolution
- [ ] `dex_resolve_method(dex_ctx*, uint32_t method_idx) → const char*`
- [ ] `dex_resolve_type(dex_ctx*, uint32_t type_idx) → const char*`
- [ ] `dex_resolve_field(dex_ctx*, uint32_t field_idx) → const char*`
- [ ] `dex_resolve_string(dex_ctx*, uint32_t string_idx) → const char*`
- [ ] `dex_resolve_proto(dex_ctx*, uint32_t proto_idx) → const char*`

### 4.3 Smali Output — Method Body
- [ ] Calculate input parameter registers (p0, p1...) vs local registers (v0, v1...) where p0 is 'this' for non-static, and parameters map to the highest registers in the method frame.
- [ ] Handle range instruction register mapping formats (`3rc` or `35c` range formats).
- [ ] `.registers N` / `.locals N`
- [ ] `.param` directives from debug info
- [ ] `.prologue` / `.line N`
- [ ] Instruction mnemonics with resolved references:
  - `invoke-virtual {v0, v1}, Lcom/example/Foo;->bar(I)V`
  - `const-string v0, "hello"`
  - `if-eqz v0, :cond_0`
- [ ] Label generation for branch targets (`:cond_0`, `:try_start_0`, etc.)
- [ ] `.catch` / `.catchall` directives from try/catch blocks
- [ ] `.packed-switch` / `.sparse-switch` data

### 4.4 Smali Output — Class Level
- [ ] `.class <access> <name>` — with resolved access flags
- [ ] `.super <type>`
- [ ] `.source "<file>"`
- [ ] `.implements <type>` (interfaces)
- [ ] `.annotation` blocks (visibility + type + values)
- [ ] `.field <access> <name>:<type>` — with static values
- [ ] `.method` / `.end method` — full method blocks

### 4.5 Multi-DEX
- [ ] Handle `classes.dex`, `classes2.dex`, `classes3.dex` ...

### 4.6 Public API
- [ ] `dex_disassemble_method(dex_ctx*, int method_idx) → char*` (Smali text)
- [ ] `dex_disassemble_class(dex_ctx*, int class_idx) → char*` (full Smali file)
- [ ] `dex_to_smali(dex_ctx*, const char *out_dir)` — write all classes to files

## Verification

```bash
# Disassemble a single method
./agent-x dex_method classes.dex 42

# Full Smali output
./agent-x dex_to_smali classes.dex /tmp/smali_out

# Compare with official apktool
apktool d test.apk -o /tmp/official
diff -r /tmp/smali_out /tmp/official/smali/
```
