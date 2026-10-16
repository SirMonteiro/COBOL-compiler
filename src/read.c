#include "read.h"

#include <stdlib.h>
#include <string.h>

bool read_cobol_line(const OPTS *opts, const char *raw_line, COBOL_LINE *out_line) {
    if (!raw_line || !out_line) return false;

    int len = strlen(raw_line);
    while (len > 0 && (raw_line[len - 1] == '\n' || raw_line[len - 1] == '\r'))
        len--;

    if (len < 7) {
        out_line->status = LINE_STATUS_EMPTY;
        return true;
    }

    /* Extract Sequence Number Area: Columns 1-6 (indices 0..5) */
    char seq_buf[7];
    memcpy(seq_buf, raw_line, 6);
    seq_buf[6] = '\0';
    out_line->seq_num = atoi(seq_buf);

    /* Extract Indicator Area: Column 7 (index 6) */
    char ind = raw_line[6];
    out_line->indicator = ind;

    if (len == 7) {
        out_line->status = LINE_STATUS_EMPTY;
        return true;
    }

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
            if (!opts->include_debug) {
                out_line->status = LINE_STATUS_DEBUG_SKIPPED;
                return true;
            }
            out_line->status = LINE_STATUS_CODE;
            break;
        default:
            out_line->status = LINE_STATUS_INVALID_INDICATOR;
            return false;
    }

    int max_code_idx = (len > 72) ? 72 : len;
    max_code_idx = max_code_idx - 7;
    memcpy(out_line->code_slice, raw_line + 7, max_code_idx);
    out_line->code_slice[max_code_idx] = '\0';
    return true;
}
