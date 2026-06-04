#include "smali_parser.h"
#include <string.h>
#include <stdlib.h>

char *smali_parse_string_literal(char **p) {
    char *start = *p;
    if (*start != '"') return NULL;
    start++;
    char *end = start;
    size_t cap = 256;
    char *str = malloc(cap);
    size_t len = 0;
    while (*end && *end != '"') {
        if (*end == '\\') {
            end++;
            if (*end == 'n') { str[len++] = '\n'; }
            else if (*end == 't') { str[len++] = '\t'; }
            else if (*end == 'r') { str[len++] = '\r'; }
            else if (*end == '"') { str[len++] = '"'; }
            else if (*end == '\\') { str[len++] = '\\'; }
            else if (*end == '\'') { str[len++] = '\''; }
            else { str[len++] = *end; }
        } else {
            str[len++] = *end;
        }
        if (len + 2 >= cap) {
            cap *= 2;
            str = realloc(str, cap);
        }
        end++;
    }
    str[len] = '\0';
    if (*end == '"') end++;
    *p = end;
    return str;
}

char *smali_next_token(char **p) {
    char *start = *p;
    while (*start && (*start == ' ' || *start == '\t' || *start == ',' || *start == '\r')) {
        start++;
    }
    if (!*start || *start == '\n' || *start == '#') {
        *p = start;
        return NULL;
    }
    if (*start == '"') {
        return smali_parse_string_literal(&start);
    }
    if (*start == '{') {
        char *end = start;
        int has_colon = 0;
        while (*end && *end != '}') {
            if (*end == ':') has_colon = 1;
            end++;
        }
        if (*end == '}') {
            end++;
            size_t len = end - start;
            char *tok = malloc(len + 1);
            memcpy(tok, start, len);
            tok[len] = '\0';
            *p = end;
            return tok;
        }
    }
    char *end = start;
    if (*end == ':') {
        end++;
    }
    while (*end && *end != ' ' && *end != '\t' && *end != ',' && *end != '\r' && *end != '\n' && *end != '#') {
        end++;
    }
    size_t len = end - start;
    char *tok = malloc(len + 1);
    memcpy(tok, start, len);
    tok[len] = '\0';
    *p = end;
    return tok;
}

uint32_t smali_parse_register(const char *tok) {
    if (!tok) return 0;
    while (*tok == ' ' || *tok == '\t') tok++;
    if (tok[0] == 'v') return (uint32_t)atoi(tok + 1);
    if (tok[0] == 'p') return (uint32_t)atoi(tok + 1) | 0x80000000;
    return 0;
}
