#include "sema.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

struct SemaCtx {
    SymTab *tab;
    const std::string &filename;

    void error(int seq, int col, const char *fmt, ...) const {
        tab->errors++;
        std::fprintf(stderr, "[E] Semantic Error (%s, Seq %06d, Col %d): ",
                     filename.empty() ? "<input>" : filename.c_str(), seq, col);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(stderr, fmt, ap);
        va_end(ap);
        std::fputc('\n', stderr);
    }

    void warn(int seq, int col, const char *fmt, ...) const {
        tab->warnings++;
        std::fprintf(stderr, "[W] Semantic Warning (%s, Seq %06d, Col %d): ",
                     filename.empty() ? "<input>" : filename.c_str(), seq, col);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(stderr, fmt, ap);
        va_end(ap);
        std::fputc('\n', stderr);
    }
};

const char *data_kind_to_string(DataKind kind) {
    switch (kind) {
        case DATA_GROUP:          return "GROUP";
        case DATA_ALPHA:          return "PIC_A";
        case DATA_NUMERIC:        return "PIC_9";
        case DATA_NUMERIC_EDITED: return "PIC_9_ED";
        case DATA_ALPHANUMERIC:   return "PIC_X";
        case DATA_INDEX:          return "INDEX";
        case DATA_COND:           return "COND_88";
        default:                  return "UNKNOWN";
    }
}

int SymTab::find_file(const std::string &name) const {
    for (size_t i = 0; i < files.size(); i++) {
        if (files[i].name == name) return (int)i;
    }
    return -1;
}

int SymTab::find_proc(const std::string &name) const {
    for (size_t i = 0; i < procs.size(); i++) {
        if (procs[i].name == name) return (int)i;
    }
    return -1;
}

static int get_or_add_file_sym(SymTab &tab, const std::string &name) {
    int idx = tab.find_file(name);
    if (idx >= 0) return idx;
    FileSym f;
    f.name = name;
    tab.files.push_back(f);
    return (int)tab.files.size() - 1;
}

static void add_proc_sym(SemaCtx &ctx, const std::string &name, bool is_section, int seq, int col) {
    if (ctx.tab->find_proc(name) >= 0) {
        ctx.error(seq, col, "Duplicate procedure/paragraph name \"%s\".", name.c_str());
        return;
    }
    ProcSym p;
    p.name = name;
    p.is_section = is_section;
    p.seq_num = seq;
    p.col = col;
    ctx.tab->procs.push_back(p);
}

/* Classify a COBOL PICTURE string into PIC A, PIC 9, PIC 9 edited, or PIC X */
static DataKind classify_pic(const std::string &pic) {
    if (pic.empty()) return DATA_GROUP;
    bool has_x = false, has_a = false, has_9 = false, has_edit = false;

    for (size_t i = 0; i < pic.size(); i++) {
        char c = (char)std::toupper((unsigned char)pic[i]);
        if (c == '(') {
            while (i < pic.size() && pic[i] != ')') i++;
            continue;
        }
        if (c == 'X') has_x = true;
        else if (c == 'A') has_a = true;
        else if (c == '9') has_9 = true;
        else if (c == 'S' || c == 'V' || c == 'P') { /* numeric sign/decimal */ }
        else if (std::strchr("Z*+-.,$/B0CRD", c) != nullptr) {
            has_edit = true;
        }
    }

    if (has_x) return DATA_ALPHANUMERIC;
    if (has_a && !has_9 && !has_edit) return DATA_ALPHA;
    if (has_a) return DATA_ALPHANUMERIC;
    if (has_edit) return DATA_NUMERIC_EDITED;
    if (has_9) return DATA_NUMERIC;
    return DATA_ALPHANUMERIC;
}

static bool is_special_register(const std::string &name) {
    return name == "RETURN-CODE" || name == "TALLY" || name == "WHEN-COMPILED" ||
           name == "SORT-RETURN" || name == "DEBUG-ITEM" || name == "LINAGE-COUNTER";
}

static bool is_numeric_string_literal(const std::string &lit) {
    size_t start = 0, len = lit.size();
    if (len < 2) return false;
    if ((lit[0] == '\'' || lit[0] == '"') && lit[len - 1] == lit[0]) {
        start = 1;
        len -= 2;
    }
    if (len == 0) return false;
    size_t i = start;
    if (lit[i] == '+' || lit[i] == '-') i++;
    bool has_digit = false, has_sep = false;
    for (; i < start + len; i++) {
        if (std::isdigit((unsigned char)lit[i])) {
            has_digit = true;
        } else if ((lit[i] == '.' || lit[i] == ',') && !has_sep) {
            has_sep = true;
        } else {
            return false;
        }
    }
    return has_digit;
}

/* =========================================================================
 * Pass 1: Collect SELECT, FD/SD, DATA_ITEMs, INDEXED BY, SECTIONs/PARAGRAPHs
 * ========================================================================= */

static void collect_env(SemaCtx &ctx, const ASTNode *node) {
    if (!node) return;
    if (node->kind == AST_SELECT && !node->text.empty()) {
        int idx = get_or_add_file_sym(*ctx.tab, node->text);
        FileSym &f = ctx.tab->files[idx];
        if (f.has_select) {
            ctx.error(node->seq_num, node->col,
                      "Duplicate SELECT declaration for file \"%s\".", node->text.c_str());
        }
        f.has_select = true;
        f.select_seq = node->seq_num;
        f.select_col = node->col;
        return;
    }
    for (size_t i = 0; i < node->children.size(); i++) {
        collect_env(ctx, node->children[i]);
    }
}

static void collect_data_items(SemaCtx &ctx, const ASTNode *container, int fd_idx) {
    struct LevelFrame { int level; int sym_idx; };
    std::vector<LevelFrame> stack;

    for (size_t i = 0; i < container->children.size(); i++) {
        const ASTNode *child = container->children[i];
        if (child->kind != AST_DATA_ITEM) continue;

        int lvl = child->ival;
        int parent_idx = -1;

        if (lvl == 1 || lvl == 77) {
            stack.clear();
        } else {
            while (!stack.empty() && stack.back().level >= lvl) stack.pop_back();
            if (!stack.empty()) parent_idx = stack.back().sym_idx;
        }

        std::string name = child->text.empty() ? "FILLER" : child->text;
        if (name != "FILLER") {
            for (size_t s = 0; s < ctx.tab->data.size(); s++) {
                const DataSym &existing = ctx.tab->data[s];
                if (existing.parent_idx == parent_idx && existing.name == name) {
                    ctx.error(child->seq_num, child->col,
                              "Duplicate identifier \"%s\" in the same scope (previous at Seq %06d).",
                              name.c_str(), existing.seq_num);
                    break;
                }
            }
        }

        DataSym sym;
        sym.name = name;
        sym.level = lvl;
        sym.parent_idx = parent_idx;
        sym.fd_idx = fd_idx;
        sym.seq_num = child->seq_num;
        sym.col = child->col;
        sym.kind = (lvl == 88) ? DATA_COND : DATA_GROUP;

        if (const ASTNode *pic_cl = child->find_clause("PIC")) {
            if (!pic_cl->extra.empty()) {
                sym.pic = pic_cl->extra;
                sym.kind = classify_pic(sym.pic);
            }
        }
        if (const ASTNode *occ_cl = child->find_clause("OCCURS")) {
            if (const ASTNode *ix_cl = occ_cl->find_clause("INDEXED-BY")) {
                for (size_t m = 0; m < ix_cl->children.size(); m++) {
                    const ASTNode *ix = ix_cl->children[m];
                    if (ix->kind == AST_DATA_REF && !ix->text.empty()) {
                        DataSym isym;
                        isym.name = ix->text;
                        isym.kind = DATA_INDEX;
                        isym.seq_num = ix->seq_num;
                        isym.col = ix->col;
                        ctx.tab->data.push_back(isym);
                    }
                }
            }
        }

        int sym_idx = (int)ctx.tab->data.size();
        ctx.tab->data.push_back(sym);
        if (lvl != 77 && lvl != 88 && lvl != 66) {
            LevelFrame lf = {lvl, sym_idx};
            stack.push_back(lf);
        }
    }
}

static void collect_data_div(SemaCtx &ctx, const ASTNode *dd) {
    if (!dd) return;
    for (size_t i = 0; i < dd->children.size(); i++) {
        const ASTNode *sec = dd->children[i];
        if (sec->kind != AST_DATA_SECTION) continue;
        if (sec->text == "FILE") {
            for (size_t j = 0; j < sec->children.size(); j++) {
                const ASTNode *fd = sec->children[j];
                if (fd->kind != AST_FD || fd->text.empty()) continue;
                int fidx = get_or_add_file_sym(*ctx.tab, fd->text);
                FileSym &f = ctx.tab->files[fidx];
                if (f.has_fd) {
                    ctx.error(fd->seq_num, fd->col,
                              "Duplicate FD declaration for file \"%s\".", fd->text.c_str());
                }
                f.has_fd = true;
                f.fd_seq = fd->seq_num;
                f.fd_col = fd->col;
                collect_data_items(ctx, fd, fidx);
            }
        } else {
            collect_data_items(ctx, sec, -1);
        }
    }
}

static void collect_procs(SemaCtx &ctx, const ASTNode *pd) {
    if (!pd) return;
    for (size_t i = 0; i < pd->children.size(); i++) {
        const ASTNode *el = pd->children[i];
        if (el->kind == AST_PROC_SECTION && !el->text.empty()) {
            add_proc_sym(ctx, el->text, true, el->seq_num, el->col);
        } else if (el->kind == AST_PARAGRAPH && !el->text.empty()) {
            add_proc_sym(ctx, el->text, false, el->seq_num, el->col);
        }
    }
}

/* =========================================================================
 * Pass 2: Semantic Validation (Section 4 Rules 1-4)
 * ========================================================================= */

static void check_node(SemaCtx &ctx, const ASTNode *node);

static bool sym_matches_qualifiers(const SymTab &tab, int sym_idx,
                                   const std::vector<std::string> &quals) {
    size_t qi = 0;
    int p = tab.data[sym_idx].parent_idx;
    while (p >= 0 && qi < quals.size()) {
        if (tab.data[p].name == quals[qi]) qi++;
        p = tab.data[p].parent_idx;
    }
    return qi == quals.size();
}

static bool data_ref_has_refmod(const ASTNode *ref) {
    for (size_t i = 0; i < ref->children.size(); i++) {
        if (ref->children[i]->kind == AST_REFMOD) return true;
    }
    return false;
}

static int resolve_data_ref(SemaCtx &ctx, const ASTNode *ref) {
    if (!ref || ref->kind != AST_DATA_REF || ref->text.empty()) return -1;

    std::vector<std::string> quals;
    for (size_t i = 0; i < ref->children.size(); i++) {
        const ASTNode *ch = ref->children[i];
        if (ch->kind == AST_DATA_REF && !ch->text.empty()) quals.push_back(ch->text);
        else check_node(ctx, ch);
    }

    if (quals.empty() && is_special_register(ref->text)) return -2;

    int match_idx = -1;
    int matches = 0;
    for (size_t i = 0; i < ctx.tab->data.size(); i++) {
        if (ctx.tab->data[i].name != ref->text) continue;
        if (quals.empty() || sym_matches_qualifiers(*ctx.tab, (int)i, quals)) {
            match_idx = (int)i;
            matches++;
        }
    }

    if (matches == 0) {
        if (!quals.empty()) {
            ctx.error(ref->seq_num, ref->col, "Undeclared qualified identifier \"%s OF %s\".",
                      ref->text.c_str(), quals[0].c_str());
        } else {
            ctx.error(ref->seq_num, ref->col, "Undeclared identifier \"%s\".", ref->text.c_str());
        }
        return -1;
    }
    if (matches > 1) {
        ctx.error(ref->seq_num, ref->col,
                  "Ambiguous identifier \"%s\" (%d declarations); qualify with OF/IN.",
                  ref->text.c_str(), matches);
        return -1;
    }
    return match_idx;
}

static void check_file_ref(SemaCtx &ctx, const ASTNode *ref, const char *verb) {
    if (!ref || ref->kind != AST_DATA_REF || ref->text.empty()) return;
    int fidx = ctx.tab->find_file(ref->text);
    if (fidx < 0) {
        ctx.error(ref->seq_num, ref->col,
                  "File \"%s\" used in %s is not declared in FILE-CONTROL or FD.",
                  ref->text.c_str(), verb);
        return;
    }
    const FileSym &f = ctx.tab->files[fidx];
    if (!f.has_select) {
        ctx.error(ref->seq_num, ref->col,
                  "File \"%s\" used in %s has no SELECT declaration in FILE-CONTROL.",
                  ref->text.c_str(), verb);
    }
    if (!f.has_fd) {
        ctx.error(ref->seq_num, ref->col,
                  "File \"%s\" used in %s has no FD declaration in DATA DIVISION.",
                  ref->text.c_str(), verb);
    }
}

static void check_record_ref(SemaCtx &ctx, const ASTNode *ref, const char *verb) {
    int didx = resolve_data_ref(ctx, ref);
    if (didx < 0) return;
    const DataSym &sym = ctx.tab->data[didx];
    if (sym.fd_idx < 0) {
        ctx.error(ref->seq_num, ref->col,
                  "Record \"%s\" used in %s is not declared under an FD in FILE SECTION.",
                  sym.name.c_str(), verb);
        return;
    }
    const FileSym &f = ctx.tab->files[sym.fd_idx];
    if (!f.has_select || !f.has_fd) {
        ctx.error(ref->seq_num, ref->col,
                  "Record \"%s\" in %s belongs to file \"%s\" without complete SELECT/FD association.",
                  sym.name.c_str(), verb, f.name.c_str());
    }
}

static void check_move(SemaCtx &ctx, const ASTNode *node) {
    if (node->children.size() < 2) return;
    const ASTNode *src = node->children[0];

    DataKind src_kind = DATA_UNKNOWN;
    const char *src_desc = src->text.empty() ? "<expr>" : src->text.c_str();
    bool src_non_numeric_str = false;

    if (src->kind == AST_DATA_REF) {
        int sidx = resolve_data_ref(ctx, src);
        if (sidx == -2) src_kind = DATA_NUMERIC;
        else if (sidx >= 0) {
            src_kind = data_ref_has_refmod(src) ? DATA_ALPHANUMERIC : ctx.tab->data[sidx].kind;
        }
    } else if (src->kind == AST_LITERAL_NUM) {
        src_kind = DATA_NUMERIC;
    } else if (src->kind == AST_LITERAL_STR) {
        src_kind = DATA_ALPHANUMERIC;
        src_non_numeric_str = !is_numeric_string_literal(src->text);
    } else if (src->kind == AST_FIGURATIVE) {
        src_kind = (src->text.rfind("ZERO", 0) == 0) ? DATA_NUMERIC : DATA_ALPHANUMERIC;
    } else {
        check_node(ctx, src);
    }

    for (size_t i = 1; i < node->children.size(); i++) {
        const ASTNode *dst = node->children[i];
        int didx = resolve_data_ref(ctx, dst);
        if (didx < 0) continue;

        DataKind dst_kind = data_ref_has_refmod(dst) ? DATA_ALPHANUMERIC : ctx.tab->data[didx].kind;
        bool dst_is_num = (dst_kind == DATA_NUMERIC || dst_kind == DATA_NUMERIC_EDITED);

        if (src_kind == DATA_ALPHA && dst_is_num) {
            ctx.error(dst->seq_num, dst->col,
                      "Incompatible MOVE from alphabetic (PIC A) \"%s\" to numeric \"%s\" (%s).",
                      src_desc, dst->text.c_str(), ctx.tab->data[didx].pic.c_str());
        } else if (src_non_numeric_str && dst_is_num) {
            ctx.error(dst->seq_num, dst->col,
                      "Incompatible MOVE from non-numeric literal %s to numeric \"%s\" (%s).",
                      src_desc, dst->text.c_str(), ctx.tab->data[didx].pic.c_str());
        } else if (src_kind == DATA_ALPHANUMERIC && src->kind == AST_DATA_REF && dst_is_num) {
            ctx.warn(dst->seq_num, dst->col,
                     "Moving alphanumeric \"%s\" to numeric \"%s\" (%s).",
                     src_desc, dst->text.c_str(), ctx.tab->data[didx].pic.c_str());
        } else if ((src_kind == DATA_NUMERIC || src_kind == DATA_NUMERIC_EDITED) && dst_kind == DATA_ALPHA) {
            ctx.warn(dst->seq_num, dst->col,
                     "Moving numeric \"%s\" to alphabetic (PIC A) \"%s\".",
                     src_desc, dst->text.c_str());
        }
    }
}

static void check_node(SemaCtx &ctx, const ASTNode *node) {
    if (!node) return;

    switch (node->kind) {
        case AST_DATA_REF:
            resolve_data_ref(ctx, node);
            return;

        case AST_STMT_MOVE:
            check_move(ctx, node);
            return;

        case AST_STMT_PERFORM:
            if (!node->text.empty() && node->text != "INLINE" && ctx.tab->find_proc(node->text) < 0) {
                ctx.error(node->seq_num, node->col,
                          "Undeclared PERFORM target \"%s\".", node->text.c_str());
            }
            for (size_t i = 0; i < node->children.size(); i++) {
                const ASTNode *ch = node->children[i];
                if (ch->is_clause("THRU") && !ch->extra.empty()) {
                    if (ctx.tab->find_proc(ch->extra) < 0) {
                        ctx.error(ch->seq_num, ch->col,
                                  "Undeclared PERFORM THRU target \"%s\".", ch->extra.c_str());
                    }
                } else {
                    check_node(ctx, ch);
                }
            }
            return;

        case AST_STMT_GOTO:
            if (!node->text.empty() && ctx.tab->find_proc(node->text) < 0) {
                ctx.error(node->seq_num, node->col,
                          "Undeclared GO TO target \"%s\".", node->text.c_str());
            }
            if (!node->extra.empty() && ctx.tab->find_proc(node->extra) < 0) {
                ctx.error(node->seq_num, node->col,
                          "Undeclared ALTER target \"%s\".", node->extra.c_str());
            }
            return;

        case AST_STMT_OPEN:
            for (size_t i = 0; i < node->children.size(); i++) {
                const ASTNode *grp = node->children[i];
                for (size_t j = 0; j < grp->children.size(); j++) {
                    check_file_ref(ctx, grp->children[j], "OPEN");
                }
            }
            return;

        case AST_STMT_CLOSE:
            for (size_t i = 0; i < node->children.size(); i++) {
                check_file_ref(ctx, node->children[i], "CLOSE");
            }
            return;

        case AST_STMT_READ:
        case AST_STMT_DELETE:
        case AST_STMT_START:
        case AST_STMT_RETURN: {
            const char *verb = ast_kind_name(node->kind);
            if (!node->children.empty()) check_file_ref(ctx, node->children[0], verb);
            for (size_t i = 1; i < node->children.size(); i++) check_node(ctx, node->children[i]);
            return;
        }

        case AST_STMT_WRITE:
        case AST_STMT_REWRITE:
        case AST_STMT_RELEASE: {
            const char *verb = ast_kind_name(node->kind);
            if (!node->children.empty()) check_record_ref(ctx, node->children[0], verb);
            for (size_t i = 1; i < node->children.size(); i++) check_node(ctx, node->children[i]);
            return;
        }

        case AST_STMT_SORT:
        case AST_STMT_MERGE:
            if (!node->children.empty()) {
                check_file_ref(ctx, node->children[0], ast_kind_name(node->kind));
            }
            return;

        default:
            for (size_t i = 0; i < node->children.size(); i++) check_node(ctx, node->children[i]);
            return;
    }
}

bool sema_analyze(const ASTNode *root, const std::string &filename, SymTab &out_tab) {
    if (!root) return false;
    out_tab = SymTab();
    SemaCtx ctx = {&out_tab, filename};

    const ASTNode *env_div = nullptr;
    const ASTNode *data_div = nullptr;
    const ASTNode *proc_div = nullptr;

    for (size_t i = 0; i < root->children.size(); i++) {
        const ASTNode *ch = root->children[i];
        if (ch->kind == AST_ENVIRONMENT) env_div = ch;
        else if (ch->kind == AST_DATA_DIVISION) data_div = ch;
        else if (ch->kind == AST_PROCEDURE_DIVISION) proc_div = ch;
    }

    collect_env(ctx, env_div);
    collect_data_div(ctx, data_div);
    collect_procs(ctx, proc_div);

    for (size_t i = 0; i < out_tab.files.size(); i++) {
        const FileSym &f = out_tab.files[i];
        if (f.has_fd && !f.has_select) {
            ctx.error(f.fd_seq, f.fd_col, "FD \"%s\" has no matching SELECT in FILE-CONTROL.", f.name.c_str());
        } else if (f.has_select && !f.has_fd) {
            ctx.warn(f.select_seq, f.select_col, "SELECT \"%s\" has no matching FD in DATA DIVISION.", f.name.c_str());
        }
    }

    if (env_div) {
        for (size_t i = 0; i < env_div->children.size(); i++) {
            const ASTNode *sec = env_div->children[i];
            if (sec->kind != AST_FILE_CONTROL) continue;
            for (size_t j = 0; j < sec->children.size(); j++) {
                const ASTNode *sel = sec->children[j];
                if (sel->kind != AST_SELECT) continue;
                for (size_t k = 0; k < sel->children.size(); k++) {
                    const ASTNode *cl = sel->children[k];
                    if (cl->is_clause("RECORD-KEY") || cl->is_clause("ALTERNATE-KEY") ||
                        cl->is_clause("FILE-STATUS") || cl->is_clause("RELATIVE-KEY")) {
                        for (size_t m = 0; m < cl->children.size(); m++) check_node(ctx, cl->children[m]);
                    }
                }
            }
        }
    }

    if (proc_div) {
        for (size_t i = 0; i < proc_div->children.size(); i++) {
            const ASTNode *ch = proc_div->children[i];
            if (ch->kind == AST_PROC_SECTION || ch->kind == AST_PARAGRAPH) continue;
            check_node(ctx, ch);
        }
    }

    return out_tab.errors == 0;
}

void symtab_print(const SymTab &tab, FILE *out) {
    if (!out) return;
    std::fprintf(out, "=== FILES (%zu) ===\n", tab.files.size());
    for (size_t i = 0; i < tab.files.size(); i++) {
        const FileSym &f = tab.files[i];
        std::fprintf(out, "  %-24s SELECT=%s  FD=%s\n",
                     f.name.c_str(), f.has_select ? "yes" : "no ", f.has_fd ? "yes" : "no ");
    }

    std::fprintf(out, "=== DATA ITEMS (%zu) ===\n", tab.data.size());
    for (size_t i = 0; i < tab.data.size(); i++) {
        const DataSym &d = tab.data[i];
        const char *parent = (d.parent_idx >= 0) ? tab.data[d.parent_idx].name.c_str() : "-";
        std::fprintf(out, "  %02d %-28s %-8s PIC=%-16s PARENT=%-20s @%d:%d\n",
                     d.level, d.name.c_str(), data_kind_to_string(d.kind),
                     d.pic.empty() ? "-" : d.pic.c_str(), parent, d.seq_num, d.col);
    }

    std::fprintf(out, "=== PROCEDURES (%zu) ===\n", tab.procs.size());
    for (size_t i = 0; i < tab.procs.size(); i++) {
        const ProcSym &p = tab.procs[i];
        std::fprintf(out, "  %-28s %-9s @%d:%d\n",
                     p.name.c_str(), p.is_section ? "SECTION" : "PARAGRAPH", p.seq_num, p.col);
    }
}
