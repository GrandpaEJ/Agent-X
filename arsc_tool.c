#include <stdio.h>
#include <string.h>
#include "include/formats.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: ./arsc_tool dump <arsc> <out.toml>\n");
        printf("       ./arsc_tool compile <arsc> <in.toml> <out.arsc>\n");
        return 1;
    }

    if (strcmp(argv[1], "dump") == 0) {
        if (arsc_dump_toml(argv[2], argv[3]) == 0) {
            printf("Dumped successfully to %s\n", argv[3]);
            return 0;
        } else {
            printf("Failed to dump\n");
            return 1;
        }
    } else if (strcmp(argv[1], "compile") == 0 && argc >= 5) {
        if (arsc_compile_toml(argv[2], argv[3], argv[4]) == 0) {
            printf("Compiled successfully to %s\n", argv[4]);
            return 0;
        } else {
            printf("Failed to compile\n");
            return 1;
        }
    }
    return 1;
}
