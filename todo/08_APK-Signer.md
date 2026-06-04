# TODO 08: APK Signer

**Files**: `src/sign.c`, `include/sign.h`, `src/hash.c`, `include/hash.h`  
**Depends on**: 01 (ZIP — for repacking)  
**Estimated LOC**: < 250 LOC per file (split dynamically into modular feature-wise files)  

## Objective

Sign APK files with APK Signature Scheme v1 (JAR signing) and v2 (block-based). This is required for repackaged APKs to be installable on Android.

## Tasks

### Core Architecture & Memory Constraints (Strict)
- [ ] Enforce the **Small File Rule**: Split this module into files of under **250 lines of code** (e.g. separate read/write or parser/serializer).
- [ ] Use **Memory Mapping (`mmap`)** for file loading to prevent heap allocations and keep RSS under 500 KB.
- [ ] Implement **Zero-Copy Parsing**: Reference data pointers directly from the `mmap` space instead of allocating heap memory.
- [ ] Maintain the strict **Lifecycle API** to prevent memory leaks: `*_parse()`, `*_serialize()`, and `*_free()`.
- [ ] Allocate temporary compiler objects (e.g. AST nodes, tokens, JSON structures) using a thread-local **Arena Allocator**.


### 8.1 Hash Primitives (src/hash.c)
- [ ] SHA-1 implementation (FIPS 180-4)
- [ ] SHA-256 implementation
- [ ] Adler-32 (for DEX checksum)
- [ ] Base64 encoding (for MANIFEST.MF)

### 8.2 Key Generation
- [ ] `sign_generate_key(const char *keystore, const char *alias, const char *password)`
- [ ] Generate RSA 2048-bit key pair
- [ ] Generate self-signed X.509 certificate
- [ ] Store in PKCS12 keystore format

### 8.3 APK Signature Scheme v1 (JAR Signing)
- [ ] Read all files in APK (excluding META-INF/)
- [ ] For each file: compute SHA-256 digest
- [ ] Write `META-INF/MANIFEST.MF`:
  ```
  Manifest-Version: 1.0
  Created-By: Agent-X
  
  Name: classes.dex
  SHA-256-Digest: <base64>
  ```
- [ ] Write `META-INF/CERT.SF`:
  - SHA-256 of MANIFEST.MF
  - SHA-256 of each MANIFEST.MF entry
- [ ] Write `META-INF/CERT.RSA`:
  - PKCS7 signed data of CERT.SF (RSA + SHA-256)

### 8.4 APK Signature Scheme v2
- [ ] Split APK into 1 MB chunks (before signing block)
- [ ] Compute digests for: APK contents, central directory, EOCD using 1 MB chunks
- [ ] Build signing block:
  - Block ID `0x7109871a`: signed data (digests + certificates + attributes)
  - Sign signed data with private key and format the RSA signature block
  - Construct the ID-value signature block (magic `APK Sig Block 42` and Block Size prefixes/suffixes)
- [ ] Insert signing block between file data and central directory (CD)
- [ ] Update EOCD offset and central directory headers

### 8.5 Public API
- [ ] `sign_v1(const char *apk_path, const char *key_path, const char *alias, const char *password) → int`
- [ ] `sign_v2(const char *apk_path, const char *key_path, const char *alias, const char *password) → int`
- [ ] `sign_apk(const char *apk_path) → int` (auto-generate key if none exists)

## Verification

```bash
# Sign an APK
./agent-x sign_apk test_unsigned.apk

# Verify signature
apksigner verify test_unsigned.apk
echo $?  # should be 0

# Or use jarsigner
jarsigner -verify test_unsigned.apk
echo $?  # should be 0
```
