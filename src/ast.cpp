#include "ast.h"

bool ASTNode::is_clause(const std::string &name) const {
    return kind == AST_CLAUSE && text == name;
}

const ASTNode *ASTNode::find_clause(const std::string &name, size_t start) const {
    for (size_t i = start; i < children.size(); i++) {
        if (children[i] && children[i]->is_clause(name)) return children[i];
    }
    return nullptr;
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
    ASTNode *n = new ASTNode();
    n->kind = kind;
    n->seq_num = seq_num;
    n->col = col;
    n->ival = ival;
    if (text) n->text = text;
    if (extra) n->extra = extra;
    return n;
}

ASTNode *ast_new(ASTKind kind, const char *text, int seq_num, int col) {
    return ast_new_ex(kind, text, nullptr, 0, seq_num, col);
}

ASTNode *ast_new(ASTKind kind, const std::string &text, int seq_num, int col) {
    return ast_new_ex(kind, text.empty() ? nullptr : text.c_str(), nullptr, 0, seq_num, col);
}

ASTNode *ast_add(ASTNode *parent, ASTNode *child) {
    if (parent && child) parent->children.push_back(child);
    return parent;
}

ASTNode *ast_adopt_children(ASTNode *parent, ASTNode *list_node) {
    if (!parent || !list_node) return parent;
    for (ASTNode *ch : list_node->children) parent->children.push_back(ch);
    list_node->children.clear();
    delete list_node;
    return parent;
}

void ast_set_extra(ASTNode *node, const char *extra) {
    if (!node) return;
    node->extra = extra ? extra : "";
}

void ast_print(const ASTNode *node, FILE *out, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; i++) std::fputs("  ", out);
    std::fprintf(out, "%s", ast_kind_name(node->kind));
    if (node->kind == AST_DATA_ITEM && node->ival > 0) {
        std::fprintf(out, " (level=%02d)", node->ival);
    } else if (node->ival != 0) {
        std::fprintf(out, " (%d)", node->ival);
    }
    if (!node->text.empty())  std::fprintf(out, " [%s]", node->text.c_str());
    if (!node->extra.empty()) std::fprintf(out, " <%s>", node->extra.c_str());
    if (node->seq_num > 0) std::fprintf(out, " @%d:%d", node->seq_num, node->col);
    std::fputc('\n', out);

    for (const ASTNode *ch : node->children) ast_print(ch, out, indent + 1);
}

void ast_free(ASTNode *node) {
    delete node;
}
