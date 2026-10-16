%{
#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"

int current_seq_num  = 0;
int current_col      = 8;
int token_start_col  = 8;

#define YY_USER_ACTION \
    token_start_col = current_col; \
    current_col += yyleng;
%}

%option noyywrap
%option nounput
%option noinput
%option caseless
%option never-interactive

/* Regular Expression Definitions (Section 2.2) */
DIGIT       [0-9]
ALNUM       [A-Z0-9]

/* Numeric literals: optional sign, digits, optional INTERNAL decimal point */
NUM_LIT     [+-]?{DIGIT}+(\.{DIGIT}+)?

/* Alphanumeric literals: single or double quoted strings (allowing escaped doubled quotes) */
STR_SINGLE  \'([^\'\n]|\'\')*\'
STR_DOUBLE  \"([^\"\n]|\"\")*\"
UNTERM_STR  (\'([^\'\n]|\'\')*)|(\"([^\"\n]|\"\")*)

/* Valid COBOL Identifier: letters/numbers with optional internal hyphens (e.g. WS-COUNT, 100-PARA) */
VALID_ID    {ALNUM}+(-{ALNUM}+)*

/* Invalid Identifier: starts or ends with hyphen */
INVALID_ID  (-+{ALNUM}[A-Z0-9-]*)|([A-Z0-9-]+-+)

%%

[ \t]+ {
    /* Whitespace advances current_col via YY_USER_ACTION; no token emitted */
}

 /* 1. Reserved Keywords (Section 2.2 + Grammar Divisions/Clauses) */
"IDENTIFICATION"|"ENVIRONMENT"|"DATA"|"PROCEDURE"|"DIVISION"|"SECTION"|"DISPLAY"|"ACCEPT"|"PERFORM"|"STOP"|"RUN"|"IF"|"ELSE"|"MOVE"|"PROGRAM-ID"|"CONFIGURATION"|"SOURCE-COMPUTER"|"OBJECT-COMPUTER"|"INPUT-OUTPUT"|"FILE-CONTROL"|"SELECT"|"ASSIGN"|"TO"|"PICTURE"|"PIC"|"VALUE"|"OPEN"|"READ"|"WRITE"|"CLOSE"|"GO"|"OF"|"FD"|"COPY" {
    emit_token(TOKEN_KEYWORD, yytext, token_start_col);
}

 /* 2. Numeric Literals (and Level Numbers like 01, 77, 88) */
{NUM_LIT} {
    emit_token(TOKEN_LITERAL_NUMERIC, yytext, token_start_col);
}

 /* 3. Alphanumeric Literals */
{STR_SINGLE}|{STR_DOUBLE} {
    emit_token(TOKEN_LITERAL_ALPHANUMERIC, yytext, token_start_col);
}

{UNTERM_STR} {
    emit_lexical_error("Unterminated alphanumeric literal", yytext, token_start_col);
}

 /* 4. Invalid Identifiers (starts or ends with hyphen) */
{INVALID_ID} {
    emit_lexical_error("Identifier cannot start or end with a hyphen", yytext, token_start_col);
}

 /* 5. User-Defined Identifiers (Max 30 characters per Section 2.2) */
{VALID_ID} {
    if (yyleng > 30) {
        emit_lexical_error("Identifier exceeds maximum length of 30 characters", yytext, token_start_col);
    } else {
        emit_token(TOKEN_IDENTIFIER, yytext, token_start_col);
    }
}

 /* 6. Special Symbols / Punctuation (Section 2.2) */
"."     { emit_token(TOKEN_PERIOD, yytext, token_start_col); }
","     { emit_token(TOKEN_COMMA, yytext, token_start_col); }
";"     { emit_token(TOKEN_SEMICOLON, yytext, token_start_col); }
"("     { emit_token(TOKEN_LPAREN, yytext, token_start_col); }
")"     { emit_token(TOKEN_RPAREN, yytext, token_start_col); }

 /* 7. Catch-all for unknown characters */
.       {
    emit_lexical_error("Unrecognized character", yytext, token_start_col);
}

%%

void lexer_scan_cobol_line(const char *area_ab_slice, int seq_num, FILE *out) {
    current_seq_num  = seq_num;
    current_col      = 8;
    token_start_col  = 8;
    lexer_set_output_file(out);

    YY_BUFFER_STATE buf = yy_scan_string(area_ab_slice);
    yylex();
    yy_delete_buffer(buf);
}
