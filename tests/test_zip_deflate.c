// Test deflated ZIP entries with debug output
#include "formats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// Declare inflate directly for testing
void *zip_inflate(const uint8_t *in, size_t in_len, size_t *out_size);

int main(void) {
    // Manually map and inspect the file
    int fd = open("/tmp/test_deflate.zip", O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    uint8_t *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    // Find EOCD
    for (size_t i = st.st_size - 22; i > 0; i--) {
        if (*(uint32_t*)(data + i) == 0x06054b50) {
            printf("EOCD at offset %zu\n", i);
            uint32_t cd_off = *(uint32_t*)(data + i + 16);
            printf("Central dir at %u\n", cd_off);
            // Walk CDFH
            uint32_t off = cd_off;
            uint32_t lh_off = *(uint32_t*)(data + off + 42);
            printf("LFH at %u\n", lh_off);
            // Read LFH
            uint32_t name_len = *(uint16_t*)(data + lh_off + 26);
            uint32_t extra_len = *(uint16_t*)(data + lh_off + 28);
            uint32_t comp_size = *(uint32_t*)(data + lh_off + 18);
            uint32_t uncomp_size = *(uint32_t*)(data + lh_off + 22);
            uint16_t method = *(uint16_t*)(data + lh_off + 8);
            uint32_t data_off = lh_off + 30 + name_len + extra_len;
            printf("Name len: %u, Extra len: %u\n", name_len, extra_len);
            printf("Comp size: %u, Uncomp size: %u\n", comp_size, uncomp_size);
            printf("Method: %u (0=stored, 8=deflated)\n", method);
            printf("Data offset: %u\n", data_off);

            // Save compressed data to inspect
            FILE *fp = fopen("/tmp/compressed.bin", "wb");
            fwrite(data + data_off, 1, comp_size, fp);
            fclose(fp);
            printf("Wrote %u compressed bytes to /tmp/compressed.bin\n", comp_size);

            // Now try our inflater directly
            size_t out_sz;
            void *result = zip_inflate(data + data_off, comp_size, &out_sz);
            if (result) {
                printf("Inflate succeeded: %zu bytes\n", out_sz);
                printf("Content: %.*s\n", (int)out_sz, (char*)result);
                free(result);
            } else {
                printf("Inflate FAILED\n");
            }
            break;
        }
    }

    munmap(data, st.st_size);

    // Now try through the public API
    printf("\n--- Through public API ---\n");
    zip_archive *za = zip_open("/tmp/test_deflate.zip");
    if (!za) { printf("FAIL: zip_open\n"); return 1; }

    printf("Entries: %d\n", zip_get_num_entries(za));
    size_t sz;
    void *ext = zip_extract_entry(za, 0, &sz);
    if (ext) {
        printf("Extracted %zu bytes: %.*s\n", sz, (int)sz, (char*)ext);
        free(ext);
    } else {
        printf("FAIL: extract returned NULL\n");
    }

    zip_close(za);
    return 0;
}
