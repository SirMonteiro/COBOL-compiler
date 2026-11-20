#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>
#include <stddef.h>
#include "read.h"

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
    TOKEN_COLON,        // ':'
    TOKEN_PICTURE,      // PIC/PICTURE string (e.g. S9(05)V99, -ZZZ9.99)
    TOKEN_PLUS,         // '+'
    TOKEN_MINUS,        // '-'
    TOKEN_STAR,         // '*'
    TOKEN_SLASH,        // '/'
    TOKEN_POWER,        // '**'
    TOKEN_EQ,           // '='
    TOKEN_NE,           // '<>'
    TOKEN_LT,           // '<'
    TOKEN_LE,           // '<='
    TOKEN_GT,           // '>'
    TOKEN_GE,           // '>='
    TOKEN_UNKNOWN,      // Error token
    TOKEN_EOF
} TOKENTYPE;

typedef struct {
    TOKENTYPE type;
    char lexeme[256];
    int seq_num;       // Sequence number from cols 1-6 (or physical line)
    int column;        // Starting column (8 to 72)
    COBOL_AREA area;   // AREA_A (8-11) or AREA_B (12-72)
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

extern int current_seq_num;
extern int current_col;
extern int token_start_col;

const char *area_to_string(COBOL_AREA area);
const char *token_type_to_string(TOKENTYPE type);

void lexer_scan_cobol_line(const char *area_ab_slice, int seq_num, FILE *out);
void lexer_scan_cobol_line_at(const char *area_ab_slice, int seq_num, int start_col, FILE *out);
void emit_token(TOKENTYPE type, const char *lexeme, int start_col);
void emit_lexical_error(const char *msg, const char *lexeme, int start_col);
void lexer_set_output_file(FILE *out);
void lexer_set_token_list(TokenList *list);
void lexer_reset(void);
void lexer_reset_errors(void);
int  lexer_error_count(void);

/* TokenList helpers */
void token_list_init(TokenList *list);
void token_list_push(TokenList *list, const Token *tok);
void token_list_free(TokenList *list);

/* Line-stream interface handling '-' continuation lines (strings and split tokens) */
void lexer_begin_file(void);
void lexer_process_line(const COBOL_LINE *line, FILE *out);
void lexer_end_file(FILE *out);

/* High-level file lexing with optional COPY copybook expansion */
bool lex_cobol_file(const char *input_path, const OPTS *opts, FILE *token_out,
                    TokenList *out_tokens, const char *const *copy_dirs, int n_copy_dirs,
                    bool expand_copy);

#endif /* LEXER_H */
