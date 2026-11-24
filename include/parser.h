#ifndef PARSER_H
#define PARSER_H

#include <string>
#include "lexer.h"
#include "ast.h"

/* Parse a TokenList into an AST. Returns nullptr on syntax error. */
ASTNode *parse_tokens(const TokenList &tokens, const std::string &filename, int *error_count);

#endif /* PARSER_H */
