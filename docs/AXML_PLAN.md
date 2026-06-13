# Native AXML (Binary XML) Encoder Plan

## Goal
Implement a completely zero-dependency native C11 assembler that can convert Plaintext XML (e.g., `AndroidManifest.xml`) back into Android Binary XML (AXML), mirroring the behavior of `apktool b`.

## The Challenge
Android Binary XML (AXML) is not a simple string format. It is a highly structured binary tree consisting of:
1. **Header (0x0003)**: Magic number and total file size.
2. **String Pool Chunk (0x0001)**: A sorted/unsorted list of all unique strings used in the XML (tags, namespaces, attributes, and values). Strings can be UTF-8 or UTF-16.
3. **Resource Map Chunk (0x0180)**: An array mapping specific string indices (from the String Pool) to official Android framework Resource IDs (e.g., `android:versionCode` = `0x0101021b`).
4. **Namespace Start/End Chunks (0x0100 / 0x0101)**: Define XML namespace prefixes and URIs.
5. **Element Start/End Chunks (0x0102 / 0x0103)**: Represent the actual XML tags.
6. **Attributes**: Encoded within the Element Start chunk. Each attribute has:
   - Namespace string index
   - Name string index
   - Value string index (if string)
   - Typed value (e.g., Integer, Boolean, Resource Reference `@ref/0x...`)

## Implementation Phases

### Phase 1: Plaintext XML Parser
Since Agent-X is zero-dependency, we cannot use `libxml2` or `tinyxml2`. We will implement a minimal, fast, recursive-descent XML lexer/parser (`axml_xml_parser.c`) that:
- Tokenizes `<tag>`, `</tag>`, `/>`, and `attribute="value"`.
- Builds an in-memory Abstract Syntax Tree (AST) of the XML document.

### Phase 2: String Pool and Resource Mapping
- Scan the entire XML AST to collect all unique namespaces, tag names, attribute names, and string values.
- Build the **String Pool** structure.
- Map recognized `android:` attributes (e.g., `android:name`, `android:icon`) to their corresponding Android framework Resource IDs. We will need a lightweight lookup table for common Android attributes.

### Phase 3: Binary Chunk Generation
- Serialize the AST into the AXML binary format.
- Generate `START_NAMESPACE` and `END_NAMESPACE` chunks.
- Convert XML nodes to `START_ELEMENT` chunks.
- Correctly parse encoded attribute types (e.g., recognizing `true`/`false` as `TYPE_INT_BOOLEAN`, integers as `TYPE_INT_DEC`, and `@ref/0x...` as `TYPE_REFERENCE`).
- Compute and inject all block sizes and offsets.

## File Structure
- `src/android/axml/axml_assemble.c`: Entry point and orchestrator.
- `src/android/axml/axml_xml_parser.c`: The plaintext XML lexer and AST builder.
- `src/android/axml/axml_encoder.c`: The binary serializer that writes AXML chunks.
- `src/android/axml/axml_res_map.h`: Lookup table mapping strings to `0x0101....` resource IDs.
