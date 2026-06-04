# TODO 11: Agent Tool Integration

**Files**: `src/tools.c`, `include/tools.h`  
**Depends on**: 06 (APK Analyzer)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Register all native RE modules as agent-callable tools so the AI agent can use them autonomously.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 11.1 Tool Definitions
- [ ] Register `analyze_apk` — full APK analysis
- [ ] Register `dump_dex` — DEX disassembly to Smali
- [ ] Register `read_axml` — binary XML decoder
- [ ] Register `dump_arsc` — resource table dump
- [ ] Register `analyze_elf` — ELF binary analysis
- [ ] Register `extract_apk` — full extract (decode all)
- [ ] Register `rebuild_apk` — build + sign
- [ ] Register `sign_apk` — sign existing APK
- [ ] Register `search_strings` — extract strings from any binary
- [ ] Register `disassemble` — native code disassembly (Capstone)

### 11.2 Tool Implementations
- [ ] `execute_analyze_apk(cJSON *args) → char*`
- [ ] `execute_dump_dex(cJSON *args) → char*`
- [ ] `execute_read_axml(cJSON *args) → char*`
- [ ] `execute_dump_arsc(cJSON *args) → char*`
- [ ] `execute_analyze_elf(cJSON *args) → char*`
- [ ] `execute_extract_apk(cJSON *args) → char*`
- [ ] `execute_rebuild_apk(cJSON *args) → char*`
- [ ] `execute_sign_apk(cJSON *args) → char*`
- [ ] `execute_search_strings(cJSON *args) → char*`
- [ ] `execute_disassemble(cJSON *args) → char*`

### 11.3 Agent Skill Definitions
- [ ] Create `skills/analyze_apk.json`
- [ ] Create `skills/dump_dex.json`
- [ ] Create `skills/read_axml.json`
- [ ] Create `skills/dump_arsc.json`
- [ ] Create `skills/analyze_elf.json`
- [ ] Create `skills/extract_apk.json`
- [ ] Create `skills/rebuild_apk.json`
- [ ] Create `skills/sign_apk.json`
- [ ] Create `skills/search_strings.json`

### 11.4 Agent Prompt Enhancement
- [ ] Add RE capabilities to system prompt
- [ ] Include example workflows:
  - "Analyze this APK and find the API endpoints"
  - "Decode this AndroidManifest.xml"
  - "Disassemble this DEX and look for the encryption logic"
  - "What native libraries does this APK use?"

## Verification

```bash
# Test each tool from CLI
./agent-x cli
> analyze_apk path="test.apk"
> dump_dex path="classes.dex" class="Lcom/example/MainActivity;"
> read_axml path="AndroidManifest.xml"
> analyze_elf path="libnative.so"

# Verify tools appear in schema
./agent-x tools_list
# Should list all RE tools
```
