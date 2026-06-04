// Debug inflate - trace code length decoding
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_BITS 15
#define TB_SIZE (1 << MAX_BITS)

typedef struct { int16_t sym; int16_t len; } huff_entry_t;
typedef struct { huff_entry_t table[TB_SIZE]; } huff_t;

static void huff_build(huff_t *h, const int *lens, int count) {
    for (int i = 0; i < TB_SIZE; i++) h->table[i].sym = -1;
    int bl_count[MAX_BITS+1] = {0};
    for (int i = 0; i < count; i++)
        if (lens[i] > 0) bl_count[lens[i]]++;
    int code = 0, next_code[MAX_BITS+1] = {0};
    for (int b = 1; b <= MAX_BITS; b++) {
        code = (code + bl_count[b-1]) << 1;
        next_code[b] = code;
    }
    for (int i = 0; i < count; i++) {
        int len = lens[i];
        if (len == 0) continue;
        int rev = 0, tmp = next_code[len]++;
        for (int b = 0; b < len; b++) {
            rev = (rev << 1) | (tmp & 1);
            tmp >>= 1;
        }
        h->table[rev].sym = i;
        h->table[rev].len = len;
    }
}

int main(void) {
    FILE *fp = fopen("/tmp/compressed.bin", "rb");
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = malloc(sz);
    fread(data, 1, sz, fp);
    fclose(fp);

    const uint8_t *start = data, *in = data;
    size_t in_len = sz;
    int buf = 0, cnt = 0;

    #define RB() ({ \
        if (cnt == 0) { if ((size_t)(in-start) >= in_len) return -1; buf=*in++; cnt=8; } \
        int b = buf&1; buf>>=1; cnt--; b; \
    })
    #define RBITS(n) ({ int v=0; for(int i=0;i<(n);i++) v|=RB()<<i; v; })

    int final = RB(); int type = RBITS(2);
    printf("BFINAL=%d BTYPE=%d\n", final, type);

    int nl = RBITS(5)+257, nd = RBITS(5)+1, nc = RBITS(4)+4;
    if (nc > 19) nc = 19;
    printf("NL=%d ND=%d NC=%d\n", nl, nd, nc);

    static int cl_order[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int cl_lens[19] = {0};
    for (int i = 0; i < nc; i++) cl_lens[cl_order[i]] = RBITS(3);
    printf("CL: "); for(int i=0;i<19;i++) printf("%d ",cl_lens[i]); printf("\n");

    huff_t hc; memset(&hc,0,sizeof(hc));
    huff_build(&hc, cl_lens, 19);

    // Decode code lengths with debug
    int lens[320], li = 0;
    while (li < nl + nd) {
        // Read code from hc
        int code = 0, sym = -1;
        for (int b = 1; b <= MAX_BITS; b++) {
            int bit = RB(); if (bit < 0) break;
            code = (code << 1) | bit;
            if (hc.table[code].len == b) { sym = hc.table[code].sym; break; }
        }
        if (sym < 0) { printf("FAIL at li=%d\n", li); break; }

        if (li < 5) printf("sym=%d ", sym);
        
        if (sym < 16) { lens[li++] = sym; }
        else if (sym == 16) {
            int rep = RBITS(2) + 3;
            int val = li > 0 ? lens[li-1] : 0;
            for (int j = 0; j < rep && li < nl+nd; j++) lens[li++] = val;
        } else if (sym == 17) {
            int rep = RBITS(3) + 3;
            for (int j = 0; j < rep && li < nl+nd; j++) lens[li++] = 0;
        } else {
            int rep = RBITS(7) + 11;
            for (int j = 0; j < rep && li < nl+nd; j++) lens[li++] = 0;
        }
    }
    printf("\nli=%d\n", li);

    // Print first 60 code lengths
    for (int i = 0; i < 60 && i < nl+nd; i++) {
        if (i % 30 == 0) printf("\n[%d-%d]: ", i, i+29);
        printf("%d", lens[i]);
    }
    printf("\n");

    // Now build literal tree
    huff_t hl; memset(&hl,0,sizeof(hl));
    huff_build(&hl, lens, nl);
    printf("HL non-empty entries: ");
    int n = 0;
    for (int i = 0; i < TB_SIZE && n < 5; i++)
        if (hl.table[i].sym >= 0) { printf("[%d]sym=%d,len=%d ", i, hl.table[i].sym, hl.table[i].len); n++; }
    printf("\n");

    // Try decoding first 10 symbols
    printf("Decoded: ");
    for (int k = 0; k < 10; k++) {
        int code = 0, sym = -1;
        for (int b = 1; b <= MAX_BITS; b++) {
            int bit = RB(); if (bit < 0) break;
            code = (code << 1) | bit;
            if (hl.table[code].len == b) { sym = hl.table[code].sym; break; }
        }
        if (sym < 0) { printf("ERR "); break; }
        if (sym < 256) printf("'%c' ", sym);
        else if (sym == 256) printf("END ");
        else printf("L%d ", sym);
    }
    printf("\n");

    free(data);
    return 0;
}
