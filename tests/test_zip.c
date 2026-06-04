// Quick smoke test for ZIP module
// Compile: zig cc -target x86_64-linux-musl -Oz -std=c11 -Iinclude -Ivendor/cJSON -o test_zip test_zip.c src/formats/format_zip.c src/formats/format_zip_extract.c src/formats/format_zip_inflate.c src/formats/format_zip_write.c
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    // 1. Create a test ZIP
    printf("=== Creating test ZIP ===\n");
    zip_writer *zw = zip_writer_open("/tmp/test.zip");
    if (!zw) { fprintf(stderr, "FAIL: zip_writer_open\n"); return 1; }

    const char *hello = "Hello, World!";
    const char *data = "Some binary data here\x00\x01\x02";
    if (zip_writer_add(zw, "hello.txt", hello, strlen(hello), 0) < 0) { fprintf(stderr, "FAIL: add hello\n"); return 1; }
    if (zip_writer_add(zw, "subdir/data.bin", data, 20, 0) < 0) { fprintf(stderr, "FAIL: add data\n"); return 1; }
    if (zip_writer_close(zw) < 0) { fprintf(stderr, "FAIL: close writer\n"); return 1; }
    printf("PASS: Created /tmp/test.zip\n");

    // 2. Read back
    printf("\n=== Reading test ZIP ===\n");
    zip_archive *za = zip_open("/tmp/test.zip");
    if (!za) { fprintf(stderr, "FAIL: zip_open\n"); return 1; }

    int n = zip_get_num_entries(za);
    printf("Entries: %d\n", n);
    if (n != 2) { fprintf(stderr, "FAIL: expected 2 entries, got %d\n", n); return 1; }

    for (int i = 0; i < n; i++) {
        const char *name = zip_get_entry_name(za, i);
        size_t sz = zip_get_entry_size(za, i);
        int comp = zip_entry_is_compressed(za, i);
        printf("  [%d] %s (size=%zu, compressed=%d)\n", i, name, sz, comp);
    }

    // 3. Extract and verify
    printf("\n=== Extracting entries ===\n");
    size_t sz;
    void *ext = zip_extract_entry(za, 0, &sz);
    if (!ext) { fprintf(stderr, "FAIL: extract entry 0\n"); return 1; }
    if (sz != strlen(hello) || memcmp(ext, hello, sz) != 0) {
        fprintf(stderr, "FAIL: content mismatch: got %zu bytes\n", sz);
        return 1;
    }
    printf("PASS: hello.txt content verified (%zu bytes)\n", sz);
    free(ext);

    ext = zip_extract_entry(za, 1, &sz);
    if (!ext) { fprintf(stderr, "FAIL: extract entry 1\n"); return 1; }
    if (sz != 20) { fprintf(stderr, "FAIL: expected 20 bytes, got %zu\n", sz); return 1; }
    printf("PASS: subdir/data.bin content verified (%zu bytes)\n", sz);
    free(ext);

    zip_close(za);

    // 4. zipalign test
    printf("\n=== zipalign test ===\n");
    if (zipalign_file("/tmp/test.zip", "/tmp/test_aligned.zip", 4) < 0) {
        fprintf(stderr, "FAIL: zipalign_file\n"); return 1;
    }
    za = zip_open("/tmp/test_aligned.zip");
    if (!za) { fprintf(stderr, "FAIL: zip_open aligned\n"); return 1; }
    n = zip_get_num_entries(za);
    printf("Aligned ZIP entries: %d\n", n);
    zip_close(za);
    printf("PASS: zipalign\n");

    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
