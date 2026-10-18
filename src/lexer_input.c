#include "lexer_input.h"
#include "lexer.h"

#include <string.h>

#define SLICE_W   65     /* columns 8-72 */
#define TEXT_MAX  1024

typedef struct {
    bool active;
    char quote;
    char text[TEXT_MAX];
    size_t len;
    int  col;              /* column of the opening quote */
    int  seq;              /* sequence/line number of the line that opened it */
} PENDING_LIT;

static PENDING_LIT pend;

static void pend_append(const char *s, size_t n) {
    for (size_t i = 0; i < n && pend.len + 1 < TEXT_MAX; i++)
        pend.text[pend.len++] = s[i];
    pend.text[pend.len] = '\0';
}

static void pend_pad_to(size_t total_len_from_col) {
    /* A literal open at column 72 includes the blanks up to column 72,
       even if the editor trimmed trailing spaces from the line. */
    while (pend.len < total_len_from_col && pend.len + 1 < TEXT_MAX)
        pend.text[pend.len++] = ' ';
    pend.text[pend.len] = '\0';
}

static void flush_unterminated(FILE *out) {
    if (!pend.active) return;
    current_seq_num = pend.seq;
    lexer_set_output_file(out);
    /* trim the padding */
    size_t n = pend.len;
    while (n > 1 && pend.text[n - 1] == ' ') n--;
    pend.text[n] = '\0';
    emit_lexical_error("Unterminated alphanumeric literal", pend.text, pend.col);
    pend.active = false;
}

/* Returns index of the closing quote of a literal opened at 'from-1', or n if
 * it is still open. A doubled quote is an escaped quote. */
static size_t find_close(const char *s, size_t from, size_t n, char q) {
    size_t j = from;
    while (j < n) {
        if (s[j] == q) {
            if (j + 1 < n && s[j + 1] == q) { j += 2; continue; }
            return j;
        }
        j++;
    }
    return n;
}

static void scan_text(const char *s, size_t n, int seq, FILE *out) {
    char tmp[SLICE_W + 1];
    if (n > SLICE_W) n = SLICE_W;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    lexer_scan_cobol_line(tmp, seq, out);
}

/* Lex one slice (columns already preserved by blanking consumed characters).
 * If a literal is still open at the end of the slice, hold it in 'pend'. */
static void process_segment(const char *s, size_t n, int seq, FILE *out) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '*' && i + 1 < n && s[i + 1] == '>') break;   /* inline comment */
        if (s[i] != '"' && s[i] != '\'') continue;

        size_t close = find_close(s, i + 1, n, s[i]);
        if (close < n) { i = close; continue; }       /* closed literal on this line */

        /* Literal open at column 72, lex and keep*/
        scan_text(s, i, seq, out);
        pend.active = true;
        pend.quote  = s[i];
        pend.len    = 0;
        pend.col    = 8 + (int)i;
        pend.seq    = seq;
        pend_append(s + i, n - i);
        pend_pad_to(SLICE_W - i);
        return;
    }
    scan_text(s, n, seq, out);
}

static void continue_literal(const char *s, size_t n, int seq, FILE *out) {
    size_t i = 0;
    while (i < n && s[i] == ' ') i++;
    if (i == n) return;                                /* blank continuation line */

    if (s[i] != pend.quote) {
        flush_unterminated(out);
        current_seq_num = seq;
        emit_lexical_error("Continuation line must start with the literal's quote character",
                           "-", 7);
        process_segment(s, n, seq, out);
        return;
    }

    size_t start = i + 1;
    size_t close = find_close(s, start, n, pend.quote);

    if (close >= n) {
        pend_append(s + start, n - start);
        pend_pad_to(pend.len + (SLICE_W - n));
        return;
    }

    /* Closed, take everything up to and including the closing quote */
    pend_append(s + start, close - start + 1);
    current_seq_num = pend.seq;
    lexer_set_output_file(out);
    emit_token(TOKEN_LITERAL_ALPHANUMERIC, pend.text, pend.col);
    pend.active = false;

    /* Lex the rest of this line with columns preserved */
    char rest[SLICE_W + 1];
    memset(rest, ' ', close + 1);
    memcpy(rest + close + 1, s + close + 1, n - close - 1);
    process_segment(rest, n, seq, out);
}

void lexer_input_begin(void) {
    memset(&pend, 0, sizeof pend);
    lexer_reset();
}

void lexer_input_line(const COBOL_LINE *line, FILE *out) {
    const char *s = line->code_slice;
    size_t n = strlen(s);

    if (line->status == LINE_STATUS_CONTINUATION && pend.active) {
        continue_literal(s, n, line->seq_num, out);
        return;
    }

    /* Normal line while a literal is still open*/
    flush_unterminated(out);
    process_segment(s, n, line->seq_num, out);
}

void lexer_input_end(FILE *out) {
    flush_unterminated(out);
}
