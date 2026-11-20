#include "sema.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SymTab *tab;
    const char *filename;
} SemaCtx;

static void sema_error(SemaCtx *ctx, int seq, int col, const char *fmt, ...) {
    ctx->tab->errors++;
    fprintf(stderr, "[E] Semantic Error (%s, Seq %06d, Col %d): ",
            ctx->filename ? ctx->filename : "<input>", seq, col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void sema_warn(SemaCtx *ctx, int seq, int col, const char *fmt, ...) {
    ctx->tab->warnings++;
    fprintf(stderr, "[W] Semantic Warning (%s, Seq %06d, Col %d): ",
            ctx->filename ? ctx->filename : "<input>", seq, col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

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

void symtab_init(SymTab *tab) {
    memset(tab, 0, sizeof(*tab));
}

void symtab_free(SymTab *tab) {
    if (!tab) return;
    free(tab->data);
    free(tab->files);
    free(tab->procs);
    memset(tab, 0, sizeof(*tab));
}

static int add_data_sym(SymTab *tab, const DataSym *sym) {
    if (tab->data_count >= tab->data_cap) {
        size_t cap = tab->data_cap ? tab->data_cap * 2 : 64;
        DataSym *next = (DataSym *)realloc(tab->data, cap * sizeof(DataSym));
        if (!next) {
            fprintf(stderr, "[S] ERROR: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        tab->data = next;
        tab->data_cap = cap;
    }
    tab->data[tab->data_count] = *sym;
    return (int)(tab->data_count++);
}

static int find_file_sym(const SymTab *tab, const char *name) {
    for (size_t i = 0; i < tab->file_count; i++) {
        if (strcmp(tab->files[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int get_or_add_file_sym(SymTab *tab, const char *name) {
    int idx = find_file_sym(tab, name);
    if (idx >= 0) return idx;
    if (tab->file_count >= tab->file_cap) {
        size_t cap = tab->file_cap ? tab->file_cap * 2 : 16;
        FileSym *next = (FileSym *)realloc(tab->files, cap * sizeof(FileSym));
        if (!next) {
            fprintf(stderr, "[S] ERROR: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        tab->files = next;
        tab->file_cap = cap;
    }
    FileSym *f = &tab->files[tab->file_count];
    memset(f, 0, sizeof(*f));
    snprintf(f->name, sizeof(f->name), "%s", name);
    return (int)(tab->file_count++);
}

static int find_proc_sym(const SymTab *tab, const char *name) {
    for (size_t i = 0; i < tab->proc_count; i++) {
        if (strcmp(tab->procs[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static void add_proc_sym(SemaCtx *ctx, const char *name, bool is_section, int seq, int col) {
    SymTab *tab = ctx->tab;
    if (find_proc_sym(tab, name) >= 0) {
        sema_error(ctx, seq, col, "Duplicate procedure/paragraph name \"%s\".", name);
        return;
    }
    if (tab->proc_count >= tab->proc_cap) {
        size_t cap = tab->proc_cap ? tab->proc_cap * 2 : 32;
        ProcSym *next = (ProcSym *)realloc(tab->procs, cap * sizeof(ProcSym));
        if (!next) {
            fprintf(stderr, "[S] ERROR: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        tab->procs = next;
        tab->proc_cap = cap;
    }
    ProcSym *p = &tab->procs[tab->proc_count++];
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->is_section = is_section;
    p->seq_num = seq;
    p->col = col;
}

/* Classify a COBOL PICTURE string into PIC A, PIC 9, PIC 9 edited, or PIC X */
static DataKind classify_pic(const char *pic) {
    if (!pic || !pic[0]) return DATA_GROUP;
    bool has_x = false;
    bool has_a = false;
    bool has_9 = false;
    bool has_edit = false;

    for (size_t i = 0; pic[i]; i++) {
        char c = (char)toupper((unsigned char)pic[i]);
        if (c == '(') {
            while (pic[i] && pic[i] != ')') i++;
            if (!pic[i]) break;
            continue;
        }
        if (c == 'X') has_x = true;
        else if (c == 'A') has_a = true;
        else if (c == '9') has_9 = true;
        else if (c == 'S' || c == 'V' || c == 'P') { /* numeric sign/decimal */ }
        else if (c == 'Z' || c == '*' || c == '+' || c == '-' ||
                 c == '.' || c == ',' || c == '$' || c == '/' ||
                 c == 'B' || c == '0' || c == 'C' || c == 'R' || c == 'D') {
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

static bool is_special_register(const char *name) {
    return strcmp(name, "RETURN-CODE") == 0 ||
           strcmp(name, "TALLY") == 0 ||
           strcmp(name, "WHEN-COMPILED") == 0 ||
           strcmp(name, "SORT-RETURN") == 0 ||
           strcmp(name, "DEBUG-ITEM") == 0 ||
           strcmp(name, "LINAGE-COUNTER") == 0;
}

static bool is_numeric_string_literal(const char *lit) {
    if (!lit) return false;
    size_t len = strlen(lit);
    if (len < 2) return false;
    if ((lit[0] == '\'' || lit[0] == '"') && lit[len - 1] == lit[0]) {
        lit++;
        len -= 2;
    }
    if (len == 0) return false;
    size_t i = 0;
    if (lit[i] == '+' || lit[i] == '-') i++;
    bool has_digit = false;
    bool has_sep = false;
    for (; i < len; i++) {
        if (isdigit((unsigned char)lit[i])) {
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

static void collect_env(SemaCtx *ctx, const ASTNode *node) {
    if (!node) return;
    if (node->kind == AST_SELECT && node->text) {
        int idx = get_or_add_file_sym(ctx->tab, node->text);
        FileSym *f = &ctx->tab->files[idx];
        if (f->has_select) {
            sema_error(ctx, node->seq_num, node->col,
                       "Duplicate SELECT declaration for file \"%s\".", node->text);
        }
        f->has_select = true;
        f->select_seq = node->seq_num;
        f->select_col = node->col;
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        collect_env(ctx, node->children[i]);
    }
}

typedef struct {
    int level;
    int sym_idx;
} LevelFrame;

static void collect_data_items(SemaCtx *ctx, const ASTNode *container, int fd_idx) {
    LevelFrame stack[64];
    int top = 0;

    for (size_t i = 0; i < container->child_count; i++) {
        const ASTNode *child = container->children[i];
        if (child->kind != AST_DATA_ITEM) continue;

        int lvl = child->ival;
        int parent_idx = -1;

        if (lvl == 1 || lvl == 77) {
            top = 0;
            parent_idx = -1;
        } else {
            while (top > 0 && stack[top - 1].level >= lvl) top--;
            parent_idx = (top > 0) ? stack[top - 1].sym_idx : -1;
        }

        const char *name = child->text ? child->text : "FILLER";
        bool is_filler = (strcmp(name, "FILLER") == 0);

        /* Rule 2: check duplicate name in the exact same parent scope */
        if (!is_filler) {
            for (size_t s = 0; s < ctx->tab->data_count; s++) {
                const DataSym *existing = &ctx->tab->data[s];
                if (existing->parent_idx == parent_idx && strcmp(existing->name, name) == 0) {
                    sema_error(ctx, child->seq_num, child->col,
                               "Duplicate identifier \"%s\" in the same scope (previous at Seq %06d).",
                               name, existing->seq_num);
                    break;
                }
            }
        }

        DataSym sym;
        memset(&sym, 0, sizeof(sym));
        snprintf(sym.name, sizeof(sym.name), "%s", name);
        sym.level = lvl;
        sym.parent_idx = parent_idx;
        sym.fd_idx = fd_idx;
        sym.seq_num = child->seq_num;
        sym.col = child->col;
        sym.kind = (lvl == 88) ? DATA_COND : DATA_GROUP;

        /* Scan clauses for PIC and OCCURS ... INDEXED BY */
        for (size_t c = 0; c < child->child_count; c++) {
            const ASTNode *cl = child->children[c];
            if (cl->kind == AST_CLAUSE && cl->text && strcmp(cl->text, "PIC") == 0 && cl->extra) {
                snprintf(sym.pic, sizeof(sym.pic), "%s", cl->extra);
                sym.kind = classify_pic(cl->extra);
            } else if (cl->kind == AST_CLAUSE && cl->text && strcmp(cl->text, "OCCURS") == 0) {
                for (size_t k = 0; k < cl->child_count; k++) {
                    const ASTNode *sub = cl->children[k];
                    if (sub->kind == AST_CLAUSE && sub->text && strcmp(sub->text, "INDEXED-BY") == 0) {
                        for (size_t m = 0; m < sub->child_count; m++) {
                            const ASTNode *ix = sub->children[m];
                            if (ix->kind == AST_DATA_REF && ix->text) {
                                DataSym isym;
                                memset(&isym, 0, sizeof(isym));
                                snprintf(isym.name, sizeof(isym.name), "%s", ix->text);
                                isym.level = 0;
                                isym.parent_idx = -1;
                                isym.fd_idx = -1;
                                isym.kind = DATA_INDEX;
                                isym.seq_num = ix->seq_num;
                                isym.col = ix->col;
                                add_data_sym(ctx->tab, &isym);
                            }
                        }
                    }
                }
            }
        }

        int sym_idx = add_data_sym(ctx->tab, &sym);
        if (lvl != 77 && lvl != 88 && lvl != 66 && top < 64) {
            stack[top].level = lvl;
            stack[top].sym_idx = sym_idx;
            top++;
        }
    }
}

static void collect_data_div(SemaCtx *ctx, const ASTNode *dd) {
    if (!dd) return;
    for (size_t i = 0; i < dd->child_count; i++) {
        const ASTNode *sec = dd->children[i];
        if (sec->kind != AST_DATA_SECTION) continue;

        if (sec->text && strcmp(sec->text, "FILE") == 0) {
            for (size_t j = 0; j < sec->child_count; j++) {
                const ASTNode *fd = sec->children[j];
                if (fd->kind == AST_FD && fd->text) {
                    int fidx = get_or_add_file_sym(ctx->tab, fd->text);
                    FileSym *f = &ctx->tab->files[fidx];
                    if (f->has_fd) {
                        sema_error(ctx, fd->seq_num, fd->col,
                                   "Duplicate FD declaration for file \"%s\".", fd->text);
                    }
                    f->has_fd = true;
                    f->fd_seq = fd->seq_num;
                    f->fd_col = fd->col;
                    collect_data_items(ctx, fd, fidx);
                }
            }
        } else {
            collect_data_items(ctx, sec, -1);
        }
    }
}

static void collect_procs(SemaCtx *ctx, const ASTNode *pd) {
    if (!pd) return;
    for (size_t i = 0; i < pd->child_count; i++) {
        const ASTNode *el = pd->children[i];
        if (el->kind == AST_PROC_SECTION && el->text) {
            add_proc_sym(ctx, el->text, true, el->seq_num, el->col);
        } else if (el->kind == AST_PARAGRAPH && el->text) {
            add_proc_sym(ctx, el->text, false, el->seq_num, el->col);
        }
    }
}

/* =========================================================================
 * Pass 2: Semantic Validation (Section 4 Rules 1-4)
 * ========================================================================= */

static void check_node(SemaCtx *ctx, const ASTNode *node);

static bool sym_matches_qualifiers(const SymTab *tab, int sym_idx,
                                   const char **quals, size_t nquals) {
    size_t qi = 0;
    int p = tab->data[sym_idx].parent_idx;
    while (p >= 0 && qi < nquals) {
        if (strcmp(tab->data[p].name, quals[qi]) == 0) {
            qi++;
        }
        p = tab->data[p].parent_idx;
    }
    return qi == nquals;
}

static bool data_ref_has_refmod(const ASTNode *ref) {
    for (size_t i = 0; i < ref->child_count; i++) {
        if (ref->children[i]->kind == AST_REFMOD) return true;
    }
    return false;
}

/* Resolve an AST_DATA_REF and check any nested subscript/refmod expressions.
 * Returns index in tab->data, -2 for built-in special registers, or -1 on error. */
static int resolve_data_ref(SemaCtx *ctx, const ASTNode *ref) {
    if (!ref || ref->kind != AST_DATA_REF || !ref->text) return -1;

    /* Extract OF/IN qualifiers and validate nested subscript/refmod expressions */
    const char *quals[16];
    size_t nquals = 0;
    for (size_t i = 0; i < ref->child_count; i++) {
        const ASTNode *ch = ref->children[i];
        if (ch->kind == AST_DATA_REF && ch->text) {
            if (nquals < 16) quals[nquals++] = ch->text;
        } else {
            check_node(ctx, ch);
        }
    }

    if (nquals == 0 && is_special_register(ref->text)) {
        return -2;
    }

    int match_idx = -1;
    int matches = 0;
    for (size_t i = 0; i < ctx->tab->data_count; i++) {
        if (strcmp(ctx->tab->data[i].name, ref->text) != 0) continue;
        if (nquals == 0 || sym_matches_qualifiers(ctx->tab, (int)i, quals, nquals)) {
            match_idx = (int)i;
            matches++;
        }
    }

    if (matches == 0) {
        if (nquals > 0) {
            sema_error(ctx, ref->seq_num, ref->col,
                       "Undeclared qualified identifier \"%s OF %s\".", ref->text, quals[0]);
        } else {
            sema_error(ctx, ref->seq_num, ref->col,
                       "Undeclared identifier \"%s\".", ref->text);
        }
        return -1;
    }
    if (matches > 1) {
        sema_error(ctx, ref->seq_num, ref->col,
                   "Ambiguous identifier \"%s\" (%d declarations); qualify with OF/IN.",
                   ref->text, matches);
        return -1;
    }
    return match_idx;
}

static void check_file_ref(SemaCtx *ctx, const ASTNode *ref, const char *verb) {
    if (!ref || ref->kind != AST_DATA_REF || !ref->text) return;
    int fidx = find_file_sym(ctx->tab, ref->text);
    if (fidx < 0) {
        sema_error(ctx, ref->seq_num, ref->col,
                   "File \"%s\" used in %s is not declared in FILE-CONTROL or FD.",
                   ref->text, verb);
        return;
    }
    const FileSym *f = &ctx->tab->files[fidx];
    if (!f->has_select) {
        sema_error(ctx, ref->seq_num, ref->col,
                   "File \"%s\" used in %s has no SELECT declaration in FILE-CONTROL.",
                   ref->text, verb);
    }
    if (!f->has_fd) {
        sema_error(ctx, ref->seq_num, ref->col,
                   "File \"%s\" used in %s has no FD declaration in DATA DIVISION.",
                   ref->text, verb);
    }
}

static void check_record_ref(SemaCtx *ctx, const ASTNode *ref, const char *verb) {
    int didx = resolve_data_ref(ctx, ref);
    if (didx < 0) return;
    const DataSym *sym = &ctx->tab->data[didx];
    if (sym->fd_idx < 0) {
        sema_error(ctx, ref->seq_num, ref->col,
                   "Record \"%s\" used in %s is not declared under an FD in FILE SECTION.",
                   sym->name, verb);
        return;
    }
    const FileSym *f = &ctx->tab->files[sym->fd_idx];
    if (!f->has_select || !f->has_fd) {
        sema_error(ctx, ref->seq_num, ref->col,
                   "Record \"%s\" in %s belongs to file \"%s\" without complete SELECT/FD association.",
                   sym->name, verb, f->name);
    }
}

static void check_move(SemaCtx *ctx, const ASTNode *node) {
    if (node->child_count < 2) return;
    const ASTNode *src = node->children[0];

    DataKind src_kind = DATA_UNKNOWN;
    const char *src_desc = src->text ? src->text : "<expr>";
    bool src_non_numeric_str = false;

    if (src->kind == AST_DATA_REF) {
        int sidx = resolve_data_ref(ctx, src);
        if (sidx == -2) {
            src_kind = DATA_NUMERIC;
        } else if (sidx >= 0) {
            src_kind = data_ref_has_refmod(src) ? DATA_ALPHANUMERIC : ctx->tab->data[sidx].kind;
        }
    } else if (src->kind == AST_LITERAL_NUM) {
        src_kind = DATA_NUMERIC;
    } else if (src->kind == AST_LITERAL_STR) {
        src_kind = DATA_ALPHANUMERIC;
        src_non_numeric_str = !is_numeric_string_literal(src->text);
    } else if (src->kind == AST_FIGURATIVE) {
        if (src->text && (strncmp(src->text, "ZERO", 4) == 0)) {
            src_kind = DATA_NUMERIC;
        } else {
            src_kind = DATA_ALPHANUMERIC;
        }
    } else {
        check_node(ctx, src);
    }

    for (size_t i = 1; i < node->child_count; i++) {
        const ASTNode *dst = node->children[i];
        int didx = resolve_data_ref(ctx, dst);
        if (didx < 0) continue;

        DataKind dst_kind = data_ref_has_refmod(dst) ? DATA_ALPHANUMERIC : ctx->tab->data[didx].kind;
        bool dst_is_num = (dst_kind == DATA_NUMERIC || dst_kind == DATA_NUMERIC_EDITED);

        /* Rule 1: forbid MOVE from purely alphabetic (PIC A) or non-numeric literal to numeric */
        if (src_kind == DATA_ALPHA && dst_is_num) {
            sema_error(ctx, dst->seq_num, dst->col,
                       "Incompatible MOVE from alphabetic (PIC A) \"%s\" to numeric \"%s\" (%s).",
                       src_desc, dst->text, ctx->tab->data[didx].pic);
        } else if (src_non_numeric_str && dst_is_num) {
            sema_error(ctx, dst->seq_num, dst->col,
                       "Incompatible MOVE from non-numeric literal %s to numeric \"%s\" (%s).",
                       src_desc, dst->text, ctx->tab->data[didx].pic);
        } else if (src_kind == DATA_ALPHANUMERIC && src->kind == AST_DATA_REF && dst_is_num) {
            sema_warn(ctx, dst->seq_num, dst->col,
                      "Moving alphanumeric \"%s\" to numeric \"%s\" (%s).",
                      src_desc, dst->text, ctx->tab->data[didx].pic);
        } else if ((src_kind == DATA_NUMERIC || src_kind == DATA_NUMERIC_EDITED) && dst_kind == DATA_ALPHA) {
            sema_warn(ctx, dst->seq_num, dst->col,
                      "Moving numeric \"%s\" to alphabetic (PIC A) \"%s\".",
                      src_desc, dst->text);
        }
    }
}

static void check_node(SemaCtx *ctx, const ASTNode *node) {
    if (!node) return;

    switch (node->kind) {
        case AST_DATA_REF:
            resolve_data_ref(ctx, node);
            return;

        case AST_STMT_MOVE:
            check_move(ctx, node);
            return;

        case AST_STMT_PERFORM:
            if (node->text && strcmp(node->text, "INLINE") != 0) {
                if (find_proc_sym(ctx->tab, node->text) < 0) {
                    sema_error(ctx, node->seq_num, node->col,
                               "Undeclared PERFORM target \"%s\".", node->text);
                }
            }
            for (size_t i = 0; i < node->child_count; i++) {
                const ASTNode *ch = node->children[i];
                if (ch->kind == AST_CLAUSE && ch->text && strcmp(ch->text, "THRU") == 0 && ch->extra) {
                    if (find_proc_sym(ctx->tab, ch->extra) < 0) {
                        sema_error(ctx, ch->seq_num, ch->col,
                                   "Undeclared PERFORM THRU target \"%s\".", ch->extra);
                    }
                } else {
                    check_node(ctx, ch);
                }
            }
            return;

        case AST_STMT_GOTO:
            if (node->text && find_proc_sym(ctx->tab, node->text) < 0) {
                sema_error(ctx, node->seq_num, node->col,
                           "Undeclared GO TO target \"%s\".", node->text);
            }
            if (node->extra && find_proc_sym(ctx->tab, node->extra) < 0) {
                sema_error(ctx, node->seq_num, node->col,
                           "Undeclared ALTER target \"%s\".", node->extra);
            }
            return;

        case AST_STMT_OPEN:
            for (size_t i = 0; i < node->child_count; i++) {
                const ASTNode *grp = node->children[i];
                for (size_t j = 0; j < grp->child_count; j++) {
                    check_file_ref(ctx, grp->children[j], "OPEN");
                }
            }
            return;

        case AST_STMT_CLOSE:
            for (size_t i = 0; i < node->child_count; i++) {
                check_file_ref(ctx, node->children[i], "CLOSE");
            }
            return;

        case AST_STMT_READ:
        case AST_STMT_DELETE:
        case AST_STMT_START:
        case AST_STMT_RETURN: {
            const char *verb = ast_kind_name(node->kind);
            if (node->child_count > 0) check_file_ref(ctx, node->children[0], verb);
            for (size_t i = 1; i < node->child_count; i++) check_node(ctx, node->children[i]);
            return;
        }

        case AST_STMT_WRITE:
        case AST_STMT_REWRITE:
        case AST_STMT_RELEASE: {
            const char *verb = ast_kind_name(node->kind);
            if (node->child_count > 0) check_record_ref(ctx, node->children[0], verb);
            for (size_t i = 1; i < node->child_count; i++) check_node(ctx, node->children[i]);
            return;
        }

        case AST_STMT_SORT:
        case AST_STMT_MERGE:
            if (node->child_count > 0) check_file_ref(ctx, node->children[0], ast_kind_name(node->kind));
            return;

        default:
            for (size_t i = 0; i < node->child_count; i++) {
                check_node(ctx, node->children[i]);
            }
            return;
    }
}

bool sema_analyze(const ASTNode *root, const char *filename, SymTab *out_tab) {
    if (!root || !out_tab) return false;
    symtab_init(out_tab);
    SemaCtx ctx = { out_tab, filename };

    const ASTNode *env_div = NULL;
    const ASTNode *data_div = NULL;
    const ASTNode *proc_div = NULL;

    for (size_t i = 0; i < root->child_count; i++) {
        const ASTNode *ch = root->children[i];
        if (ch->kind == AST_ENVIRONMENT) env_div = ch;
        else if (ch->kind == AST_DATA_DIVISION) data_div = ch;
        else if (ch->kind == AST_PROCEDURE_DIVISION) proc_div = ch;
    }

    /* Pass 1: declarations */
    collect_env(&ctx, env_div);
    collect_data_div(&ctx, data_div);
    collect_procs(&ctx, proc_div);

    /* Rule 4a: check SELECT <-> FD pairing */
    for (size_t i = 0; i < out_tab->file_count; i++) {
        const FileSym *f = &out_tab->files[i];
        if (f->has_fd && !f->has_select) {
            sema_error(&ctx, f->fd_seq, f->fd_col,
                       "FD \"%s\" has no matching SELECT in FILE-CONTROL.", f->name);
        } else if (f->has_select && !f->has_fd) {
            sema_warn(&ctx, f->select_seq, f->select_col,
                      "SELECT \"%s\" has no matching FD in DATA DIVISION.", f->name);
        }
    }

    /* Also validate data references inside ENVIRONMENT (ASSIGN TO var, RECORD KEY, FILE STATUS) */
    if (env_div) {
        for (size_t i = 0; i < env_div->child_count; i++) {
            const ASTNode *sec = env_div->children[i];
            if (sec->kind != AST_FILE_CONTROL) continue;
            for (size_t j = 0; j < sec->child_count; j++) {
                const ASTNode *sel = sec->children[j];
                if (sel->kind != AST_SELECT) continue;
                for (size_t k = 0; k < sel->child_count; k++) {
                    const ASTNode *cl = sel->children[k];
                    if (cl->kind == AST_CLAUSE && cl->text &&
                        (strcmp(cl->text, "RECORD-KEY") == 0 ||
                         strcmp(cl->text, "ALTERNATE-KEY") == 0 ||
                         strcmp(cl->text, "FILE-STATUS") == 0 ||
                         strcmp(cl->text, "RELATIVE-KEY") == 0)) {
                        for (size_t m = 0; m < cl->child_count; m++) {
                            check_node(&ctx, cl->children[m]);
                        }
                    }
                }
            }
        }
    }

    /* Pass 2: statements & expressions in PROCEDURE DIVISION */
    if (proc_div) {
        for (size_t i = 0; i < proc_div->child_count; i++) {
            const ASTNode *ch = proc_div->children[i];
            if (ch->kind == AST_PROC_SECTION || ch->kind == AST_PARAGRAPH) continue;
            check_node(&ctx, ch);
        }
    }

    return out_tab->errors == 0;
}

void symtab_print(const SymTab *tab, FILE *out) {
    if (!tab || !out) return;
    fprintf(out, "=== FILES (%zu) ===\n", tab->file_count);
    for (size_t i = 0; i < tab->file_count; i++) {
        const FileSym *f = &tab->files[i];
        fprintf(out, "  %-24s SELECT=%s  FD=%s\n",
                f->name, f->has_select ? "yes" : "no ", f->has_fd ? "yes" : "no ");
    }

    fprintf(out, "=== DATA ITEMS (%zu) ===\n", tab->data_count);
    for (size_t i = 0; i < tab->data_count; i++) {
        const DataSym *d = &tab->data[i];
        const char *parent = (d->parent_idx >= 0) ? tab->data[d->parent_idx].name : "-";
        fprintf(out, "  %02d %-28s %-8s PIC=%-16s PARENT=%-20s @%d:%d\n",
                d->level, d->name, data_kind_to_string(d->kind),
                d->pic[0] ? d->pic : "-", parent, d->seq_num, d->col);
    }

    fprintf(out, "=== PROCEDURES (%zu) ===\n", tab->proc_count);
    for (size_t i = 0; i < tab->proc_count; i++) {
        const ProcSym *p = &tab->procs[i];
        fprintf(out, "  %-28s %-9s @%d:%d\n",
                p->name, p->is_section ? "SECTION" : "PARAGRAPH", p->seq_num, p->col);
    }
}
