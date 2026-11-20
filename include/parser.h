#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

/* Parse a TokenList into an AST. Returns NULL on syntax error. */
ASTNode *parse_tokens(const TokenList *tokens, const char *filename, int *error_count);

#endif /* PARSER_H */
