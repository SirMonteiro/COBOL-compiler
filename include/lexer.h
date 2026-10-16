#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>

typedef enum {
    AREA_UNKNOWN = 0,
    AREA_A, // Columns 8-11 (Divisions, Sections, Paragraphs, 01/77 levels)
    AREA_B  // Columns 12-72 (Statements, sentences, clauses)
} COBOL_AREA;

typedef enum {
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_LITERAL_NUMERIC,
    TOKEN_LITERAL_ALPHANUMERIC,
    TOKEN_PERIOD,       // '.'
    TOKEN_COMMA,        // ','
    TOKEN_SEMICOLON,    // ';'
    TOKEN_LPAREN,       // '('
    TOKEN_RPAREN,       // ')'
    TOKEN_UNKNOWN,      // Error token
    TOKEN_EOF
} TOKENTYPE;

typedef struct {
    TOKENTYPE type;
    char lexeme[256];
    int seq_num;       // Sequence number from cols 1-6
    int column;        // Starting column (8 to 72)
    COBOL_AREA area;   // AREA_A (8-11) or AREA_B (12-72)
} Token;

extern int current_seq_num;
extern int current_col;
extern int token_start_col;

const char *area_to_string(COBOL_AREA area);
const char *token_type_to_string(TOKENTYPE type);

void lexer_scan_cobol_line(const char *area_ab_slice, int seq_num, FILE *out);
void emit_token(TOKENTYPE type, const char *lexeme, int start_col);
void emit_lexical_error(const char *msg, const char *lexeme, int start_col);
void lexer_set_output_file(FILE *out);

#endif /* LEXER_H */
