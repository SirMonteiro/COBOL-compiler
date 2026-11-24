#ifndef LEXER_H
#define LEXER_H

#include <cstdio>
#include <string>
#include <vector>

#include "read.h"

enum COBOL_AREA {
    AREA_UNKNOWN = 0,
    AREA_A, /* Columns 8-11 (Divisions, Sections, Paragraphs, 01/77 levels) */
    AREA_B  /* Columns 12-72 (Statements, sentences, clauses) */
};

enum TOKENTYPE {
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_LITERAL_NUMERIC,
    TOKEN_LITERAL_ALPHANUMERIC,
    TOKEN_PERIOD,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COLON,
    TOKEN_PICTURE,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_POWER,
    TOKEN_EQ,
    TOKEN_NE,
    TOKEN_LT,
    TOKEN_LE,
    TOKEN_GT,
    TOKEN_GE,
    TOKEN_UNKNOWN,
    TOKEN_EOF
};

struct Token {
    TOKENTYPE type = TOKEN_UNKNOWN;
    std::string lexeme;
    int seq_num = 0;
    int column = 8;
    COBOL_AREA area = AREA_UNKNOWN;
};

typedef std::vector<Token> TokenList;

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

/* Line-stream interface handling '-' continuation lines (strings and split tokens) */
void lexer_begin_file(void);
void lexer_process_line(const COBOL_LINE *line, FILE *out);
void lexer_end_file(FILE *out);

/* High-level file lexing with optional COPY copybook expansion */
bool lex_cobol_file(const std::string &input_path, const OPTS &opts, FILE *token_out,
                    TokenList *out_tokens, const std::vector<std::string> &copy_dirs,
                    bool expand_copy);

#endif /* LEXER_H */
