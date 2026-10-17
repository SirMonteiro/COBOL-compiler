#ifndef READ_H
#define READ_H

#include <stdbool.h>

typedef struct {
    bool display_comments;
    bool include_debug;
} OPTS;

typedef enum {
    LINE_STATUS_EMPTY,
    LINE_STATUS_COMMENT,
    LINE_STATUS_DEBUG_SKIPPED,
    LINE_STATUS_CONTINUATION,
    LINE_STATUS_CODE,
    LINE_STATUS_INVALID_INDICATOR
} LINE_STATUS;

typedef struct {
    int seq_num;
    char indicator;
    LINE_STATUS status;
    /* Preserves exact spacing for Columns 8-72 so Flex knows exact column numbers */
    char code_slice[66]; // Columns 8-72 + \0
    char comment[80];    // Comment text (cols 8-80), only set for LINE_STATUS_COMMENT
} COBOL_LINE;

bool read_cobol_line(const OPTS *opts, const char *raw_line, COBOL_LINE *out_line);

#endif /* READ_H */
