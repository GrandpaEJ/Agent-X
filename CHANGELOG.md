# Changelog

All notable changes to the Agent X project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Versioning Rule:**
- `1.x.x` : [Lts] (Long Term Support) releases
- `x.n.x` : [new feat]
- `x.x.n` : [fixed]

---

## [0.4.0] - 2026-06-13

### Added
- Initial Release of the Smali to DEX assembler (`smali_assemble` tool) with full round-trip parity.
- 3-pass contiguous annotation writing (`annotation_item`, `annotation_set_item`, `annotations_directory_item`) ensuring perfect Android Dalvik/ART parser correctness.
- Support for complex static field initializer types and annotation parsing.
- Hash table-based pool lookups for string, type, method, and field pools.

### Fixed
- Fixed memory corruption bugs related to uninitialized `smali_field_def_t`.
- Resolved `List too large for field_annotations list` Android runtime verification crashes in generated DEX files.
- Fixed `encoded_value` size encoding for string, type, and enum references.
- Fixed string pool inflation, null terminators, and string pool bloat issues.
- Fixed duplicate opcodes (e.g., `0x90` collision) and unified instruction parsing logic.

---

## [0.3.1] - 2026-06-01

### Changed
- Upgraded the 15 microstep todo tasks inside the `todo/` directory to align with memory mapping (`mmap`), zero-copy parsing, and strict `< 250 LOC` file limits.
- Injected specific specifications from `apktool_implementation_plan.md` (zipalign padding math, AXML length prefixes, ULEB128 decoding, parameter register mapping, and APK Signing Block formats) into the microstep files.

---

## [0.3.0] - 2026-06-01

### Added
- Integrated reverse engineering dynamic skills in `skills/re/` directory (`re_extract_apk`, `re_search_strings`, `re_analyze_elf`, `re_sign_apk`, and `re_zipalign`).
- Added recursive scanning and lookup in `src/tools.c` to support hierarchical grouping of dynamic agent skills inside subdirectories (e.g. `skills/re/`).

### Changed
- Refactored `execute_dynamic_skill` and `tools_get_definitions` in `src/tools.c` to resolve subfolders safely and prevent directory traversal.

---

## [0.2.1] - 2026-06-01

### Changed
- Added strict memory-first coding guidelines and semantic version tracking rule definitions to [AGENT.md](file:///home/grandpa/me/code/zig/agent-x/AGENT.md).

---

## [0.2.0] - 2026-06-01


### Added
- Created comprehensive architecture and engineering plan for a custom, zero-dependency APKTool clone (`apktool_implementation_plan.md`), detailing binary AXML, resources.arsc, DEX disassembly/assembly, and v2 APK signatures.

### Changed
- Refactored `AGENT.md` architectural manifest to enforce strict guidelines:
  - **Memory Limits**: Mandatory memory mapping (`mmap`), zero-copy parsing, streaming iterators, and thread-local Arena Allocators to keep RAM footprint < 500 KB.
  - **Code Quality**: Strict file limit of max 250 Lines of Code (LOC) and feature-wise directory layout (`core/`, `net/`, `tools/`, `formats/`, `crypto/`).
  - **Size Optimization**: Compiler configurations targeting < 100 KB binaries via aggressive `-Oz`, Link-Time Optimization (LTO), and section garbage collection.

---

## [0.1.0] - 2026-05-15

### Added
- Integrated system, network, git, text, and Android/Termux skills (69 scripts/schemas) into the dynamic skill engine.

### Removed
- Cleaned up redundant duplicate/generated math and service checking tools to minimize build footprint.

---

## [0.0.0] - 2026-05-01

### Added
- Initial release of Agent X (compiled rewrite in pure C11).
- Free AI Backend Integration (no API key required).
- Production Pro features: File-based keyword memory database (RAG), session isolation, dynamic context/token optimization, POSIX daemonization, and structured JSON logging.
