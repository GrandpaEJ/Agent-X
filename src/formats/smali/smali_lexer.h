#ifndef SMALI_LEXER_H
#define SMALI_LEXER_H

#include <stdint.h>

char *smali_next_token(char **p);
char *smali_parse_string_literal(char **p);
uint32_t smali_parse_register(const char *tok);

#endif
