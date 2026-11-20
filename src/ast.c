#include "ast.h"

#include <stdlib.h>
#include <string.h>

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    if (!copy) {
        fprintf(stderr, "[S] ERROR: Out of memory.\n");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, s, n);
    return copy;
}

const char *ast_kind_name(ASTKind kind) {
    switch (kind) {
        case AST_PROGRAM:            return "PROGRAM";
        case AST_IDENTIFICATION:     return "IDENTIFICATION_DIVISION";
        case AST_ID_ENTRY:           return "ID_ENTRY";
        case AST_ENVIRONMENT:        return "ENVIRONMENT_DIVISION";
        case AST_CONFIGURATION:      return "CONFIGURATION_SECTION";
        case AST_SPECIAL_NAMES:      return "SPECIAL_NAMES";
        case AST_FILE_CONTROL:       return "FILE_CONTROL";
        case AST_SELECT:             return "SELECT";
        case AST_DATA_DIVISION:      return "DATA_DIVISION";
        case AST_DATA_SECTION:       return "DATA_SECTION";
        case AST_FD:                 return "FD";
        case AST_DATA_ITEM:          return "DATA_ITEM";
        case AST_COPY:               return "COPY";
        case AST_PROCEDURE_DIVISION: return "PROCEDURE_DIVISION";
        case AST_PROC_SECTION:       return "SECTION";
        case AST_PARAGRAPH:          return "PARAGRAPH";
        case AST_SENTENCE:           return "SENTENCE";
        case AST_STMT_DISPLAY:       return "DISPLAY";
        case AST_STMT_ACCEPT:        return "ACCEPT";
        case AST_STMT_MOVE:          return "MOVE";
        case AST_STMT_ADD:           return "ADD";
        case AST_STMT_SUBTRACT:      return "SUBTRACT";
        case AST_STMT_MULTIPLY:      return "MULTIPLY";
        case AST_STMT_DIVIDE:        return "DIVIDE";
        case AST_STMT_COMPUTE:       return "COMPUTE";
        case AST_STMT_IF:            return "IF";
        case AST_STMT_EVALUATE:      return "EVALUATE";
        case AST_WHEN:               return "WHEN";
        case AST_STMT_PERFORM:       return "PERFORM";
        case AST_STMT_OPEN:          return "OPEN";
        case AST_STMT_CLOSE:         return "CLOSE";
        case AST_STMT_READ:          return "READ";
        case AST_STMT_WRITE:         return "WRITE";
        case AST_STMT_REWRITE:       return "REWRITE";
        case AST_STMT_DELETE:        return "DELETE";
        case AST_STMT_START:         return "START";
        case AST_STMT_GOTO:          return "GO_TO";
        case AST_STMT_CALL:          return "CALL";
        case AST_STMT_CANCEL:        return "CANCEL";
        case AST_STMT_STOP_RUN:      return "STOP_RUN";
        case AST_STMT_GOBACK:        return "GOBACK";
        case AST_STMT_EXIT:          return "EXIT";
        case AST_STMT_CONTINUE:      return "CONTINUE";
        case AST_STMT_NEXT_SENTENCE: return "NEXT_SENTENCE";
        case AST_STMT_INITIALIZE:    return "INITIALIZE";
        case AST_STMT_INSPECT:       return "INSPECT";
        case AST_STMT_STRING:        return "STRING";
        case AST_STMT_UNSTRING:      return "UNSTRING";
        case AST_STMT_SET:           return "SET";
        case AST_STMT_SEARCH:        return "SEARCH";
        case AST_STMT_SORT:          return "SORT";
        case AST_STMT_MERGE:         return "MERGE";
        case AST_STMT_RELEASE:       return "RELEASE";
        case AST_STMT_RETURN:        return "RETURN";
        case AST_CLAUSE:             return "CLAUSE";
        case AST_DATA_REF:           return "DATA_REF";
        case AST_REFMOD:             return "REFMOD";
        case AST_SUBSCRIPT:          return "SUBSCRIPT";
        case AST_LITERAL_NUM:        return "LITERAL_NUM";
        case AST_LITERAL_STR:        return "LITERAL_STR";
        case AST_FIGURATIVE:         return "FIGURATIVE";
        case AST_FUNC_CALL:          return "FUNC_CALL";
        case AST_LENGTH_OF:          return "LENGTH_OF";
        case AST_BINOP:              return "BINOP";
        case AST_UNOP:               return "UNOP";
        case AST_COND_REL:           return "COND_REL";
        case AST_COND_LOGIC:         return "COND_LOGIC";
        case AST_COND_NOT:           return "COND_NOT";
        case AST_COND_CLASS:         return "COND_CLASS";
        case AST_LIST:               return "LIST";
        default:                     return "UNKNOWN";
    }
}

ASTNode *ast_new_ex(ASTKind kind, const char *text, const char *extra, int ival, int seq_num, int col) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!n) {
        fprintf(stderr, "[S] ERROR: Out of memory.\n");
        exit(EXIT_FAILURE);
    }
    n->kind = kind;
    n->seq_num = seq_num;
    n->col = col;
    n->ival = ival;
    n->text = xstrdup(text);
    n->extra = xstrdup(extra);
    return n;
}

ASTNode *ast_new(ASTKind kind, const char *text, int seq_num, int col) {
    return ast_new_ex(kind, text, NULL, 0, seq_num, col);
}

ASTNode *ast_add(ASTNode *parent, ASTNode *child) {
    if (!parent || !child) return parent;
    if (parent->child_count >= parent->child_cap) {
        size_t new_cap = parent->child_cap ? parent->child_cap * 2 : 4;
        ASTNode **next = (ASTNode **)realloc(parent->children, new_cap * sizeof(ASTNode *));
        if (!next) {
            fprintf(stderr, "[S] ERROR: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        parent->children = next;
        parent->child_cap = new_cap;
    }
    parent->children[parent->child_count++] = child;
    return parent;
}

ASTNode *ast_adopt_children(ASTNode *parent, ASTNode *list_node) {
    if (!parent || !list_node) return parent;
    for (size_t i = 0; i < list_node->child_count; i++) {
        ast_add(parent, list_node->children[i]);
    }
    list_node->child_count = 0;
    ast_free(list_node);
    return parent;
}

void ast_set_text(ASTNode *node, const char *text) {
    if (!node) return;
    free(node->text);
    node->text = xstrdup(text);
}

void ast_set_extra(ASTNode *node, const char *extra) {
    if (!node) return;
    free(node->extra);
    node->extra = xstrdup(extra);
}

void ast_print(const ASTNode *node, FILE *out, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; i++) fputs("  ", out);
    fprintf(out, "%s", ast_kind_name(node->kind));
    if (node->kind == AST_DATA_ITEM && node->ival > 0) {
        fprintf(out, " (level=%02d)", node->ival);
    } else if (node->ival != 0) {
        fprintf(out, " (%d)", node->ival);
    }
    if (node->text)  fprintf(out, " [%s]", node->text);
    if (node->extra) fprintf(out, " <%s>", node->extra);
    if (node->seq_num > 0) fprintf(out, " @%d:%d", node->seq_num, node->col);
    fputc('\n', out);

    for (size_t i = 0; i < node->child_count; i++) {
        ast_print(node->children[i], out, indent + 1);
    }
}

void ast_free(ASTNode *node) {
    if (!node) return;
    for (size_t i = 0; i < node->child_count; i++) {
        ast_free(node->children[i]);
    }
    free(node->children);
    free(node->text);
    free(node->extra);
    free(node);
}
