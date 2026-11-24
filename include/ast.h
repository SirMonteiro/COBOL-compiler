#ifndef AST_H
#define AST_H

#include <cstdio>
#include <string>
#include <vector>

enum ASTKind {
    AST_PROGRAM,
    AST_IDENTIFICATION,
    AST_ID_ENTRY,
    AST_ENVIRONMENT,
    AST_CONFIGURATION,
    AST_SPECIAL_NAMES,
    AST_FILE_CONTROL,
    AST_SELECT,
    AST_DATA_DIVISION,
    AST_DATA_SECTION,
    AST_FD,
    AST_DATA_ITEM,
    AST_COPY,
    AST_PROCEDURE_DIVISION,
    AST_PROC_SECTION,
    AST_PARAGRAPH,
    AST_SENTENCE,

    /* Statements */
    AST_STMT_DISPLAY,
    AST_STMT_ACCEPT,
    AST_STMT_MOVE,
    AST_STMT_ADD,
    AST_STMT_SUBTRACT,
    AST_STMT_MULTIPLY,
    AST_STMT_DIVIDE,
    AST_STMT_COMPUTE,
    AST_STMT_IF,
    AST_STMT_EVALUATE,
    AST_WHEN,
    AST_STMT_PERFORM,
    AST_STMT_OPEN,
    AST_STMT_CLOSE,
    AST_STMT_READ,
    AST_STMT_WRITE,
    AST_STMT_REWRITE,
    AST_STMT_DELETE,
    AST_STMT_START,
    AST_STMT_GOTO,
    AST_STMT_CALL,
    AST_STMT_CANCEL,
    AST_STMT_STOP_RUN,
    AST_STMT_GOBACK,
    AST_STMT_EXIT,
    AST_STMT_CONTINUE,
    AST_STMT_NEXT_SENTENCE,
    AST_STMT_INITIALIZE,
    AST_STMT_INSPECT,
    AST_STMT_STRING,
    AST_STMT_UNSTRING,
    AST_STMT_SET,
    AST_STMT_SEARCH,
    AST_STMT_SORT,
    AST_STMT_MERGE,
    AST_STMT_RELEASE,
    AST_STMT_RETURN,

    /* Clauses / Expressions / References */
    AST_CLAUSE,
    AST_DATA_REF,
    AST_REFMOD,
    AST_SUBSCRIPT,
    AST_LITERAL_NUM,
    AST_LITERAL_STR,
    AST_FIGURATIVE,
    AST_FUNC_CALL,
    AST_LENGTH_OF,
    AST_BINOP,
    AST_UNOP,
    AST_COND_REL,
    AST_COND_LOGIC,
    AST_COND_NOT,
    AST_COND_CLASS,
    AST_LIST
};

struct ASTNode {
    ASTKind kind = AST_PROGRAM;
    int seq_num = 0;
    int col = 0;
    int ival = 0;
    std::string text;
    std::string extra;
    std::vector<ASTNode *> children;

    ~ASTNode() {
        for (size_t i = 0; i < children.size(); i++) delete children[i];
    }

    bool is_clause(const std::string &name) const;
    const ASTNode *find_clause(const std::string &name, size_t start = 0) const;
};

const char *ast_kind_name(ASTKind kind);

ASTNode *ast_new(ASTKind kind, const char *text, int seq_num, int col);
ASTNode *ast_new(ASTKind kind, const std::string &text, int seq_num, int col);
ASTNode *ast_new_ex(ASTKind kind, const char *text, const char *extra, int ival, int seq_num, int col);
ASTNode *ast_add(ASTNode *parent, ASTNode *child);
ASTNode *ast_adopt_children(ASTNode *parent, ASTNode *list_node);
void     ast_set_text(ASTNode *node, const char *text);
void     ast_set_extra(ASTNode *node, const char *extra);

void     ast_print(const ASTNode *node, FILE *out, int indent);
void     ast_free(ASTNode *node);

#endif /* AST_H */
