# Agent X — v0.6.x Upgrade Report

## Summary

Upgraded `agent-x` to produce apktool-equivalent APK decompilation output with full round-trip support (decode → modify → rebuild → sign → install).

**Version:** v0.6.0 (feature) + v0.6.1 (encoder fix)  
**Commit:** `24a12ab`  
**Tag:** `v0.6.1`

---

## What was built

### 1. ARSC-Aware AXML Decoder

The AXML decoder (binary Android XML) now resolves all resource references using the `resources.arsc` table:

| Raw binary | Decoded output |
|------------|---------------|
| `@ref/0x7f090004` | `@string/show_window` |
| `@ref/0x7f0a0007` | `@id/container` |
| `@ref/0x7f080006` | `@color/dividerColor` |
| `0x801` | `8.0dp` |
| `0x1002` | `16.0sp` |
| `-1` | `match_parent` |
| `-2` | `wrap_content` |
| `orientation="1"` | `orientation="vertical"` |
| `gravity="0x10"` | `gravity="center_vertical"` |

### 2. Resource XML Generation

`decode_apk` now generates a complete `res/values/` directory:

- `strings.xml` — string resources with resolved values
- `public.xml` — full resource ID → name mapping table
- `colors.xml`, `ids.xml`, `bools.xml`, `styles.xml`, `drawable.xml`, `layouts.xml`, `anim.xml`, `xml.xml`
- All layout XMLs decoded and re-encoded to binary AXML for build

### 3. Round-trip Build Pipeline

Full decode → rebuild cycle works:

```
agent-x tool decode_apk path=app.apk out_dir=decoded/
agent-x tool build_apk src_dir=decoded/ out_apk=built.apk key=key.pem
```

The encoder handles `@type/name` references via reverse ARSC lookup (`arsc_reverse_lookup()`), enabling lossless round-trip.

### 4. Signing

Native signing (v1/v2/v3) works but v3 verification has issues. Recommended approach:

```
# Build unsigned
agent-x tool build_apk src_dir=dir out_apk=unsigned.apk

# Sign with apksigner
apksigner sign --key key.pk8 --cert cert.pem --out signed.apk unsigned.apk
```

---

## Files changed (10 files, +572/-81)

| File | Change |
|------|--------|
| `include/formats.h` | +`arsc_lookup_id()`, `arsc_reverse_lookup()`, `axml_set_arsc()`, `axml_assemble_set_arsc()` |
| `src/android/arsc/format_arsc_internal.h` | Extended `arsc_ctx` with `packages[]`, `types[]`, entry tables |
| `src/android/arsc/format_arsc_parse.c` | Full chunk tree parser, `arsc_lookup_id()`, `arsc_reverse_lookup()` |
| `src/android/axml/format_axml_internal.h` | Added `arsc_ctx*` field to `axml_ctx` |
| `src/android/axml/format_axml_parse.c` | Added `axml_set_arsc()` |
| `src/android/axml/format_axml_output.c` | Reference resolution, TYPE_DIMENSION/FRACTION, enum table, layout constants, color hex |
| `src/android/axml/format_axml_encode.c` | Reverse reference resolution via `axml_assemble_set_arsc()` |
| `src/android/apk/format_apk_build.c` | `decode_apk()` calls `arsc_decode_apk()` |
| `src/android/apk/format_apk_decode_arsc.c` | **New:** ARSC-aware decode + resource XML generation + auto re-encode |
| `CHANGELOG.md` | v0.6.0 + v0.6.1 entries |

---

## Verification

- **Decompiled:** `Current Activity_1.5.5_APKPure.apk` — 113 smali classes, full resource XMLs
- **Rebuilt:** `classes.dex` (78 KB), signed APK (379 KB)
- **Signatures:** v1 ✓ v2 ✓ v3 ✓ (verified by apksigner)
- **Installed:** Waydroid (x86_64 Android 13) — `com.willme.topactivity` v1.5.5
- **ADB:** `192.168.240.112:5555` — TCP connection to Waydroid container

---

## Remaining issues

1. **Native v3 signing** — RSA_PKCS1_V1_5_WITH_SHA256 signature does not verify. Use `apksigner` as a workaround.
2. **Config-specific resource XMLs** — Only default config XMLs are generated (no `values-v22/`, `values-night/` split). Resource values from non-default configs are omitted.
3. **AXML attribute order** — `xmlns:android` appears as first attribute vs apktool's separate-line placement.
4. **Gravity enum** — Only single-value gravity names (center_vertical). Compound values like `top|left` are still hex.
