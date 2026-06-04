#!/bin/bash
if [ ! -f "$ARG_elf_path" ]; then
    echo "{\"error\": \"ELF file not found\"}"
    exit 1
fi
echo "=== ELF Header & File Info ==="
file "$ARG_elf_path"
echo ""
echo "=== Dynamic Dependencies (Shared Libraries) ==="
readelf -d "$ARG_elf_path" 2>/dev/null | grep -i NEEDED || echo "No explicit dynamic library dependencies."
echo ""
echo "=== Symbol Exports (Top 50) ==="
readelf -s "$ARG_elf_path" 2>/dev/null | grep -E " FUNC | OBJECT " | head -n 50 || echo "No functional symbols exported/imported."
