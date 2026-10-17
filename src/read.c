#include "read.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define SEQ_LEN     6
#define CODE_START  7
#define CODE_END    72

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

    size_t len = strlen(raw_line);
    while (len > 0 && (raw_line[len - 1] == '\n' || raw_line[len - 1] == '\r'))
        len--;

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
        case '/':
            out_line->status = LINE_STATUS_COMMENT;
            return true;
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
