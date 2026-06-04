# TODO 12: Structured Decompiler (Smali → Pseudocode / Java)

**Files**: `src/decompile.c`, `include/decompile.h`  
**Depends on**: 04 (DEX Disassembler)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Go beyond Smali. Produce structured pseudocode or Java-like output with control flow recovery (if/else, loops, try/catch). This is JADX-level decompilation in C.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 12.1 Basic Block Detection
- [ ] Walk linear instruction stream → identify basic blocks
  - Block starts at: function entry, branch targets, exception handler entry
  - Block ends at: unconditional branch, conditional branch, return, throw
- [ ] Build control flow graph (CFG): edges between blocks

### 12.2 Structured Control Flow Recovery
- [ ] If/else recovery: `if-eq` → `if (...) {` / `} else {`
- [ ] Loop recovery:
  - [ ] `while` loops (header-controlled)
  - [ ] `for` loops (init + condition + increment)
  - [ ] `do-while` loops (footer-controlled)
- [ ] `switch` recovery: `packed-switch` / `sparse-switch` → `switch(...) { case: ... }`
- [ ] Ternary operator recovery: `if-eq` → `(cond) ? a : b`

### 12.3 Exception Handling Recovery
- [ ] Parse try/catch table
- [ ] Recover structured try/catch/finally from flat table
- [ ] Handle nested try/catch

### 12.4 Variable Tracking
- [ ] Track register assignments across basic blocks
- [ ] SSA-like renaming for variables used in multiple blocks
- [ ] Type inference for registers:
  - `const-string` → String type
  - `new-instance` → object type from class ref
  - `aget` → array type from element type

### 12.5 Expression Reconstruction
- [ ] `add-int v0, v1, v2` → `v0 = v1 + v2`
- [ ] `invoke-virtual {v0, v1}, StringBuilder.append` → `sb.append(v1)`
- [ ] `iget v0, v1, Foo.bar` → `v0 = v1.bar`
- [ ] `const-string v0, "hello"` → `"hello"`
- [ ] Chain operations into compound expressions

### 12.6 Deobfuscation (Basic)
- [ ] Rename single-letter identifiers using context heuristics
- [ ] Inline trivial getters/setters
- [ ] Constant propagation (replace `const v0, 0` + `if-eqz` with `if (!...)`)

### 12.7 Output Formatting
- [ ] Proper indentation
- [ ] Type declarations for variables
- [ ] Package imports
- [ ] Method signatures with full types
- [ ] Javadoc comments from debug info

### 12.8 Public API
- [ ] `decompile_method(dex_ctx*, int method_idx) → char*`
- [ ] `decompile_class(dex_ctx*, int class_idx) → char*`
- [ ] `decompile_to_java(dex_ctx*, const char *out_dir)`

## Verification

```bash
# Decompile a method
./agent-x decompile_method classes.dex 42

# Compare with JADX output
jadx -d /tmp/jadx_out test.apk
cat /tmp/jadx_out/com/example/MainActivity.java
./agent-x decompile_class classes.dex 0

# Manual review: output should be functionally equivalent Java
```
