#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define SLICE_W  65
#define TEXT_MAX 1024

static FILE *lexer_out = NULL;
static int lexer_errors = 0;

/* Continuation state across physical lines */
static struct {
    bool has_prev;
    COBOL_LINE prev;

    bool in_string;
    char quote;
    char text[TEXT_MAX];
    size_t len;
    int col;
    int seq;
} cont_state;

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
        case TOKEN_COLON:                return "TOKEN_COLON";
        case TOKEN_PICTURE:              return "TOKEN_PICTURE";
        case TOKEN_PLUS:                 return "TOKEN_PLUS";
        case TOKEN_MINUS:                return "TOKEN_MINUS";
        case TOKEN_STAR:                 return "TOKEN_STAR";
        case TOKEN_SLASH:                return "TOKEN_SLASH";
        case TOKEN_POWER:                return "TOKEN_POWER";
        case TOKEN_EQ:                   return "TOKEN_EQ";
        case TOKEN_NE:                   return "TOKEN_NE";
        case TOKEN_LT:                   return "TOKEN_LT";
        case TOKEN_LE:                   return "TOKEN_LE";
        case TOKEN_GT:                   return "TOKEN_GT";
        case TOKEN_GE:                   return "TOKEN_GE";
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
    COBOL_AREA area = (start_col >= 8 && start_col <= 11) ? AREA_A :
                      (start_col >= 12 ? AREA_B : AREA_UNKNOWN);

    /* Normalize keywords and identifiers to uppercase */
    char normalized[TEXT_MAX];
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
    if (lexer_out) fflush(lexer_out);
    fprintf(stderr,
            "[E] Lexical Error (Seq %06d, Col %d): %s -> \"%s\"\n",
            current_seq_num, start_col, msg, lexeme);
    fflush(stderr);
    emit_token(TOKEN_UNKNOWN, lexeme, start_col);
}

static size_t find_closing_quote(const char *s, size_t from, size_t n, char q) {
    for (size_t j = from; j < n; j++) {
        if (s[j] == q) {
            if (j + 1 < n && s[j + 1] == q) {
                j++;
                continue;
            }
            return j;
        }
    }
    return n;
}

static void scan_slice_n(const char *s, size_t n, int seq, FILE *out) {
    char tmp[SLICE_W + 1];
    if (n > SLICE_W) n = SLICE_W;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    lexer_scan_cobol_line(tmp, seq, out);
}

static void string_append(const char *s, size_t n) {
    for (size_t i = 0; i < n && cont_state.len + 1 < TEXT_MAX; i++)
        cont_state.text[cont_state.len++] = s[i];
    cont_state.text[cont_state.len] = '\0';
}

static void string_pad_to(size_t target_len) {
    while (cont_state.len < target_len && cont_state.len + 1 < TEXT_MAX)
        cont_state.text[cont_state.len++] = ' ';
    cont_state.text[cont_state.len] = '\0';
}

static void flush_open_string(FILE *out) {
    if (!cont_state.in_string) return;
    current_seq_num = cont_state.seq;
    lexer_set_output_file(out);
    while (cont_state.len > 1 && cont_state.text[cont_state.len - 1] == ' ')
        cont_state.text[--cont_state.len] = '\0';
    emit_lexical_error("Unterminated alphanumeric literal", cont_state.text, cont_state.col);
    cont_state.in_string = false;
}

/* Scan a slice; if an alphanumeric literal stays open past col 72, save it for '-' continuation. */
static void scan_with_string_check(const char *s, size_t n, int seq, FILE *out) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '*' && i + 1 < n && s[i + 1] == '>') break;
        if (s[i] != '"' && s[i] != '\'') continue;

        size_t close = find_closing_quote(s, i + 1, n, s[i]);
        if (close < n) {
            i = close;
            continue;
        }

        scan_slice_n(s, i, seq, out);
        cont_state.in_string = true;
        cont_state.quote = s[i];
        cont_state.len = 0;
        cont_state.col = 8 + (int)i;
        cont_state.seq = seq;
        string_append(s + i, n - i);
        string_pad_to(SLICE_W - i);
        return;
    }
    scan_slice_n(s, n, seq, out);
}

static void handle_string_continuation(const char *s, size_t n, int seq, FILE *out) {
    size_t i = 0;
    while (i < n && s[i] == ' ') i++;
    if (i == n) return;

    if (s[i] != cont_state.quote) {
        flush_open_string(out);
        current_seq_num = seq;
        emit_lexical_error("Continuation line must start with the literal's quote character",
                           "-", 7);
        scan_with_string_check(s, n, seq, out);
        return;
    }

    size_t start = i + 1;
    size_t close = find_closing_quote(s, start, n, cont_state.quote);
    if (close >= n) {
        string_append(s + start, n - start);
        string_pad_to(cont_state.len + (SLICE_W - n));
        return;
    }

    string_append(s + start, close - start + 1);
    current_seq_num = cont_state.seq;
    lexer_set_output_file(out);
    emit_token(TOKEN_LITERAL_ALPHANUMERIC, cont_state.text, cont_state.col);
    cont_state.in_string = false;

    char rest[SLICE_W + 1];
    memset(rest, ' ', close + 1);
    memcpy(rest + close + 1, s + close + 1, n - close - 1);
    scan_with_string_check(rest, n, seq, out);
}

static bool is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

static void flush_prev_line(FILE *out) {
    if (!cont_state.has_prev) return;
    const COBOL_LINE *p = &cont_state.prev;
    cont_state.has_prev = false;
    scan_with_string_check(p->code_slice, strlen(p->code_slice), p->seq_num, out);
}

void lexer_begin_file(void) {
    memset(&cont_state, 0, sizeof cont_state);
    lexer_reset();
}

void lexer_process_line(const COBOL_LINE *line, FILE *out) {
    const char *s = line->code_slice;
    size_t n = strlen(s);

    if (line->status == LINE_STATUS_CONTINUATION) {
        /* If a previous normal line hasn't been flushed yet, check string vs split-word continuation */
        if (cont_state.has_prev) {
            const COBOL_LINE prev = cont_state.prev;
            const size_t plen = strlen(prev.code_slice);

            /* Check if prev ended mid-word at col 72 and this line continues the word */
            size_t lead = 0;
            while (lead < n && s[lead] == ' ') lead++;

            if (plen == SLICE_W && is_word_char(prev.code_slice[plen - 1]) &&
                lead < n && is_word_char(s[lead])) {
                size_t pstart = plen;
                while (pstart > 0 && is_word_char(prev.code_slice[pstart - 1])) pstart--;

                /* Only treat as word continuation if prev prefix wasn't inside an open string */
                size_t send = lead;
                while (send < n && is_word_char(s[send])) send++;

                char joined[TEXT_MAX];
                size_t jlen = (plen - pstart) + (send - lead);
                if (jlen < sizeof joined) {
                    /* First scan prev up to pstart */
                    cont_state.has_prev = false;
                    scan_with_string_check(prev.code_slice, pstart, prev.seq_num, out);

                    if (!cont_state.in_string) {
                        memcpy(joined, prev.code_slice + pstart, plen - pstart);
                        memcpy(joined + (plen - pstart), s + lead, send - lead);
                        joined[jlen] = '\0';

                        /* Lex the joined token with prev's seq_num and starting column */
                        lexer_scan_cobol_line_at(joined, prev.seq_num, 8 + (int)pstart, out);
                        char rest[SLICE_W + 1];
                        memset(rest, ' ', send);
                        memcpy(rest + send, s + send, n - send);
                        rest[n] = '\0';
                        cont_state.prev = *line;
                        memcpy(cont_state.prev.code_slice, rest, n + 1);
                        cont_state.has_prev = true;
                        return;
                    }
                    /* If prev actually had an open string, fall through to string continuation */
                    handle_string_continuation(s, n, line->seq_num, out);
                    return;
                }
            }

            flush_prev_line(out);
        }

        if (cont_state.in_string) {
            handle_string_continuation(s, n, line->seq_num, out);
            return;
        }

        /* Standalone continuation (e.g. statement continuation): scan normally */
        cont_state.prev = *line;
        cont_state.has_prev = true;
        return;
    }

    /* Normal LINE_STATUS_CODE */
    flush_prev_line(out);
    flush_open_string(out);
    cont_state.prev = *line;
    cont_state.has_prev = true;
}

void lexer_end_file(FILE *out) {
    flush_prev_line(out);
    flush_open_string(out);
}
