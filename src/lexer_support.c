#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static FILE *lexer_out = NULL;

void lexer_set_output_file(FILE *out) {
    lexer_out = out ? out : stdout;
}

const char *token_type_to_string(TOKENTYPE type) {
    switch (type) {
        case TOKEN_KEYWORD:              return "TOKEN_KEYWORD";
        case TOKEN_IDENTIFIER:           return "TOKEN_IDENTIFIER";
        case TOKEN_LITERAL_NUMERIC:      return "TOKEN_LITERAL_NUMERIC";
        case TOKEN_LITERAL_ALPHANUMERIC: return "TOKEN_LITERAL_ALPHANUMERIC";
        case TOKEN_PERIOD:               return "TOKEN_PERIOD";
        case TOKEN_COMMA:                return "TOKEN_COMMA";
        case TOKEN_SEMICOLON:            return "TOKEN_SEMICOLON";
        case TOKEN_LPAREN:               return "TOKEN_LPAREN";
        case TOKEN_RPAREN:               return "TOKEN_RPAREN";
        case TOKEN_UNKNOWN:              return "TOKEN_UNKNOWN";
        case TOKEN_EOF:                  return "TOKEN_EOF";
        default:                         return "TOKEN_INVALID";
    }
}

const char *area_to_string(COBOL_AREA area) {
    switch (area) {
        case AREA_A: return "AREA A";
        case AREA_B: return "AREA B";
        default:     return "UNK   ";
    }
}

void emit_token(TOKENTYPE type, const char *lexeme, int start_col) {
    COBOL_AREA area = (start_col >= 8 && start_col <= 11) ? AREA_A : AREA_B;

    /* Normalize keywords and identifiers to uppercase */
    char normalized[256];
    strncpy(normalized, lexeme, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';

    if (type == TOKEN_KEYWORD || type == TOKEN_IDENTIFIER) {
        for (int i = 0; normalized[i]; i++) {
            normalized[i] = (char)toupper((unsigned char)normalized[i]);
        }
    }

    fprintf(lexer_out,
            "[SEQ %06d | COL %02d (%s)] %-26s : %s\n",
            current_seq_num,
            start_col,
            area_to_string(area),
            token_type_to_string(type),
            normalized);
}

void emit_lexical_error(const char *msg, const char *lexeme, int start_col) {
    fprintf(stderr,
            "[E] Lexical Error (Seq %06d, Col %d): %s -> \"%s\"\n",
            current_seq_num, start_col, msg, lexeme);
    emit_token(TOKEN_UNKNOWN, lexeme, start_col);
}
