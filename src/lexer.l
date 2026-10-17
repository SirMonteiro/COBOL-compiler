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

/* Exclusive start condition: the PICTURE string that follows PIC/PICTURE [IS] */
%x PICSTR

/* Regular Expression Definitions (Section 2.2) */
DIGIT       [0-9]
/* '_' is not standard COBOL-85 but is widely accepted (GnuCOBOL) and used in PROGRAM-IDs */
ALNUM       [A-Z0-9_]

/* Numeric literals: optional sign, digits, optional INTERNAL decimal point */
NUM_LIT     [+-]?{DIGIT}+(\.{DIGIT}+)?

/* Alphanumeric literals: single or double quoted strings (allowing escaped doubled quotes) */
STR_SINGLE  \'([^\'\n]|\'\')*\'
STR_DOUBLE  \"([^\"\n]|\"\")*\"
UNTERM_STR  (\'([^\'\n]|\'\')*)|(\"([^\"\n]|\"\")*)

/* Valid COBOL Identifier: letters/numbers with optional internal hyphens (e.g. WS-COUNT, 100-PARA) */
VALID_ID    {ALNUM}+(-{ALNUM}+)*

/* Invalid Identifier: starts or ends with hyphen */
INVALID_ID  (-+{ALNUM}[A-Z0-9_-]*)|([A-Z0-9_-]+-+)

/* Reserved words, grouped (each definition must stay on one line: flex cannot wrap them) */
KW_DIVISIONS   ("IDENTIFICATION"|"ENVIRONMENT"|"DATA"|"PROCEDURE"|"DIVISION"|"SECTION"|"PROGRAM-ID"|"AUTHOR"|"INSTALLATION"|"DATE-WRITTEN"|"DATE-COMPILED"|"CONFIGURATION"|"SOURCE-COMPUTER"|"OBJECT-COMPUTER"|"SPECIAL-NAMES"|"INPUT-OUTPUT"|"FILE-CONTROL"|"I-O-CONTROL"|"FILE"|"WORKING-STORAGE"|"LOCAL-STORAGE"|"LINKAGE")
KW_VERBS       ("DISPLAY"|"ACCEPT"|"PERFORM"|"STOP"|"RUN"|"GOBACK"|"IF"|"ELSE"|"MOVE"|"ADD"|"SUBTRACT"|"MULTIPLY"|"DIVIDE"|"COMPUTE"|"OPEN"|"READ"|"WRITE"|"REWRITE"|"DELETE"|"START"|"CLOSE"|"GO"|"CALL"|"CANCEL"|"EVALUATE"|"STRING"|"UNSTRING"|"INSPECT"|"EXIT"|"CONTINUE"|"INITIALIZE"|"SET"|"SEARCH"|"SORT"|"MERGE"|"RETURN"|"RELEASE")
KW_ENDS        ("END"|"END-IF"|"END-PERFORM"|"END-EVALUATE"|"END-READ"|"END-WRITE"|"END-REWRITE"|"END-DELETE"|"END-START"|"END-STRING"|"END-UNSTRING"|"END-CALL"|"END-ADD"|"END-SUBTRACT"|"END-MULTIPLY"|"END-DIVIDE"|"END-COMPUTE"|"END-SEARCH"|"END-RETURN"|"END-ACCEPT"|"END-DISPLAY")
KW_CLAUSES     ("SELECT"|"ASSIGN"|"TO"|"VALUE"|"VALUES"|"OF"|"IN"|"FD"|"SD"|"COPY"|"OCCURS"|"TIMES"|"DEPENDING"|"INDEXED"|"BY"|"ORGANIZATION"|"SEQUENTIAL"|"RELATIVE"|"ACCESS"|"MODE"|"DYNAMIC"|"RANDOM"|"RECORD"|"RECORDS"|"KEY"|"ALTERNATE"|"DUPLICATES"|"STATUS"|"LINE"|"REDEFINES"|"USAGE"|"COMP"|"COMP-1"|"COMP-2"|"COMP-3"|"COMP-4"|"COMP-5"|"COMPUTATIONAL"|"COMPUTATIONAL-3"|"BINARY"|"PACKED-DECIMAL"|"JUSTIFIED"|"JUST"|"RIGHT"|"BLANK"|"SIGN"|"LEADING"|"TRAILING"|"SEPARATE"|"CHARACTER"|"CHARACTERS"|"SYNCHRONIZED"|"SYNC"|"FILLER"|"GLOBAL"|"EXTERNAL"|"BLOCK"|"CONTAINS"|"LABEL"|"STANDARD"|"OMITTED"|"ASCENDING"|"DESCENDING")
KW_MISC        ("INPUT"|"OUTPUT"|"I-O"|"EXTEND"|"GIVING"|"REMAINDER"|"INTO"|"FROM"|"UNTIL"|"VARYING"|"AFTER"|"BEFORE"|"THRU"|"THROUGH"|"WHEN"|"OTHER"|"TRUE"|"FALSE"|"ALSO"|"AT"|"NOT"|"INVALID"|"ON"|"SIZE"|"ERROR"|"OVERFLOW"|"EXCEPTION"|"AND"|"OR"|"EQUAL"|"EQUALS"|"GREATER"|"LESS"|"THAN"|"IS"|"ARE"|"THEN"|"NEXT"|"SENTENCE"|"DELIMITED"|"DELIMITER"|"TALLYING"|"REPLACING"|"CONVERTING"|"ALL"|"FIRST"|"INITIAL"|"POINTER"|"WITH"|"POSITION"|"ADVANCING"|"NO"|"UPON"|"USING"|"RETURNING"|"REFERENCE"|"CONTENT"|"ROUNDED"|"CORRESPONDING"|"CORR"|"NUMERIC"|"ALPHABETIC"|"ALPHABETIC-LOWER"|"ALPHABETIC-UPPER"|"POSITIVE"|"NEGATIVE"|"FUNCTION"|"PROGRAM")
KW_FIGURATIVE  ("SPACE"|"SPACES"|"ZERO"|"ZEROS"|"ZEROES"|"HIGH-VALUE"|"HIGH-VALUES"|"LOW-VALUE"|"LOW-VALUES"|"QUOTE"|"QUOTES")

%%

[ \t\r\n]+ {
    /* Whitespace advances current_col via YY_USER_ACTION; no token emitted */
}

 /* 0. Inline comment (COBOL 2002 '*>'): ignore the rest of the line */
"*>".* { }

 /* 1. PICTURE clause: emit the keyword, then read the picture string as ONE token */
"PICTURE"|"PIC" {
    emit_token(TOKEN_KEYWORD, yytext, token_start_col);
    BEGIN(PICSTR);
}

<PICSTR>[ \t\r\n]+ { /* skip blanks between PIC and its string */ }

<PICSTR>"IS"/[ \t] {
    emit_token(TOKEN_KEYWORD, yytext, token_start_col);
}

<PICSTR>[^ \t\r\n]+ {
    /* A trailing '.' is the sentence terminator, not part of the picture
       (PIC 9(2).  vs  PIC ZZ9.99).  Give it back to the scanner. */
    if (yyleng > 1 && yytext[yyleng - 1] == '.') {
        yyless(yyleng - 1);
        current_col -= 1;          /* YY_USER_ACTION already counted it */
    }
    emit_token(TOKEN_PICTURE, yytext, token_start_col);
    BEGIN(INITIAL);
}

 /* 2. Reserved Keywords */
{KW_DIVISIONS}|{KW_VERBS}|{KW_ENDS}|{KW_CLAUSES}|{KW_MISC}|{KW_FIGURATIVE} {
    emit_token(TOKEN_KEYWORD, yytext, token_start_col);
}

 /* 3. Numeric Literals (and Level Numbers like 01, 77, 88) */
{NUM_LIT} {
    emit_token(TOKEN_LITERAL_NUMERIC, yytext, token_start_col);
}

 /* 4. Alphanumeric Literals */
{STR_SINGLE}|{STR_DOUBLE} {
    emit_token(TOKEN_LITERAL_ALPHANUMERIC, yytext, token_start_col);
}

{UNTERM_STR} {
    emit_lexical_error("Unterminated alphanumeric literal", yytext, token_start_col);
}

 /* 5. Invalid Identifiers (starts or ends with hyphen) */
{INVALID_ID} {
    emit_lexical_error("Identifier cannot start or end with a hyphen", yytext, token_start_col);
}

 /* 6. User-Defined Identifiers (Max 30 characters per Section 2.2) */
{VALID_ID} {
    if (yyleng > 30) {
        emit_lexical_error("Identifier exceeds maximum length of 30 characters", yytext, token_start_col);
    } else {
        emit_token(TOKEN_IDENTIFIER, yytext, token_start_col);
    }
}

 /* 7. Special Symbols / Punctuation / Operators (Section 2.2) */
"."     { emit_token(TOKEN_PERIOD,    yytext, token_start_col); }
","     { emit_token(TOKEN_COMMA,     yytext, token_start_col); }
";"     { emit_token(TOKEN_SEMICOLON, yytext, token_start_col); }
"("     { emit_token(TOKEN_LPAREN,    yytext, token_start_col); }
")"     { emit_token(TOKEN_RPAREN,    yytext, token_start_col); }
":"     { emit_token(TOKEN_COLON,     yytext, token_start_col); }
"**"    { emit_token(TOKEN_POWER,     yytext, token_start_col); }
"+"     { emit_token(TOKEN_PLUS,      yytext, token_start_col); }
"-"     { emit_token(TOKEN_MINUS,     yytext, token_start_col); }
"*"     { emit_token(TOKEN_STAR,      yytext, token_start_col); }
"/"     { emit_token(TOKEN_SLASH,     yytext, token_start_col); }
"="     { emit_token(TOKEN_EQ,        yytext, token_start_col); }
"<>"    { emit_token(TOKEN_NE,        yytext, token_start_col); }
"<="    { emit_token(TOKEN_LE,        yytext, token_start_col); }
">="    { emit_token(TOKEN_GE,        yytext, token_start_col); }
"<"     { emit_token(TOKEN_LT,        yytext, token_start_col); }
">"     { emit_token(TOKEN_GT,        yytext, token_start_col); }

 /* 8. Catch-all for unknown characters */
.       {
    emit_lexical_error("Unrecognized character", yytext, token_start_col);
}

%%

/* Drop any leftover start condition (e.g. a dangling PIC) between files. */
void lexer_reset(void) {
    BEGIN(INITIAL);
}

void lexer_scan_cobol_line(const char *area_ab_slice, int seq_num, FILE *out) {
    current_seq_num  = seq_num;
    current_col      = 8;
    token_start_col  = 8;
    lexer_set_output_file(out);

    YY_BUFFER_STATE buf = yy_scan_string(area_ab_slice);
    yylex();
    yy_delete_buffer(buf);
}
