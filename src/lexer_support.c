#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static FILE *lexer_out = NULL;
static int lexer_errors = 0;

int lexer_error_count(void) {
    return lexer_errors;
}

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
        case TOKEN_COLON:             return "TOKEN_COLON";
        case TOKEN_PICTURE:           return "TOKEN_PICTURE";
        case TOKEN_PLUS:              return "TOKEN_PLUS";
        case TOKEN_MINUS:             return "TOKEN_MINUS";
        case TOKEN_STAR:              return "TOKEN_STAR";
        case TOKEN_SLASH:             return "TOKEN_SLASH";
        case TOKEN_POWER:             return "TOKEN_POWER";
        case TOKEN_EQ:                return "TOKEN_EQ";
        case TOKEN_NE:                return "TOKEN_NE";
        case TOKEN_LT:                return "TOKEN_LT";
        case TOKEN_LE:                return "TOKEN_LE";
        case TOKEN_GT:                return "TOKEN_GT";
        case TOKEN_GE:                return "TOKEN_GE";
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
    lexer_errors++;
    /* Flush saída de token primeiro para não misturar stdout/stderr */
    if (lexer_out) fflush(lexer_out);
    fprintf(stderr,
            "[E] Lexical Error (Seq %06d, Col %d): %s -> \"%s\"\n",
            current_seq_num, start_col, msg, lexeme);
    fflush(stderr);
    emit_token(TOKEN_UNKNOWN, lexeme, start_col);
}
