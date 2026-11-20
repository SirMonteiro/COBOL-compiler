#include "lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLICE_W        65
#define TEXT_MAX       1024
#define MAX_COPY_DEPTH 16

static FILE *lexer_out = NULL;
static TokenList *active_tokens = NULL;
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

void lexer_reset_errors(void) {
    lexer_errors = 0;
}

void lexer_set_output_file(FILE *out) {
    lexer_out = out;
}

void lexer_set_token_list(TokenList *list) {
    active_tokens = list;
}

void token_list_init(TokenList *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void token_list_push(TokenList *list, const Token *tok) {
    if (!list || !tok) return;
    if (list->count >= list->capacity) {
        size_t next_cap = list->capacity ? list->capacity * 2 : 256;
        Token *next = (Token *)realloc(list->items, next_cap * sizeof(Token));
        if (!next) {
            fprintf(stderr, "[S] ERROR: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        list->items = next;
        list->capacity = next_cap;
    }
    list->items[list->count++] = *tok;
}

void token_list_free(TokenList *list) {
    if (!list) return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
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

    if (active_tokens) {
        Token tok;
        tok.type = type;
        size_t n = strlen(normalized);
        if (n >= sizeof(tok.lexeme)) n = sizeof(tok.lexeme) - 1;
        memcpy(tok.lexeme, normalized, n);
        tok.lexeme[n] = '\0';
        tok.seq_num = current_seq_num;
        tok.column = start_col;
        tok.area = area;
        token_list_push(active_tokens, &tok);
    }

    if (lexer_out) {
        fprintf(lexer_out,
                "[SEQ %06d | COL %02d (%s)] %-26s : %s\n",
                current_seq_num,
                start_col,
                area_to_string(area),
                token_type_to_string(type),
                normalized);
    }
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
        if (cont_state.has_prev) {
            const COBOL_LINE prev = cont_state.prev;
            const size_t plen = strlen(prev.code_slice);

            size_t lead = 0;
            while (lead < n && s[lead] == ' ') lead++;

            if (plen == SLICE_W && is_word_char(prev.code_slice[plen - 1]) &&
                lead < n && is_word_char(s[lead])) {
                size_t pstart = plen;
                while (pstart > 0 && is_word_char(prev.code_slice[pstart - 1])) pstart--;

                size_t send = lead;
                while (send < n && is_word_char(s[send])) send++;

                char joined[TEXT_MAX];
                size_t jlen = (plen - pstart) + (send - lead);
                if (jlen < sizeof joined) {
                    cont_state.has_prev = false;
                    scan_with_string_check(prev.code_slice, pstart, prev.seq_num, out);

                    if (!cont_state.in_string) {
                        memcpy(joined, prev.code_slice + pstart, plen - pstart);
                        memcpy(joined + (plen - pstart), s + lead, send - lead);
                        joined[jlen] = '\0';

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

        cont_state.prev = *line;
        cont_state.has_prev = true;
        return;
    }

    flush_prev_line(out);
    flush_open_string(out);
    cont_state.prev = *line;
    cont_state.has_prev = true;
}

void lexer_end_file(FILE *out) {
    flush_prev_line(out);
    flush_open_string(out);
}

/* --- Copybook lookup and high-level file lexing --- */

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

static bool resolve_copybook(const char *name, const char *current_file,
                             const char *const *copy_dirs, int n_copy_dirs,
                             char *out_path, size_t out_size) {
    /* Strip surrounding quotes if COPY 'NAME'. */
    char clean[256];
    size_t len = strlen(name);
    if (len >= 2 && (name[0] == '\'' || name[0] == '"') && name[len - 1] == name[0]) {
        len -= 2;
        if (len >= sizeof(clean)) len = sizeof(clean) - 1;
        memcpy(clean, name + 1, len);
        clean[len] = '\0';
    } else {
        strncpy(clean, name, sizeof(clean) - 1);
        clean[sizeof(clean) - 1] = '\0';
    }

    static const char *exts[] = { ".cpy", ".CPY", ".cbl", ".CBL", ".cob", ".COB", "" };

    /* Build candidate directory list: -I dirs, source dir, codigo_fonte_cobol */
    char src_dir[512] = ".";
    const char *slash = strrchr(current_file, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - current_file);
        if (dlen >= sizeof(src_dir)) dlen = sizeof(src_dir) - 1;
        memcpy(src_dir, current_file, dlen);
        src_dir[dlen] = '\0';
    }

    const char *dirs[64];
    int nd = 0;
    for (int i = 0; i < n_copy_dirs && nd < 60; i++) dirs[nd++] = copy_dirs[i];
    dirs[nd++] = src_dir;
    dirs[nd++] = "codigo_fonte_cobol";
    dirs[nd++] = ".";

    for (int d = 0; d < nd; d++) {
        for (size_t e = 0; e < sizeof(exts) / sizeof(exts[0]); e++) {
            snprintf(out_path, out_size, "%s/%s%s", dirs[d], clean, exts[e]);
            if (file_exists(out_path)) return true;
        }
    }
    return false;
}

static bool lex_cobol_file_rec(const char *input_path, const OPTS *opts, FILE *token_out,
                               TokenList *out_tokens, const char *const *copy_dirs,
                               int n_copy_dirs, bool expand_copy, int depth) {
    FILE *cobolSrc = fopen(input_path, "r");
    if (!cobolSrc) {
        fprintf(stderr, "[S] ERROR: Failed to open input file \"%s\".\n", input_path);
        return false;
    }

    TokenList local_tokens;
    token_list_init(&local_tokens);

    TokenList *prev_list = active_tokens;
    FILE *prev_out = lexer_out;

    lexer_set_token_list(&local_tokens);
    lexer_set_output_file(expand_copy ? NULL : token_out);
    lexer_begin_file();

    bool ok_all = true;
    char raw_buffer[256];
    int line_no = 0;

    while (fgets(raw_buffer, sizeof(raw_buffer), cobolSrc) != NULL) {
        line_no++;
        size_t n = strlen(raw_buffer);
        if (n > 0 && raw_buffer[n - 1] != '\n' && !feof(cobolSrc)) {
            int c;
            while ((c = fgetc(cobolSrc)) != '\n' && c != EOF) { }
        }

        COBOL_LINE parsed_line;
        if (!read_cobol_line(opts, raw_buffer, &parsed_line)) {
            if (parsed_line.seq_num == 0) parsed_line.seq_num = line_no;
            if (token_out) fflush(token_out);
            fprintf(stderr,
                    "[S] Indicator Error (%s, Seq %06d): Invalid indicator '%c' in Column 7.\n",
                    input_path, parsed_line.seq_num, parsed_line.indicator);
            ok_all = false;
            continue;
        }

        if (parsed_line.seq_num == 0) parsed_line.seq_num = line_no;

        switch (parsed_line.status) {
            case LINE_STATUS_CONTINUATION:
            case LINE_STATUS_CODE:
                lexer_process_line(&parsed_line, expand_copy ? NULL : token_out);
                break;
            case LINE_STATUS_COMMENT:
                if (opts && opts->display_comments && token_out) {
                    fprintf(token_out, "[I] Comment (Seq %06d): %s\n",
                            parsed_line.seq_num, parsed_line.comment);
                }
                break;
            case LINE_STATUS_EMPTY:
            case LINE_STATUS_DEBUG_SKIPPED:
            default:
                break;
        }
    }

    lexer_end_file(expand_copy ? NULL : token_out);
    fclose(cobolSrc);

    lexer_set_token_list(prev_list);
    lexer_set_output_file(prev_out);

    /* Transfer tokens, optionally expanding COPY <name>. directives */
    for (size_t i = 0; i < local_tokens.count; i++) {
        const Token *t = &local_tokens.items[i];
        if (expand_copy && depth < MAX_COPY_DEPTH &&
            t->type == TOKEN_KEYWORD && strcmp(t->lexeme, "COPY") == 0 &&
            i + 2 < local_tokens.count &&
            (local_tokens.items[i + 1].type == TOKEN_IDENTIFIER ||
             local_tokens.items[i + 1].type == TOKEN_LITERAL_ALPHANUMERIC) &&
            local_tokens.items[i + 2].type == TOKEN_PERIOD) {
            char cpy_path[1024];
            if (resolve_copybook(local_tokens.items[i + 1].lexeme, input_path,
                                 copy_dirs, n_copy_dirs, cpy_path, sizeof(cpy_path))) {
                if (!lex_cobol_file_rec(cpy_path, opts, token_out, out_tokens,
                                        copy_dirs, n_copy_dirs, expand_copy, depth + 1)) {
                    ok_all = false;
                }
                i += 2;
                continue;
            } else {
                fprintf(stderr,
                        "[W] Copybook Warning (%s, Seq %06d): Copybook \"%s\" not found; keeping COPY node.\n",
                        input_path, t->seq_num, local_tokens.items[i + 1].lexeme);
            }
        }

        if (out_tokens) token_list_push(out_tokens, t);
        if (expand_copy && token_out) {
            fprintf(token_out,
                    "[SEQ %06d | COL %02d (%s)] %-26s : %s\n",
                    t->seq_num,
                    t->column,
                    area_to_string(t->area),
                    token_type_to_string(t->type),
                    t->lexeme);
        }
    }

    token_list_free(&local_tokens);
    return ok_all;
}

bool lex_cobol_file(const char *input_path, const OPTS *opts, FILE *token_out,
                    TokenList *out_tokens, const char *const *copy_dirs, int n_copy_dirs,
                    bool expand_copy) {
    return lex_cobol_file_rec(input_path, opts, token_out, out_tokens,
                              copy_dirs, n_copy_dirs, expand_copy, 0);
}
