#ifndef LEXER_INPUT_H
#define LEXER_INPUT_H

#include <stdio.h>
#include "read.h"

/*
 * Feeds COBOL_LINEs to the lexer and handles continuation lines.
 * The pieces are joined and emitted as ONE token positioned at the column
 * where the literal started.
 */
void lexer_input_begin(void); /* call before the first line of each file */
void lexer_input_line(const COBOL_LINE *line, FILE *out); /* call for every LINE_STATUS_CODE / _CONTINUATION line */
void lexer_input_end(FILE *out); /* call after the last line; reports a literal that was never closed */

#endif /* LEXER_INPUT_H */
