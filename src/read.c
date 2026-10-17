#include "read.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define SEQ_LEN     6
#define CODE_START  7
#define CODE_END    72
#define TAB_WIDTH   8   /*  match GnuCOBOL */
#define LINE_MAX_EXP 1024

/* Expand tabs to spaces to correct column numbers. */
static size_t expand_tabs(const char *src, size_t len, char *dst, size_t size) {
    size_t o = 0;
    for (size_t i = 0; i < len && o + 1 < size; i++) {
        if (src[i] == '\t') {
            size_t next = (o / TAB_WIDTH + 1) * TAB_WIDTH;
            while (o < next && o + 1 < size) dst[o++] = ' ';
        } else {
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
    return o;
}

static bool all_blank(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!isspace((unsigned char)s[i])) return false;
    return true;
}

/* Sequence area (cols 1-6): returns numeric value or 0 when blank or
 * non-numeric*/
static int parse_seq(const char *raw, size_t len) {
    char buf[SEQ_LEN + 1];
    size_t n = len < SEQ_LEN ? len : SEQ_LEN;
    memcpy(buf, raw, n);
    buf[n] = '\0';

    size_t i = 0;
    while (buf[i] == ' ') i++;
    if (buf[i] == '\0') return 0;
    for (size_t j = i; buf[j]; j++)
        if (!isdigit((unsigned char)buf[j]) && buf[j] != ' ') return 0;
    return atoi(buf);
}

bool read_cobol_line(const OPTS *opts, const char *raw_line, COBOL_LINE *out_line) {
    if (!raw_line || !out_line) return false;

    /* Always leave out_line fully initialised. */
    memset(out_line, 0, sizeof *out_line);
    out_line->indicator = ' ';
    out_line->status = LINE_STATUS_EMPTY;

    const bool include_debug = opts && opts->include_debug;

    size_t rawlen = strlen(raw_line);
    while (rawlen > 0 && (raw_line[rawlen - 1] == '\n' || raw_line[rawlen - 1] == '\r'))
        rawlen--;

    /* Work on a tab-expanded copy from here on. */
    char expanded[LINE_MAX_EXP];
    size_t len = expand_tabs(raw_line, rawlen, expanded, sizeof expanded);
    raw_line = expanded;

    out_line->seq_num = parse_seq(raw_line, len);

    /* No indicator column present: nothing on this line. */
    if (len <= SEQ_LEN) return true;

    /* Indicator Area: Column 7 (index 6) */
    const char ind = raw_line[SEQ_LEN];
    out_line->indicator = ind;

    switch (ind) {
        case ' ':
            out_line->status = LINE_STATUS_CODE;
            break;
        case '-':
            out_line->status = LINE_STATUS_CONTINUATION;
            break;
        case '*':
        case '/': {
            out_line->status = LINE_STATUS_COMMENT;
            size_t cn = len > CODE_START ? len - CODE_START : 0;
            if (cn > sizeof out_line->comment - 1) cn = sizeof out_line->comment - 1;
            memcpy(out_line->comment, raw_line + CODE_START, cn);
            out_line->comment[cn] = '\0';
            return true;
        }
        case 'D':
        case 'd':
            if (!include_debug) {
                out_line->status = LINE_STATUS_DEBUG_SKIPPED;
                return true;
            }
            out_line->status = LINE_STATUS_CODE;
            break;
        default:
            out_line->status = LINE_STATUS_INVALID_INDICATOR;
            return false;
    }

    /* Program text: columns 8-72. Past column 72 is ignored. */
    size_t end = len > CODE_END ? CODE_END : len;
    size_t n = end > CODE_START ? end - CODE_START : 0;
    memcpy(out_line->code_slice, raw_line + CODE_START, n);
    out_line->code_slice[n] = '\0';

    /* Treat empty columns 8-72 as an empty line. */
    if (all_blank(out_line->code_slice, n))
        out_line->status = LINE_STATUS_EMPTY;

    return true;
}
