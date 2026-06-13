# Changelog

All notable changes to the Agent X project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**Versioning Rule:**
- `n.x.x` : [Lts] (Long Term Support) releases
- `x.n.x` : [new feat]
- `x.x.n` : [fixed]

---

## [Unreleased]

---

## [0.5.0] - 2026-06-13

### Added
- **Native APK Signer:** Full implementation of APK Signature Scheme v1, v2, and v3 including chunked hashing, native SHA-256 and RSA-SHA256, PKCS#7/PKCS#8 DER parsing, and dynamic custom certificates.
- **AXML Encoder:** Complete native AXML encoder with 100% AAPT compliance, supporting Resource Maps, namespace URIs, and attribute sorting.
- **Smali Assembler:** Implemented Phase 2 Annotation System (Class, Field, Method, Parameter) and added rigorous validation & error reporting (register limits, type list deduplication).
- **ZipAlign & APK Pipeline:** Lossless native `zipalign` integration and full APK decode/build orchestration logic.
- **Docs & Licenses:** Added `CONTRIBUTING.md` and GNU General Public License v3.0 (`LICENSE`), alongside comprehensive internals documentation for AXML, ARSC, APK Signer, and DEX/Smali.
- **Project Tracking:** Created GitHub issues tracking the remaining native implementation phases (APK Signer, AXML/ARSC Encoders, ZipAlign, Debug Info).

### Changed
- **Architectural Restructure:** Moved source files into a strict semantic layout (`src/core`, `src/net`, `src/tools`) adhering to `AGENTS.md` and the Small File Rule.
- **Android Separation:** Extracted all Android-specific modules (`adb`, `apk`, `axml`, `dex`, `smali`) from `src/formats/` to a dedicated `src/android/` directory.
- **Documentation:** Renamed `AGENT.md` to `AGENTS.md` and moved `REVERSE_ENGINEERING.md` to `docs/RE_ARCHITECTURE.md`.
- **Build System:** Updated `Makefile` to reflect the new directory structure, maintaining full compatibility with `pico` and `nano` targets.
- **Security:** Enabled GitHub branch protection on `main` to prevent force pushes and deletions.

### Fixed
- **Smali:** Corrected MUTF-8 surrogate pair encoding for 4-byte UTF-8 chars.
- **DEX:** Fixed DEX verifier crash and string pool bloat issues.
- **AXML:** Fixed boolean encoding, integer types, and sorted attributes by resource ID.
- **Crypto:** Corrected CERT.RSA signature offset injection length in V1 PKCS#7 template.

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
