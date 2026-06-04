# Agent X — Architecture & Refactoring Manifest

Agent X is designed to be an ultra-lightweight, zero-dependency, and extremely fast AI autonomous agent. To scale its features (such as custom binary parsing and APKTool capabilities) without bloat, it must adhere to a strict modular design. 

This manifest outlines the refactoring guidelines to split the code into small, feature-wise, reusable, size-optimized, and low-RAM-consuming components.

---

## 1. Feature-Wise File System Layout

The codebase must be reorganized into modular feature directories under `src/` and `include/`. No monolithic files are allowed.

```
agent-x/
├── include/                 # Header declarations
│   ├── core.h               # Core agent loop, structures, and config
│   ├── net.h                # HTTP client and Telegram bot
│   ├── tools.h              # Tool dispatch and registration definitions
│   ├── formats.h            # ZIP, AXML, ARSC, DEX, and ELF parser APIs
│   └── crypto.h             # Hashes (SHA1, SHA256) and signatures
│
├── src/
│   ├── core/                # Core agent orchestrator
│   │   ├── main.c           # CLI parsing and daemon startup
│   │   ├── agent.c          # AI agent execution loop
│   │   └── logger.c         # Structured JSON logging
│   │
│   ├── net/                 # Networking interfaces
│   │   ├── http.c           # Free Pollinations API / HTTP integration
│   │   └── telegram.c       # Long-polling Telegram daemon
│   │
│   ├── tools/               # Agent tools (POSIX wrapper scripts & internal functions)
│   │   ├── tool_dispatch.c  # Main tool registration and parsing
│   │   ├── tool_fs.c        # File read, write, delete, search functions
│   │   ├── tool_sys.c       # Subprocess running and terminal commands
│   │   └── tool_net.c       # Curl downloads and external integration
│   │
│   ├── formats/             # Binary format parsing/writing
│   │   ├── format_zip.c     # ZIP operations (wrapping miniz)
│   │   ├── format_axml.c    # Android Binary XML parser and encoder
│   │   ├── format_arsc.c    # resource.arsc parser and generator
│   │   ├── format_dex.c     # DEX parser and Smali decompiler
│   │   └── format_elf.c     # ELF parser (native .so symbol dumper)
│   │
│   └── crypto/              # Minimal cryptographic implementations
│       ├── hash.c           # MD5, SHA-1, SHA-256
│       └── sign.c           # JAR (v1) and APK (v2) signature block generators
```

---

## 2. The Small File Rule (Max 250 LOC)

To prevent code complexity and ensure clean reviews, files must remain small and tightly focused.
- **Rule**: No source file (`.c`) should exceed **250 lines of code** (excluding header files and comments).
- **Single Responsibility Principle**: A file must do one thing. For example, do not mix ZIP directory alignment with file extraction. Keep them in separate translation units.
- **Header Isolation**: Public functions must be documented and declared in `include/`, while internal helper functions must be marked `static` and kept in the `.c` file.

---

## 3. Designing for Reusability

All format decoders and helper utilities must be **decoupled** from the agent's main state.
- **No Global State**: Variables like current directory, agent config, or JSON state must not be globally accessed. Always pass context pointers (e.g. `axml_ctx*` or `dex_ctx*`).
- **Encapsulated Memory**: Every module must follow a consistent Lifecycle Pattern:
  ```c
  // Initialization & Parsing
  module_ctx* module_parse(const uint8_t* data, size_t size);
  
  // Logical operations (read-only)
  const char* module_get_property(module_ctx* ctx, int index);
  
  // Serialization (rebuild)
  uint8_t*    module_serialize(module_ctx* ctx, size_t* out_size);
  
  // Cleanup
  void        module_free(module_ctx* ctx);
  ```

---

## 4. RAM Footprint Optimization (Magically Low RSS)

To keep RAM usage under **500 KB** (even when processing large APKs/DEX files), we employ the following memory management techniques:

### 4.1. Memory Mapping (`mmap`)
Do not load target binary files (like APKs or `.dex` files) into heap memory using `malloc` + `fread`. 
* Use `mmap()` (POSIX) to map files directly into the virtual address space in read-only mode.
* The OS handles page demand caching, keeping the Resident Set Size (RSS) close to zero.

### 4.2. Zero-Copy Parsing
Instead of creating heap-allocated objects for metadata, parse structures **in-place** directly from the `mmap` pointer.
* Use direct byte-offset casting to structures.
* Store pointers referencing string locations inside the mapped file rather than allocating new strings.

### 4.3. Streaming and Iterators
For heavy tables (like the DEX String Table or resources.arsc Key table), do not build loaded lookup tables.
* Implement **iterators** that scan the tables sequentially or perform binary searches on sorted index arrays inside the mapped byte buffer.

### 4.4. Arena Allocators
For tools that require temporary allocations (e.g., JSON processing, Smali generation):
* Implement a simple thread-local Arena Allocator.
* Allocate a single small block of memory (e.g., 64 KB) at startup.
* Sub-allocate inside the arena with simple pointer increments.
* Free the entire block at once at the end of the action, completely eliminating heap fragmentation and GC overhead.

---

## 5. Binary Size Optimization (Low Executable Target)

The static binary must compile to under **100 KB** for CLI/Pico mode.

### 5.1. Compiler Optimization Flags
Use the following compiler flags inside the `Makefile` or `build.zig` to minimize executable size:
```make
# Size-optimized compilation
CFLAGS += -Oz                      # Optimize for size aggressively (closer to machine instructions)
CFLAGS += -ffunction-sections      # Split functions into their own sections
CFLAGS += -fdata-sections          # Split global variables into their own sections
CFLAGS += -flto                    # Enable Link Time Optimization (cross-module tree shaking)

# Linker dead-code stripping
LDFLAGS += -Wl,--gc-sections       # Garbage collect unused sections (strips dead code)
LDFLAGS += -Wl,--strip-all         # Strip all debugging symbols and symbol tables
```

### 5.2. Avoiding Runtime Bloat
- **Standard Library Formatting**: Avoid heavy `printf`/`sprintf` calls with complex format strings when writing small utilities. Utilize simple string concatenations or custom write-based micro-formatters.
- **Dynamic Allocations**: Avoid linking against bloated custom malloc implementations. Stick to the basic POSIX system allocator or our custom Arena.

---

## 6. Development Workflow, Memory Focus & Versioning

To ensure codebase integrity, resource efficiency, and trackable milestones:

### 6.1. Memory-First Focus
Every single code change, refactoring step, or feature implementation **must focus on memory usage** as the highest priority:
* Run memory leak checks (e.g. via Valgrind or static analysis tools) with every build.
* Always favor zero-copy pointers, stack buffers, and arena-managed blocks over standard heap allocation.

### 6.2. Git & Semantic Versioning Rules
* **Track with Git**: All changes must be cleanly staged and committed to git.
* **Changelog Updates**: Every release and major update must be documented in [CHANGELOG.md](file:///home/grandpa/me/code/zig/agent-x/CHANGELOG.md) following Keep a Changelog standards.
* **Semantic Versioning**: Adhere strictly to semantic versioning (`x.y.z` format):
  * **Major (X.y.z)**: Incompatible architectural API rewrites.
  * **Minor (x.Y.z)**: New features added in a backward-compatible manner.
  * **Patch (x.y.Z)**: Backward-compatible bug fixes or documentation updates.
* **Release Tags**: Every commit that updates the version inside the changelog must be tagged in git (e.g. `git tag v1.2.1`).

---

## 7. DEX->Smali Baksmali Parity (In Progress)

The native DEX->Smali disassembler (`src/formats/dex/format_dex_smali.c`) is being aligned to produce output byte-identical to the official baksmali.jar tool. Current status: **23/113 classes identical**.

### Completed
- Section comments (`# static fields`, `# instance fields`, `# direct methods`, `# virtual methods`, `# interfaces`)
- `constructor` keyword for `<clinit>`/`<init>` methods
- Parameter register naming: `p0-N` for params, `v0-N` for locals (params at END of register range)
- Rotating static buffer for multi-register instructions (prevents overwrite)
- `0x%x` format for integer literals
- Offset-based branch labels (`:cond_xx`, `:goto_xx`)
- Blank lines between instructions (suppressed before `.end method`)
- Class-level annotation parsing (`@TargetApi`, `@Retention`, `@Override`)
- `.enum` prefix for enum-typed annotation values
- `# interfaces` section with proper uint16 type_list parsing
- `annotations_off` parsed from class_def_item (offset 20 in class_def_item)

### Remaining (for full byte-identical output)
1. **Static field initializers** - parse `static_values_off` (offset 28 in class_def_item) and `encoded_array_item` to output `= 0x...` for static final fields
2. **Annotation element indent** - class-level annotations need `.annotation` at col 0, elements at col 4, `.end annotation` at col 0
3. **Blank lines between fields** - baksmali separates consecutive field entries with blank lines
4. **Duplicate labels** - when multiple branch instructions target the same offset, baksmali emits both `:cond_N` and `:goto_N` labels
5. **Array annotation values** - `write_encoded_annotation` needs recursive encoded_value parsing for array elements (currently outputs empty `{ }` for non-empty arrays)
6. **Method-level annotations** - need 4-space indent context (currently class-level only)
7. **Parameter annotations** - `params_sz` in `annotations_directory_item` is ignored

### Reference
- Test APK: `apk/Current Activity_1.5.5_APKPure.apk` (113 classes)
- Compare: `diff -rq /tmp/native_smali /tmp/baksmali_smali`
- Official baksmali: `java -jar apk/baksmali.jar d apk/classes.dex -o /tmp/baksmali_smali`
- Native: `./agent-x tool read_dex path=apk/classes.dex out_dir=/tmp/native_smali`

