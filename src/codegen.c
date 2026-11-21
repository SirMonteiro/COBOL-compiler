#include "codegen.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"

/* =========================================================================
 * Internal Data Structures
 * ========================================================================= */

#define MAX_OCCURS_DEPTH 4
#define MAX_MODULES      16

typedef struct {
    char name[64];
    int level;
    int parent_idx;
    int root_idx;
    int fd_idx;
    int section_kind; /* 0=FILE, 1=WS, 2=LS, 3=LINKAGE, 4=SCREEN */
    DataKind kind;
    char pic_raw[64];
    char pic_exp[128];
    int elem_size;
    int total_size;
    int offset_in_root;
    int int_digits;
    int frac_digits;
    bool is_signed;
    bool is_edited;
    bool is_redefines;
    int redefines_idx;
    int occurs;
    int occurs_depth;
    int occurs_stride[MAX_OCCURS_DEPTH];
    int pic_str_id;        /* string pool id for edited PIC mask */
    const ASTNode *val_ast;/* AST_CLAUSE "VALUE" */
    bool is_global_retcode;
} VarLayout;

typedef struct {
    char name[64];
    char assign_lit[256];
    int assign_var_idx;   /* >=0 if ASSIGN TO <var> */
    int status_var_idx;   /* >=0 if FILE STATUS IS <var> */
    int rec_var_idx;      /* primary 01 record under FD */
    bool is_line_seq;
    int path_str_id;      /* string pool id for static path */
} FileLayout;

typedef struct {
    char name[64];
    bool is_section;
    int sec_end_block;    /* for sections: last block index belonging to section */
    int global_id;        /* unique global block id for _rt_perf_chk */
    const ASTNode **stmts;
    size_t stmt_count;
    size_t stmt_cap;
} ProcBlock;

typedef struct {
    char *bytes;
    size_t len;
    bool zterm;
} StrPoolItem;

typedef struct {
    int prog_id;
    char prog_name[64];
    char san_name[64];
    const ASTNode *root;
    bool owns_ast;
    bool decimal_comma;
    int prog_frac;
    int prog_scale;

    VarLayout *vars;
    size_t var_count;
    size_t var_cap;

    FileLayout *files;
    size_t file_count;
    size_t file_cap;

    ProcBlock *blocks;
    size_t block_count;
    size_t block_cap;

    int using_vars[8];
    int using_count;
} ProgCtx;

typedef struct {
    FILE *out;
    MipsTarget target;
    const CodegenOpts *opts;

    ProgCtx *progs[MAX_MODULES];
    int prog_count;

    StrPoolItem *strs;
    size_t str_count;
    size_t str_cap;

    int next_label;
    int next_block_gid;

    /* Current program being emitted */
    ProgCtx *cur;
    int cur_block_idx;
} EmitCtx;

typedef struct {
    const ASTNode *last_lhs;
    const char *last_op;
} CondCtx;

/* =========================================================================
 * Small Utilities
 * ========================================================================= */

static int ci_cmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

static void upper_copy(char *dst, size_t n, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

static void sanitize_label(char *dst, size_t n, const char *src) {
    size_t i = 0;
    if (!src || !*src) { snprintf(dst, n, "UNNAMED"); return; }
    for (; i + 1 < n && src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c)) dst[i] = (char)toupper(c);
        else dst[i] = '_';
    }
    dst[i] = '\0';
}

static size_t unquote_cobol_str(const char *raw, char *out, size_t out_cap) {
    if (!raw) { if (out_cap) out[0] = '\0'; return 0; }
    size_t rlen = strlen(raw);
    size_t start = 0, end = rlen;
    char q = 0;
    if (rlen >= 2 && (raw[0] == '\'' || raw[0] == '"') && raw[rlen - 1] == raw[0]) {
        q = raw[0];
        start = 1;
        end = rlen - 1;
    }
    size_t j = 0;
    for (size_t i = start; i < end && j + 1 < out_cap; i++) {
        if (q && raw[i] == q && i + 1 < end && raw[i + 1] == q) {
            out[j++] = q;
            i++;
        } else {
            out[j++] = raw[i];
        }
    }
    if (out_cap) out[j] = '\0';
    return j;
}

static int pow10_int(int e) {
    int r = 1;
    for (int i = 0; i < e; i++) r *= 10;
    return r;
}

static int32_t parse_num_lit_scaled(const char *s, int prog_frac, bool dec_comma) {
    if (!s) return 0;
    int neg = 0;
    size_t i = 0;
    while (s[i] == ' ') i++;
    if (s[i] == '-') { neg = 1; i++; }
    else if (s[i] == '+') { i++; }

    int64_t acc = 0;
    int seen_dec = 0;
    int frac = 0;
    for (; s[i]; i++) {
        char c = s[i];
        if (c == '.' || (dec_comma && c == ',') || (!dec_comma && c == ',')) {
            if (!seen_dec) { seen_dec = 1; continue; }
        }
        if (c >= '0' && c <= '9') {
            acc = acc * 10 + (c - '0');
            if (seen_dec) frac++;
        }
    }
    int diff = prog_frac - frac;
    while (diff > 0) { acc *= 10; diff--; }
    while (diff < 0) { acc /= 10; diff++; }
    if (neg) acc = -acc;
    return (int32_t)acc;
}

static int strpool_add(EmitCtx *ec, const char *bytes, size_t len, bool zterm) {
    for (size_t i = 0; i < ec->str_count; i++) {
        if (ec->strs[i].len == len && ec->strs[i].zterm == zterm &&
            (len == 0 || memcmp(ec->strs[i].bytes, bytes, len) == 0)) {
            return (int)i;
        }
    }
    if (ec->str_count == ec->str_cap) {
        size_t ncap = ec->str_cap ? ec->str_cap * 2 : 64;
        ec->strs = realloc(ec->strs, ncap * sizeof(StrPoolItem));
        ec->str_cap = ncap;
    }
    char *copy = malloc(len + 1);
    if (len > 0) memcpy(copy, bytes, len);
    copy[len] = '\0';
    ec->strs[ec->str_count].bytes = copy;
    ec->strs[ec->str_count].len = len;
    ec->strs[ec->str_count].zterm = zterm;
    return (int)(ec->str_count++);
}

static int strpool_add_zstr(EmitCtx *ec, const char *s) {
    return strpool_add(ec, s ? s : "", s ? strlen(s) : 0, true);
}

static void emit_bytes_data(FILE *out, const unsigned char *buf, size_t len) {
    if (len == 0) {
        fprintf(out, "    .byte 0\n");
        return;
    }
    for (size_t i = 0; i < len; i += 16) {
        fprintf(out, "    .byte ");
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            if (j > 0) fputc(',', out);
            fprintf(out, "%u", (unsigned)buf[i + j]);
        }
        fputc('\n', out);
    }
}

/* =========================================================================
 * PIC Parsing & Data Layout Builder
 * ========================================================================= */

static void parse_pic_into_var(VarLayout *v, const char *pic, bool dec_comma) {
    v->pic_raw[0] = '\0';
    v->pic_exp[0] = '\0';
    v->elem_size = 0;
    v->int_digits = 0;
    v->frac_digits = 0;
    v->is_signed = false;
    v->is_edited = false;
    v->kind = DATA_ALPHANUMERIC;

    if (!pic || !*pic) return;
    snprintf(v->pic_raw, sizeof(v->pic_raw), "%s", pic);

    char exp[128];
    size_t elen = 0;
    bool has_x = false, has_a = false, has_9 = false, has_edit = false;
    bool has_s = false, seen_v = false;
    size_t n = strlen(pic);

    for (size_t i = 0; i < n; i++) {
        char c = (char)toupper((unsigned char)pic[i]);
        if (c == ' ' || c == '\t') continue;
        int rep = 1;
        if (i + 1 < n && pic[i + 1] == '(') {
            size_t j = i + 2;
            int val = 0;
            while (j < n && isdigit((unsigned char)pic[j])) {
                val = val * 10 + (pic[j] - '0');
                j++;
            }
            if (j < n && pic[j] == ')' && val > 0) {
                rep = val;
                i = j;
            }
        }
        if (c == 'S') {
            has_s = true;
            continue;
        }
        if (c == 'V') {
            seen_v = true;
            continue;
        }
        for (int r = 0; r < rep; r++) {
            if (elen + 1 < sizeof(exp)) exp[elen++] = c;
            if (c == 'X') has_x = true;
            else if (c == 'A') has_a = true;
            else if (c == '9') {
                has_9 = true;
                if (seen_v) v->frac_digits++;
                else v->int_digits++;
            } else {
                has_edit = true;
                if (c == 'Z') {
                    if (seen_v) v->frac_digits++;
                    else v->int_digits++;
                }
            }
        }
    }
    exp[elen] = '\0';
    snprintf(v->pic_exp, sizeof(v->pic_exp), "%s", exp);

    if (has_x) {
        v->kind = DATA_ALPHANUMERIC;
        v->elem_size = (int)elen;
        v->frac_digits = 0;
    } else if (has_a && !has_9 && !has_edit) {
        v->kind = DATA_ALPHA;
        v->elem_size = (int)elen;
        v->frac_digits = 0;
    } else if (has_edit) {
        v->kind = DATA_NUMERIC_EDITED;
        v->is_edited = true;
        v->elem_size = (int)elen;
        /* Recompute frac_digits for edited picture based on decimal separator */
        char dec_sep = dec_comma ? ',' : '.';
        const char *dp = strrchr(exp, dec_sep);
        v->frac_digits = 0;
        if (dp) {
            for (const char *p = dp + 1; *p; p++) {
                if (*p == '9' || *p == 'Z') v->frac_digits++;
            }
        }
    } else if (has_9) {
        v->kind = DATA_NUMERIC;
        v->is_signed = has_s;
        v->elem_size = (int)elen + (has_s ? 1 : 0);
    } else {
        v->kind = DATA_ALPHANUMERIC;
        v->elem_size = (int)(elen > 0 ? elen : 1);
    }
    if (v->elem_size <= 0) v->elem_size = 1;
}

static int prog_add_var(ProgCtx *pc, const VarLayout *v) {
    if (pc->var_count == pc->var_cap) {
        size_t ncap = pc->var_cap ? pc->var_cap * 2 : 64;
        pc->vars = realloc(pc->vars, ncap * sizeof(VarLayout));
        pc->var_cap = ncap;
    }
    pc->vars[pc->var_count] = *v;
    return (int)(pc->var_count++);
}

static int prog_add_file(ProgCtx *pc, const FileLayout *f) {
    if (pc->file_count == pc->file_cap) {
        size_t ncap = pc->file_cap ? pc->file_cap * 2 : 16;
        pc->files = realloc(pc->files, ncap * sizeof(FileLayout));
        pc->file_cap = ncap;
    }
    pc->files[pc->file_count] = *f;
    return (int)(pc->file_count++);
}

static int find_file_by_name(const ProgCtx *pc, const char *name) {
    for (size_t i = 0; i < pc->file_count; i++) {
        if (ci_cmp(pc->files[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static bool has_ancestor_named(const ProgCtx *pc, int var_idx, const char *qname) {
    int p = pc->vars[var_idx].parent_idx;
    while (p >= 0) {
        if (ci_cmp(pc->vars[p].name, qname) == 0) return true;
        p = pc->vars[p].parent_idx;
    }
    /* Also allow FD name as qualifier */
    int fd = pc->vars[var_idx].fd_idx;
    if (fd >= 0 && fd < (int)pc->file_count && ci_cmp(pc->files[fd].name, qname) == 0)
        return true;
    return false;
}

static int resolve_data_ref(const ProgCtx *pc, const ASTNode *ref) {
    if (!ref || ref->kind != AST_DATA_REF || !ref->text) return -1;

    /* Collect OF/IN qualifiers */
    const char *quals[8];
    int nq = 0;
    for (size_t i = 0; i < ref->child_count && nq < 8; i++) {
        if (ref->children[i]->kind == AST_DATA_REF && ref->children[i]->text) {
            quals[nq++] = ref->children[i]->text;
        }
    }

    int best = -1;
    for (size_t i = 0; i < pc->var_count; i++) {
        if (ci_cmp(pc->vars[i].name, ref->text) != 0) continue;
        bool ok = true;
        for (int q = 0; q < nq; q++) {
            if (!has_ancestor_named(pc, (int)i, quals[q])) { ok = false; break; }
        }
        if (ok) {
            /* If multiple matches without qualifiers (e.g., 01 TAB containing 05 TAB OCCURS),
             * prefer one with OCCURS if ref has AST_SUBSCRIPT, else prefer first match. */
            bool has_sub = false;
            for (size_t k = 0; k < ref->child_count; k++) {
                if (ref->children[k]->kind == AST_SUBSCRIPT) has_sub = true;
            }
            if (best < 0) {
                best = (int)i;
            } else if (has_sub && pc->vars[i].occurs_depth > pc->vars[best].occurs_depth) {
                best = (int)i;
            }
        }
    }
    return best;
}

static void collect_env_info(ProgCtx *pc, const ASTNode *node) {
    if (!node) return;
    if (node->kind == AST_CLAUSE && node->text && ci_cmp(node->text, "DECIMAL-POINT-IS-COMMA") == 0) {
        pc->decimal_comma = true;
    }
    if (node->kind == AST_SELECT && node->text) {
        FileLayout f;
        memset(&f, 0, sizeof(f));
        upper_copy(f.name, sizeof(f.name), node->text);
        f.assign_var_idx = -1;
        f.status_var_idx = -1;
        f.rec_var_idx = -1;
        f.path_str_id = -1;
        for (size_t i = 0; i < node->child_count; i++) {
            const ASTNode *c = node->children[i];
            if (c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "ASSIGN") == 0 && c->child_count > 0) {
                const ASTNode *a = c->children[0];
                if (a->kind == AST_LITERAL_STR && a->text) {
                    unquote_cobol_str(a->text, f.assign_lit, sizeof(f.assign_lit));
                } else if (a->text) {
                    snprintf(f.assign_lit, sizeof(f.assign_lit), "%s", a->text);
                }
            } else if (c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "ORGANIZATION") == 0) {
                if (c->ival == 1) f.is_line_seq = true;
            }
        }
        prog_add_file(pc, &f);
    }
    for (size_t i = 0; i < node->child_count; i++) {
        collect_env_info(pc, node->children[i]);
    }
}

static void collect_data_items(ProgCtx *pc, EmitCtx *ec, const ASTNode *parent,
                               int sec_kind, int fd_idx) {
    struct { int idx; int level; } stack[32];
    int top = 0;

    for (size_t i = 0; i < parent->child_count; i++) {
        const ASTNode *n = parent->children[i];
        if (n->kind != AST_DATA_ITEM) continue;

        VarLayout v;
        memset(&v, 0, sizeof(v));
        upper_copy(v.name, sizeof(v.name), n->text ? n->text : "FILLER");
        v.level = n->ival;
        v.parent_idx = -1;
        v.root_idx = -1;
        v.fd_idx = fd_idx;
        v.section_kind = sec_kind;
        v.occurs = 1;
        v.redefines_idx = -1;
        v.pic_str_id = -1;

        const char *pic = NULL;
        const char *redef_name = NULL;
        const ASTNode *indexed_by = NULL;

        for (size_t k = 0; k < n->child_count; k++) {
            const ASTNode *c = n->children[k];
            if (c->kind != AST_CLAUSE || !c->text) continue;
            if (ci_cmp(c->text, "PIC") == 0) pic = c->extra;
            else if (ci_cmp(c->text, "VALUE") == 0) v.val_ast = c;
            else if (ci_cmp(c->text, "REDEFINES") == 0) redef_name = c->extra;
            else if (ci_cmp(c->text, "OCCURS") == 0) {
                v.occurs = c->ival > 0 ? c->ival : 1;
                if (c->child_count > 0) indexed_by = c->children[0];
            }
        }

        if (v.level == 88) {
            v.kind = DATA_COND;
            if (top > 0) v.parent_idx = stack[top - 1].idx;
            prog_add_var(pc, &v);
            continue;
        }

        if (v.level == 1 || v.level == 77) {
            top = 0;
            v.parent_idx = -1;
        } else {
            while (top > 0 && stack[top - 1].level >= v.level) top--;
            if (top > 0) v.parent_idx = stack[top - 1].idx;
        }

        if (pic) {
            parse_pic_into_var(&v, pic, pc->decimal_comma);
            if (v.is_edited) {
                v.pic_str_id = strpool_add_zstr(ec, v.pic_exp);
            }
        } else {
            v.kind = DATA_GROUP;
            v.elem_size = 0;
        }

        if (redef_name) {
            /* Look backwards for matching sibling */
            for (int j = (int)pc->var_count - 1; j >= 0; j--) {
                if (pc->vars[j].parent_idx == v.parent_idx &&
                    ci_cmp(pc->vars[j].name, redef_name) == 0) {
                    v.is_redefines = true;
                    v.redefines_idx = j;
                    break;
                }
            }
        }

        int idx = prog_add_var(pc, &v);
        if (fd_idx >= 0 && v.level == 1 && pc->files[fd_idx].rec_var_idx < 0) {
            pc->files[fd_idx].rec_var_idx = idx;
        }

        if (top < 32) {
            stack[top].idx = idx;
            stack[top].level = v.level;
            top++;
        }

        /* Register INDEXED BY variables as standalone numeric index vars */
        if (indexed_by && indexed_by->kind == AST_CLAUSE) {
            for (size_t m = 0; m < indexed_by->child_count; m++) {
                const ASTNode *ix = indexed_by->children[m];
                if (ix->kind == AST_DATA_REF && ix->text) {
                    VarLayout iv;
                    memset(&iv, 0, sizeof(iv));
                    upper_copy(iv.name, sizeof(iv.name), ix->text);
                    iv.level = 77;
                    iv.parent_idx = -1;
                    iv.root_idx = -1;
                    iv.fd_idx = -1;
                    iv.section_kind = 1;
                    iv.kind = DATA_INDEX;
                    iv.elem_size = 9;
                    iv.total_size = 9;
                    iv.int_digits = 9;
                    iv.occurs = 1;
                    iv.redefines_idx = -1;
                    iv.pic_str_id = -1;
                    prog_add_var(pc, &iv);
                }
            }
        }
    }
}

static void resolve_select_links(ProgCtx *pc, EmitCtx *ec, const ASTNode *node) {
    if (!node) return;
    if (node->kind == AST_SELECT && node->text) {
        int fi = find_file_by_name(pc, node->text);
        if (fi >= 0) {
            for (size_t i = 0; i < node->child_count; i++) {
                const ASTNode *c = node->children[i];
                if (c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "FILE-STATUS") == 0 && c->child_count > 0) {
                    pc->files[fi].status_var_idx = resolve_data_ref(pc, c->children[0]);
                } else if (c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "ASSIGN") == 0 && c->child_count > 0) {
                    const ASTNode *a = c->children[0];
                    if (a->kind == AST_DATA_REF) {
                        int vi = resolve_data_ref(pc, a);
                        if (vi >= 0) {
                            pc->files[fi].assign_var_idx = vi;
                        }
                    }
                }
            }
            if (pc->files[fi].assign_var_idx < 0) {
                const char *p = pc->files[fi].assign_lit;
                if (!p[0]) p = pc->files[fi].name;
                pc->files[fi].path_str_id = strpool_add_zstr(ec, p);
            }
        }
    }
    for (size_t i = 0; i < node->child_count; i++) {
        resolve_select_links(pc, ec, node->children[i]);
    }
}

static void compute_var_sizes_and_offsets(ProgCtx *pc) {
    /* 1. Compute group sizes bottom-up (children always have index > parent) */
    for (int i = (int)pc->var_count - 1; i >= 0; i--) {
        VarLayout *v = &pc->vars[i];
        if (v->kind == DATA_COND) continue;
        if (v->kind == DATA_GROUP) {
            int cur_off = 0;
            int max_end = 0;
            for (size_t j = (size_t)i + 1; j < pc->var_count; j++) {
                if (pc->vars[j].parent_idx != i) continue;
                if (pc->vars[j].kind == DATA_COND) continue;
                int child_off;
                if (pc->vars[j].is_redefines && pc->vars[j].redefines_idx >= 0) {
                    child_off = pc->vars[pc->vars[j].redefines_idx].offset_in_root;
                } else {
                    child_off = cur_off;
                    cur_off += pc->vars[j].total_size;
                }
                pc->vars[j].offset_in_root = child_off;
                if (child_off + pc->vars[j].total_size > max_end) {
                    max_end = child_off + pc->vars[j].total_size;
                }
            }
            v->elem_size = max_end > 0 ? max_end : 1;
        }
        v->total_size = v->elem_size * (v->occurs > 0 ? v->occurs : 1);
    }

    /* 2. Propagate root_idx, absolute offset_in_root, and OCCURS strides top-down */
    for (size_t i = 0; i < pc->var_count; i++) {
        VarLayout *v = &pc->vars[i];
        if (v->kind == DATA_COND) continue;
        if (v->parent_idx < 0) {
            v->root_idx = (int)i;
            if (v->is_redefines && v->redefines_idx >= 0) {
                v->root_idx = pc->vars[v->redefines_idx].root_idx;
            }
            v->offset_in_root = 0;
            v->occurs_depth = 0;
            if (v->occurs > 1) {
                v->occurs_stride[v->occurs_depth++] = v->elem_size;
            }
        } else {
            VarLayout *p = &pc->vars[v->parent_idx];
            v->root_idx = p->root_idx;
            v->offset_in_root = p->offset_in_root + v->offset_in_root;
            v->occurs_depth = p->occurs_depth;
            for (int d = 0; d < p->occurs_depth && d < MAX_OCCURS_DEPTH; d++) {
                v->occurs_stride[d] = p->occurs_stride[d];
            }
            if (v->occurs > 1 && v->occurs_depth < MAX_OCCURS_DEPTH) {
                v->occurs_stride[v->occurs_depth++] = v->elem_size;
            }
        }
    }

    /* 3. Determine program fixed-point scaling (max frac_digits, capped at 4) */
    int max_frac = 0;
    for (size_t i = 0; i < pc->var_count; i++) {
        if (pc->vars[i].frac_digits > max_frac) {
            max_frac = pc->vars[i].frac_digits;
        }
    }
    if (max_frac > 4) max_frac = 4;
    pc->prog_frac = max_frac;
    pc->prog_scale = pow10_int(max_frac);
}

static void fill_value_bytes(const VarLayout *v, const ASTNode *val_clause,
                             bool dec_comma, unsigned char *dst, size_t dlen) {
    if (!val_clause || val_clause->child_count == 0) return;
    const ASTNode *item = val_clause->children[0];
    if (!item) return;

    if (item->kind == AST_FIGURATIVE && item->text) {
        unsigned char ch = ' ';
        if (ci_cmp(item->text, "ZERO") == 0 || ci_cmp(item->text, "ZEROS") == 0 || ci_cmp(item->text, "ZEROES") == 0) {
            ch = '0';
        } else if (ci_cmp(item->text, "LOW-VALUE") == 0 || ci_cmp(item->text, "LOW-VALUES") == 0) {
            ch = 0;
        } else if (ci_cmp(item->text, "HIGH-VALUE") == 0 || ci_cmp(item->text, "HIGH-VALUES") == 0) {
            ch = 0xFF;
        } else if (ci_cmp(item->text, "QUOTE") == 0 || ci_cmp(item->text, "QUOTES") == 0) {
            ch = '"';
        }
        memset(dst, ch, dlen);
        if (v->kind == DATA_NUMERIC && v->is_signed && ch == '0' && dlen > 0) {
            dst[0] = '+';
        }
        return;
    }

    if (item->kind == AST_LITERAL_STR && item->text) {
        char buf[512];
        size_t slen = unquote_cobol_str(item->text, buf, sizeof(buf));
        if (item->extra && ci_cmp(item->extra, "ALL") == 0 && slen > 0) {
            for (size_t i = 0; i < dlen; i++) dst[i] = (unsigned char)buf[i % slen];
        } else {
            memset(dst, ' ', dlen);
            size_t cp = slen < dlen ? slen : dlen;
            if (cp > 0) memcpy(dst, buf, cp);
        }
        return;
    }

    if (item->kind == AST_LITERAL_NUM && item->text) {
        if (v->kind == DATA_NUMERIC) {
            int64_t val = parse_num_lit_scaled(item->text, v->frac_digits, dec_comma);
            int neg = val < 0;
            if (neg) val = -val;
            size_t start = 0;
            if (v->is_signed && dlen > 0) {
                dst[0] = neg ? '-' : '+';
                start = 1;
            }
            for (size_t p = dlen; p > start; p--) {
                dst[p - 1] = (unsigned char)('0' + (val % 10));
                val /= 10;
            }
        } else {
            /* Numeric literal into alnum or edited field */
            memset(dst, ' ', dlen);
            size_t slen = strlen(item->text);
            size_t cp = slen < dlen ? slen : dlen;
            memcpy(dst, item->text, cp);
        }
    }
}

static void init_var_tree(const ProgCtx *pc, int idx, unsigned char *root_buf, size_t root_len, int base_off) {
    const VarLayout *v = &pc->vars[idx];
    if (v->kind == DATA_COND) return;
    if (v->is_redefines && !v->val_ast) return;

    int rel_off = base_off;
    if (v->kind != DATA_GROUP) {
        /* Leaf item: set default then VALUE */
        if (rel_off >= 0 && (size_t)(rel_off + v->elem_size) <= root_len) {
            unsigned char *dst = root_buf + rel_off;
            if (!v->is_redefines) {
                if (v->kind == DATA_NUMERIC || v->kind == DATA_INDEX) {
                    memset(dst, '0', (size_t)v->elem_size);
                    if (v->is_signed && v->elem_size > 0) dst[0] = '+';
                } else {
                    memset(dst, ' ', (size_t)v->elem_size);
                }
            }
            if (v->val_ast) {
                fill_value_bytes(v, v->val_ast, pc->decimal_comma, dst, (size_t)v->elem_size);
            }
        }
    } else {
        if (v->val_ast && rel_off >= 0 && (size_t)(rel_off + v->elem_size) <= root_len) {
            fill_value_bytes(v, v->val_ast, pc->decimal_comma, root_buf + rel_off, (size_t)v->elem_size);
        }
        /* Recurse into direct children */
        for (size_t j = (size_t)idx + 1; j < pc->var_count; j++) {
            if (pc->vars[j].parent_idx != idx) continue;
            if (pc->vars[j].kind == DATA_COND) continue;
            int child_rel = rel_off + (pc->vars[j].offset_in_root - v->offset_in_root);
            init_var_tree(pc, (int)j, root_buf, root_len, child_rel);
        }
    }

    /* Duplicate element 0 across OCCURS copies 1..occurs-1 */
    if (v->occurs > 1 && rel_off >= 0 && (size_t)(rel_off + v->total_size) <= root_len) {
        for (int k = 1; k < v->occurs; k++) {
            memcpy(root_buf + rel_off + k * v->elem_size,
                   root_buf + rel_off,
                   (size_t)v->elem_size);
        }
    }
}

/* =========================================================================
 * Procedure Division Block Builder
 * ========================================================================= */

static int add_proc_block(ProgCtx *pc, EmitCtx *ec, const char *name, bool is_sec) {
    if (pc->block_count == pc->block_cap) {
        size_t ncap = pc->block_cap ? pc->block_cap * 2 : 32;
        pc->blocks = realloc(pc->blocks, ncap * sizeof(ProcBlock));
        pc->block_cap = ncap;
    }
    ProcBlock *b = &pc->blocks[pc->block_count];
    memset(b, 0, sizeof(*b));
    upper_copy(b->name, sizeof(b->name), name);
    b->is_section = is_sec;
    b->sec_end_block = (int)pc->block_count;
    b->global_id = ++ec->next_block_gid;
    return (int)(pc->block_count++);
}

static void block_add_stmt(ProcBlock *b, const ASTNode *st) {
    if (!st) return;
    if (b->stmt_count == b->stmt_cap) {
        size_t ncap = b->stmt_cap ? b->stmt_cap * 2 : 16;
        b->stmts = realloc(b->stmts, ncap * sizeof(const ASTNode *));
        b->stmt_cap = ncap;
    }
    b->stmts[b->stmt_count++] = st;
}

static void build_proc_blocks(ProgCtx *pc, EmitCtx *ec, const ASTNode *proc_div) {
    if (!proc_div) return;
    int cur = -1;
    int cur_sec = -1;

    for (size_t i = 0; i < proc_div->child_count; i++) {
        const ASTNode *n = proc_div->children[i];
        if (!n) continue;
        if (n->kind == AST_CLAUSE && n->text && ci_cmp(n->text, "USING") == 0) {
            for (size_t k = 0; k < n->child_count && pc->using_count < 8; k++) {
                int vi = resolve_data_ref(pc, n->children[k]);
                if (vi >= 0) pc->using_vars[pc->using_count++] = pc->vars[vi].root_idx;
            }
            continue;
        }
        if (n->kind == AST_PROC_SECTION) {
            cur = add_proc_block(pc, ec, n->text ? n->text : "SEC", true);
            cur_sec = cur;
        } else if (n->kind == AST_PARAGRAPH) {
            cur = add_proc_block(pc, ec, n->text ? n->text : "PARA", false);
            if (cur_sec >= 0) {
                pc->blocks[cur_sec].sec_end_block = cur;
            }
        } else if (n->kind == AST_SENTENCE) {
            if (cur < 0) {
                cur = add_proc_block(pc, ec, "__ENTRY__", false);
            }
            for (size_t k = 0; k < n->child_count; k++) {
                block_add_stmt(&pc->blocks[cur], n->children[k]);
            }
        }
    }
    if (pc->block_count == 0) {
        add_proc_block(pc, ec, "__EMPTY__", false);
    }
}

static int find_block_by_name(const ProgCtx *pc, const char *name) {
    if (!name) return -1;
    for (size_t i = 0; i < pc->block_count; i++) {
        if (ci_cmp(pc->blocks[i].name, name) == 0) return (int)i;
    }
    return -1;
}

/* =========================================================================
 * Subprogram Discovery (Auto-compile CALL 'PROGDATA' etc.)
 * ========================================================================= */

static bool try_resolve_subprog_path(const CodegenOpts *opts, const char *name,
                                     char *out_path, size_t out_cap) {
    static const char *exts[] = { ".cbl", ".cob", ".CBL", ".COB", "" };
    char lower_name[128];
    size_t nlen = strlen(name);
    for (size_t i = 0; i < nlen && i + 1 < sizeof(lower_name); i++)
        lower_name[i] = (char)tolower((unsigned char)name[i]);
    lower_name[nlen < sizeof(lower_name) ? nlen : sizeof(lower_name) - 1] = '\0';

    const char *dirs[40];
    int nd = 0;
    char input_dir[512] = "";
    if (opts && opts->input_path) {
        const char *slash = strrchr(opts->input_path, '/');
        if (slash) {
            size_t dl = (size_t)(slash - opts->input_path);
            if (dl >= sizeof(input_dir)) dl = sizeof(input_dir) - 1;
            memcpy(input_dir, opts->input_path, dl);
            input_dir[dl] = '\0';
            dirs[nd++] = input_dir;
        }
    }
    if (opts) {
        for (int i = 0; i < opts->n_copy_dirs && nd < 36; i++) {
            dirs[nd++] = opts->copy_dirs[i];
        }
    }
    dirs[nd++] = "codigo_fonte_cobol";
    dirs[nd++] = "exemplos";
    dirs[nd++] = ".";

    for (int d = 0; d < nd; d++) {
        for (int e = 0; e < 5; e++) {
            snprintf(out_path, out_cap, "%s/%s%s", dirs[d], name, exts[e]);
            FILE *f = fopen(out_path, "r");
            if (f) { fclose(f); return true; }
            snprintf(out_path, out_cap, "%s/%s%s", dirs[d], lower_name, exts[e]);
            f = fopen(out_path, "r");
            if (f) { fclose(f); return true; }
        }
    }
    return false;
}

static ProgCtx *build_prog_ctx(EmitCtx *ec, const ASTNode *root, bool owns_ast);

static void scan_calls_recursive(EmitCtx *ec, const ASTNode *node) {
    if (!node) return;
    if (node->kind == AST_STMT_CALL && node->child_count > 0) {
        const ASTNode *tgt = node->children[0];
        if (tgt && tgt->kind == AST_LITERAL_STR && tgt->text) {
            char subname[128];
            unquote_cobol_str(tgt->text, subname, sizeof(subname));
            /* Check if already loaded */
            bool exists = false;
            for (int i = 0; i < ec->prog_count; i++) {
                if (ci_cmp(ec->progs[i]->prog_name, subname) == 0) { exists = true; break; }
            }
            if (!exists && ec->prog_count < MAX_MODULES) {
                char path[1024];
                if (try_resolve_subprog_path(ec->opts, subname, path, sizeof(path))) {
                    TokenList toks;
                    token_list_init(&toks);
                    lexer_reset_errors();
                    OPTS lo = { false, false };
                    if (ec->opts && ec->opts->lex_opts) lo = *ec->opts->lex_opts;
                    bool lok = lex_cobol_file(path, &lo, NULL, &toks,
                                              ec->opts ? ec->opts->copy_dirs : NULL,
                                              ec->opts ? ec->opts->n_copy_dirs : 0,
                                              true);
                    if (lok && lexer_error_count() == 0) {
                        int perr = 0;
                        ASTNode *sub_ast = parse_tokens(&toks, path, &perr);
                        if (sub_ast && perr == 0) {
                            SymTab stab;
                            if (sema_analyze(sub_ast, path, &stab)) {
                                ProgCtx *sub_pc = build_prog_ctx(ec, sub_ast, true);
                                /* Also override prog_name if PROGRAM-ID differs slightly */
                                if (ci_cmp(sub_pc->prog_name, subname) != 0) {
                                    upper_copy(sub_pc->prog_name, sizeof(sub_pc->prog_name), subname);
                                    sanitize_label(sub_pc->san_name, sizeof(sub_pc->san_name), subname);
                                }
                                scan_calls_recursive(ec, sub_ast);
                            } else {
                                ast_free(sub_ast);
                            }
                            symtab_free(&stab);
                        } else {
                            ast_free(sub_ast);
                        }
                    }
                    token_list_free(&toks);
                }
            }
        }
    }
    for (size_t i = 0; i < node->child_count; i++) {
        scan_calls_recursive(ec, node->children[i]);
    }
}

static ProgCtx *build_prog_ctx(EmitCtx *ec, const ASTNode *root, bool owns_ast) {
    ProgCtx *pc = calloc(1, sizeof(ProgCtx));
    pc->prog_id = ec->prog_count;
    pc->root = root;
    pc->owns_ast = owns_ast;
    upper_copy(pc->prog_name, sizeof(pc->prog_name), root && root->text ? root->text : "MAIN");
    sanitize_label(pc->san_name, sizeof(pc->san_name), pc->prog_name);

    ec->progs[ec->prog_count++] = pc;

    /* Add special registers RETURN-CODE and TALLY */
    VarLayout rc;
    memset(&rc, 0, sizeof(rc));
    snprintf(rc.name, sizeof(rc.name), "RETURN-CODE");
    rc.level = 77; rc.parent_idx = -1; rc.root_idx = 0; rc.fd_idx = -1;
    rc.section_kind = 1; rc.kind = DATA_NUMERIC; rc.is_signed = true;
    rc.elem_size = 10; rc.total_size = 10; rc.int_digits = 9; rc.occurs = 1;
    rc.redefines_idx = -1; rc.pic_str_id = -1; rc.is_global_retcode = true;
    prog_add_var(pc, &rc);

    VarLayout tl;
    memset(&tl, 0, sizeof(tl));
    snprintf(tl.name, sizeof(tl.name), "TALLY");
    tl.level = 77; tl.parent_idx = -1; tl.root_idx = 1; tl.fd_idx = -1;
    tl.section_kind = 1; tl.kind = DATA_NUMERIC; tl.is_signed = false;
    tl.elem_size = 9; tl.total_size = 9; tl.int_digits = 9; tl.occurs = 1;
    tl.redefines_idx = -1; tl.pic_str_id = -1;
    prog_add_var(pc, &tl);

    /* Walk ENVIRONMENT DIVISION */
    for (size_t i = 0; root && i < root->child_count; i++) {
        if (root->children[i]->kind == AST_ENVIRONMENT) {
            collect_env_info(pc, root->children[i]);
        }
    }

    /* Walk DATA DIVISION */
    for (size_t i = 0; root && i < root->child_count; i++) {
        const ASTNode *dd = root->children[i];
        if (dd->kind != AST_DATA_DIVISION) continue;
        for (size_t s = 0; s < dd->child_count; s++) {
            const ASTNode *sec = dd->children[s];
            if (sec->kind != AST_DATA_SECTION) continue;
            int sk = 1;
            if (sec->text && ci_cmp(sec->text, "FILE") == 0) sk = 0;
            else if (sec->text && ci_cmp(sec->text, "LOCAL-STORAGE") == 0) sk = 2;
            else if (sec->text && ci_cmp(sec->text, "LINKAGE") == 0) sk = 3;
            else if (sec->text && ci_cmp(sec->text, "SCREEN") == 0) sk = 4;

            if (sk == 0) {
                for (size_t f = 0; f < sec->child_count; f++) {
                    const ASTNode *fd = sec->children[f];
                    if (fd->kind != AST_FD) continue;
                    int fi = find_file_by_name(pc, fd->text);
                    collect_data_items(pc, ec, fd, 0, fi);
                }
            } else {
                collect_data_items(pc, ec, sec, sk, -1);
            }
        }
    }

    compute_var_sizes_and_offsets(pc);

    /* Link SELECT clauses (FILE STATUS, dynamic ASSIGN) now that variables exist */
    for (size_t i = 0; root && i < root->child_count; i++) {
        if (root->children[i]->kind == AST_ENVIRONMENT) {
            resolve_select_links(pc, ec, root->children[i]);
        }
    }

    /* Build PROCEDURE DIVISION blocks */
    for (size_t i = 0; root && i < root->child_count; i++) {
        if (root->children[i]->kind == AST_PROCEDURE_DIVISION) {
            build_proc_blocks(pc, ec, root->children[i]);
        }
    }

    return pc;
}

/* =========================================================================
 * Code Emission Forward Declarations
 * ========================================================================= */

static void emit_eval_num(EmitCtx *ec, const ASTNode *expr);
static void emit_addr_len(EmitCtx *ec, const ASTNode *rv, int *out_var_idx);
static void emit_store_num(EmitCtx *ec, const ASTNode *target_ref, bool val_in_v0);
static void emit_move_one(EmitCtx *ec, const ASTNode *src, const ASTNode *dst_ref);
static void emit_cond(EmitCtx *ec, const ASTNode *cond, CondCtx *cctx);
static void emit_stmt(EmitCtx *ec, const ASTNode *st);
static void emit_stmt_list(EmitCtx *ec, const ASTNode *list_or_parent, size_t start_idx);

static int new_lbl(EmitCtx *ec) {
    return ec->next_label++;
}

static bool is_large_uint_var(const VarLayout *v) {
    return v && v->kind == DATA_NUMERIC && !v->is_signed && v->frac_digits == 0 && v->elem_size >= 10;
}

/* Emit address of data_ref in $v0 and byte length in $v1.
 * Sets *out_var_idx if non-NULL. */
static void emit_data_ref_addr_len(EmitCtx *ec, const ASTNode *ref, int *out_var_idx) {
    ProgCtx *pc = ec->cur;
    int vi = resolve_data_ref(pc, ref);
    if (out_var_idx) *out_var_idx = vi;

    if (vi < 0) {
        /* Fallback safe scratch buffer */
        fprintf(ec->out, "    la   $v0, _rt_numbuf\n");
        fprintf(ec->out, "    li   $v1, 1\n");
        return;
    }

    const VarLayout *v = &pc->vars[vi];
    int root = v->root_idx >= 0 ? v->root_idx : vi;
    const VarLayout *rv = &pc->vars[root];

    /* 1. Base address of root */
    if (v->is_global_retcode) {
        fprintf(ec->out, "    la   $v0, cob_g_retcode\n");
    } else if (rv->section_kind == 3) {
        /* LINKAGE SECTION: load pointer from p<id>_lp<root> */
        fprintf(ec->out, "    la   $t9, p%d_lp%d\n", pc->prog_id, root);
        fprintf(ec->out, "    lw   $v0, 0($t9)\n");
    } else {
        fprintf(ec->out, "    la   $v0, p%d_d%d\n", pc->prog_id, root);
    }

    if (v->offset_in_root > 0) {
        fprintf(ec->out, "    addiu $v0, $v0, %d\n", v->offset_in_root);
    }

    /* 2. Check for AST_SUBSCRIPT and AST_REFMOD children */
    const ASTNode *sub_node = NULL;
    const ASTNode *refmod_node = NULL;
    for (size_t i = 0; i < ref->child_count; i++) {
        if (ref->children[i]->kind == AST_SUBSCRIPT) sub_node = ref->children[i];
        else if (ref->children[i]->kind == AST_REFMOD) refmod_node = ref->children[i];
    }

    if (sub_node && v->occurs_depth > 0) {
        /* Save running base address on stack while computing subscripts */
        fprintf(ec->out, "    addiu $sp, $sp, -4\n");
        fprintf(ec->out, "    sw   $v0, 0($sp)\n");
        size_t nsubs = sub_node->child_count;
        if ((int)nsubs > v->occurs_depth) nsubs = (size_t)v->occurs_depth;
        for (size_t s = 0; s < nsubs; s++) {
            emit_eval_num(ec, sub_node->children[s]);
            if (pc->prog_scale > 1) {
                fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                fprintf(ec->out, "    div  $v0, $t0\n");
                fprintf(ec->out, "    mflo $v0\n");
            }
            /* COBOL subscripts are 1-based; clamp >= 1 */
            fprintf(ec->out, "    addiu $v0, $v0, -1\n");
            int lbl_ok = new_lbl(ec);
            fprintf(ec->out, "    bgez $v0, L%d\n", lbl_ok);
            fprintf(ec->out, "    li   $v0, 0\n");
            fprintf(ec->out, "L%d:\n", lbl_ok);
            fprintf(ec->out, "    li   $t0, %d\n", v->occurs_stride[s]);
            fprintf(ec->out, "    mult $v0, $t0\n");
            fprintf(ec->out, "    mflo $t0\n");
            fprintf(ec->out, "    lw   $t1, 0($sp)\n");
            fprintf(ec->out, "    addu $t1, $t1, $t0\n");
            fprintf(ec->out, "    sw   $t1, 0($sp)\n");
        }
        fprintf(ec->out, "    lw   $v0, 0($sp)\n");
        fprintf(ec->out, "    addiu $sp, $sp, 4\n");
    }

    int cur_len = (sub_node || v->occurs <= 1) ? v->elem_size : v->total_size;

    if (refmod_node && refmod_node->child_count >= 1) {
        /* Save base address */
        fprintf(ec->out, "    addiu $sp, $sp, -8\n");
        fprintf(ec->out, "    sw   $v0, 0($sp)\n");
        /* Evaluate start pos (1-based) */
        emit_eval_num(ec, refmod_node->children[0]);
        if (pc->prog_scale > 1) {
            fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
            fprintf(ec->out, "    div  $v0, $t0\n");
            fprintf(ec->out, "    mflo $v0\n");
        }
        fprintf(ec->out, "    addiu $v0, $v0, -1\n");
        int lbl_p = new_lbl(ec);
        fprintf(ec->out, "    bgez $v0, L%d\n", lbl_p);
        fprintf(ec->out, "    li   $v0, 0\n");
        fprintf(ec->out, "L%d:\n", lbl_p);
        fprintf(ec->out, "    sw   $v0, 4($sp)\n"); /* save 0-based offset */
        if (refmod_node->child_count >= 2) {
            emit_eval_num(ec, refmod_node->children[1]);
            if (pc->prog_scale > 1) {
                fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                fprintf(ec->out, "    div  $v0, $t0\n");
                fprintf(ec->out, "    mflo $v0\n");
            }
            fprintf(ec->out, "    move $v1, $v0\n");
        } else {
            fprintf(ec->out, "    lw   $t0, 4($sp)\n");
            fprintf(ec->out, "    li   $v1, %d\n", cur_len);
            fprintf(ec->out, "    subu $v1, $v1, $t0\n");
        }
        fprintf(ec->out, "    lw   $v0, 0($sp)\n");
        fprintf(ec->out, "    lw   $t0, 4($sp)\n");
        fprintf(ec->out, "    addu $v0, $v0, $t0\n");
        fprintf(ec->out, "    addiu $sp, $sp, 8\n");
    } else {
        fprintf(ec->out, "    li   $v1, %d\n", cur_len);
    }
}

/* Emit address in $v0 and byte length in $v1 for any rvalue */
static void emit_addr_len(EmitCtx *ec, const ASTNode *rv, int *out_var_idx) {
    if (out_var_idx) *out_var_idx = -1;
    if (!rv) {
        fprintf(ec->out, "    la   $v0, _rt_numbuf\n");
        fprintf(ec->out, "    li   $v1, 0\n");
        return;
    }

    if (rv->kind == AST_DATA_REF) {
        emit_data_ref_addr_len(ec, rv, out_var_idx);
        return;
    }

    if (rv->kind == AST_LITERAL_STR) {
        char buf[512];
        size_t len = unquote_cobol_str(rv->text, buf, sizeof(buf));
        int sid = strpool_add(ec, buf, len, false);
        fprintf(ec->out, "    la   $v0, cob_s%d\n", sid);
        fprintf(ec->out, "    li   $v1, %zu\n", len);
        return;
    }

    if (rv->kind == AST_LITERAL_NUM) {
        const char *s = rv->text ? rv->text : "0";
        int sid = strpool_add(ec, s, strlen(s), false);
        fprintf(ec->out, "    la   $v0, cob_s%d\n", sid);
        fprintf(ec->out, "    li   $v1, %zu\n", strlen(s));
        return;
    }

    if (rv->kind == AST_FIGURATIVE && rv->text) {
        char ch = ' ';
        if (ci_cmp(rv->text, "ZERO") == 0 || ci_cmp(rv->text, "ZEROS") == 0 || ci_cmp(rv->text, "ZEROES") == 0)
            ch = '0';
        else if (ci_cmp(rv->text, "LOW-VALUE") == 0 || ci_cmp(rv->text, "LOW-VALUES") == 0)
            ch = 0;
        else if (ci_cmp(rv->text, "HIGH-VALUE") == 0 || ci_cmp(rv->text, "HIGH-VALUES") == 0)
            ch = (char)0xFF;
        else if (ci_cmp(rv->text, "QUOTE") == 0 || ci_cmp(rv->text, "QUOTES") == 0)
            ch = '"';
        int sid = strpool_add(ec, &ch, 1, false);
        fprintf(ec->out, "    la   $v0, cob_s%d\n", sid);
        fprintf(ec->out, "    li   $v1, 1\n");
        return;
    }

    if (rv->kind == AST_FUNC_CALL && rv->text) {
        if (ci_cmp(rv->text, "REVERSE") == 0 && rv->child_count > 0) {
            emit_addr_len(ec, rv->children[0], NULL);
            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
            fprintf(ec->out, "    sw   $v1, 0($sp)\n");
            fprintf(ec->out, "    move $a1, $v0\n");
            fprintf(ec->out, "    move $a2, $v1\n");
            fprintf(ec->out, "    la   $a0, _rt_revbuf\n");
            fprintf(ec->out, "    jal  _rt_reverse\n");
            fprintf(ec->out, "    la   $v0, _rt_revbuf\n");
            fprintf(ec->out, "    lw   $v1, 0($sp)\n");
            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            return;
        }
        if (ci_cmp(rv->text, "UPPER-CASE") == 0 && rv->child_count > 0) {
            emit_addr_len(ec, rv->children[0], NULL);
            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
            fprintf(ec->out, "    sw   $v1, 0($sp)\n");
            fprintf(ec->out, "    move $a1, $v0\n");
            fprintf(ec->out, "    move $a2, $v1\n");
            fprintf(ec->out, "    la   $a0, _rt_revbuf\n");
            fprintf(ec->out, "    jal  _rt_upper_case\n");
            fprintf(ec->out, "    la   $v0, _rt_revbuf\n");
            fprintf(ec->out, "    lw   $v1, 0($sp)\n");
            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            return;
        }
        if (ci_cmp(rv->text, "CURRENT-DATE") == 0) {
            fprintf(ec->out, "    la   $a0, _rt_datebuf\n");
            fprintf(ec->out, "    li   $a1, 8\n");
            fprintf(ec->out, "    li   $a2, 0\n");
            fprintf(ec->out, "    jal  _rt_accept_datetime\n");
            fprintf(ec->out, "    la   $a0, _rt_datebuf\n");
            fprintf(ec->out, "    addiu $a0, $a0, 8\n");
            fprintf(ec->out, "    li   $a1, 8\n");
            fprintf(ec->out, "    li   $a2, 4\n");
            fprintf(ec->out, "    jal  _rt_accept_datetime\n");
            fprintf(ec->out, "    la   $v0, _rt_datebuf\n");
            fprintf(ec->out, "    li   $v1, 16\n");
            return;
        }
        if (rv->child_count > 0) {
            emit_addr_len(ec, rv->children[0], NULL);
            return;
        }
    }

    /* Numeric expression -> format into _rt_numbuf */
    emit_eval_num(ec, rv);
    int packed = (ec->cur->prog_frac << 8) | (ec->cur->prog_frac << 1) | 1;
    fprintf(ec->out, "    la   $a0, _rt_numbuf\n");
    fprintf(ec->out, "    li   $a1, 12\n");
    fprintf(ec->out, "    move $a2, $v0\n");
    fprintf(ec->out, "    li   $a3, %d\n", packed);
    fprintf(ec->out, "    jal  _rt_format_num\n");
    fprintf(ec->out, "    la   $v0, _rt_numbuf\n");
    fprintf(ec->out, "    li   $v1, 12\n");
}

/* =========================================================================
 * Numeric Expression Evaluation (Scaled by prog_scale)
 * ========================================================================= */

static void emit_eval_num(EmitCtx *ec, const ASTNode *expr) {
    ProgCtx *pc = ec->cur;
    if (!expr) {
        fprintf(ec->out, "    li   $v0, 0\n");
        return;
    }

    switch (expr->kind) {
        case AST_LITERAL_NUM: {
            int32_t v = parse_num_lit_scaled(expr->text, pc->prog_frac, pc->decimal_comma);
            fprintf(ec->out, "    li   $v0, %d\n", (int)v);
            return;
        }
        case AST_FIGURATIVE: {
            if (expr->text && ci_cmp(expr->text, "TRUE") == 0)
                fprintf(ec->out, "    li   $v0, %d\n", pc->prog_scale);
            else
                fprintf(ec->out, "    li   $v0, 0\n");
            return;
        }
        case AST_LITERAL_STR: {
            char buf[64];
            unquote_cobol_str(expr->text, buf, sizeof(buf));
            int32_t v = parse_num_lit_scaled(buf, pc->prog_frac, pc->decimal_comma);
            fprintf(ec->out, "    li   $v0, %d\n", (int)v);
            return;
        }
        case AST_DATA_REF: {
            int vi = -1;
            emit_data_ref_addr_len(ec, expr, &vi);
            int imp_frac = (vi >= 0 && pc->vars[vi].kind == DATA_NUMERIC) ? pc->vars[vi].frac_digits : 0;
            fprintf(ec->out, "    move $a0, $v0\n");
            fprintf(ec->out, "    move $a1, $v1\n");
            fprintf(ec->out, "    li   $a2, %d\n", imp_frac);
            fprintf(ec->out, "    li   $a3, %d\n", pc->prog_frac);
            fprintf(ec->out, "    jal  _rt_parse_num\n");
            return;
        }
        case AST_LENGTH_OF: {
            int len = 0;
            if (expr->child_count > 0) {
                int vi = resolve_data_ref(pc, expr->children[0]);
                if (vi >= 0) len = pc->vars[vi].total_size;
            }
            fprintf(ec->out, "    li   $v0, %d\n", len * pc->prog_scale);
            return;
        }
        case AST_UNOP: {
            if (expr->child_count > 0) emit_eval_num(ec, expr->children[0]);
            if (expr->text && strcmp(expr->text, "-") == 0) {
                fprintf(ec->out, "    subu $v0, $zero, $v0\n");
            }
            return;
        }
        case AST_BINOP: {
            if (expr->child_count < 2) { fprintf(ec->out, "    li   $v0, 0\n"); return; }
            emit_eval_num(ec, expr->children[0]);
            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
            fprintf(ec->out, "    sw   $v0, 0($sp)\n");
            emit_eval_num(ec, expr->children[1]);
            fprintf(ec->out, "    lw   $t0, 0($sp)\n");
            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            const char *op = expr->text ? expr->text : "+";
            if (strcmp(op, "+") == 0) {
                fprintf(ec->out, "    addu $v0, $t0, $v0\n");
            } else if (strcmp(op, "-") == 0) {
                fprintf(ec->out, "    subu $v0, $t0, $v0\n");
            } else if (strcmp(op, "*") == 0) {
                fprintf(ec->out, "    move $a0, $t0\n");
                fprintf(ec->out, "    move $a1, $v0\n");
                fprintf(ec->out, "    li   $a2, %d\n", pc->prog_scale);
                fprintf(ec->out, "    jal  _rt_mul_scaled\n");
            } else if (strcmp(op, "/") == 0) {
                fprintf(ec->out, "    move $a0, $t0\n");
                fprintf(ec->out, "    move $a1, $v0\n");
                fprintf(ec->out, "    li   $a2, %d\n", pc->prog_scale);
                fprintf(ec->out, "    jal  _rt_div_scaled\n");
            } else if (strcmp(op, "**") == 0) {
                fprintf(ec->out, "    move $a0, $t0\n");
                fprintf(ec->out, "    move $a1, $v0\n");
                fprintf(ec->out, "    li   $a2, %d\n", pc->prog_scale);
                fprintf(ec->out, "    jal  _rt_pow_scaled\n");
            }
            return;
        }
        case AST_FUNC_CALL: {
            const char *fn = expr->text ? expr->text : "";
            if (ci_cmp(fn, "LENGTH") == 0 && expr->child_count > 0) {
                emit_addr_len(ec, expr->children[0], NULL);
                fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                fprintf(ec->out, "    mult $v1, $t0\n");
                fprintf(ec->out, "    mflo $v0\n");
            } else if (ci_cmp(fn, "ABS") == 0 && expr->child_count > 0) {
                emit_eval_num(ec, expr->children[0]);
                int lbl = new_lbl(ec);
                fprintf(ec->out, "    bgez $v0, L%d\n", lbl);
                fprintf(ec->out, "    subu $v0, $zero, $v0\n");
                fprintf(ec->out, "L%d:\n", lbl);
            } else if ((ci_cmp(fn, "INTEGER") == 0 || ci_cmp(fn, "INTEGER-PART") == 0) && expr->child_count > 0) {
                emit_eval_num(ec, expr->children[0]);
                if (pc->prog_scale > 1) {
                    fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    div  $v0, $t0\n");
                    fprintf(ec->out, "    mflo $v0\n");
                    fprintf(ec->out, "    mult $v0, $t0\n");
                    fprintf(ec->out, "    mflo $v0\n");
                }
            } else if (ci_cmp(fn, "MOD") == 0 && expr->child_count >= 2) {
                emit_eval_num(ec, expr->children[0]);
                fprintf(ec->out, "    addiu $sp, $sp, -4\n");
                fprintf(ec->out, "    sw   $v0, 0($sp)\n");
                emit_eval_num(ec, expr->children[1]);
                fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                fprintf(ec->out, "    addiu $sp, $sp, 4\n");
                int lbl = new_lbl(ec);
                fprintf(ec->out, "    beq  $v0, $zero, L%d\n", lbl);
                fprintf(ec->out, "    div  $t0, $v0\n");
                fprintf(ec->out, "    mfhi $v0\n");
                fprintf(ec->out, "L%d:\n", lbl);
            } else if (ci_cmp(fn, "ORD") == 0 && expr->child_count > 0) {
                emit_addr_len(ec, expr->children[0], NULL);
                fprintf(ec->out, "    lbu  $v0, 0($v0)\n");
                fprintf(ec->out, "    addiu $v0, $v0, 1\n");
                if (pc->prog_scale > 1) {
                    fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    mult $v0, $t0\n");
                    fprintf(ec->out, "    mflo $v0\n");
                }
            } else if (ci_cmp(fn, "NUMVAL") == 0 || ci_cmp(fn, "NUMVAL-C") == 0) {
                if (expr->child_count > 0) {
                    emit_addr_len(ec, expr->children[0], NULL);
                    fprintf(ec->out, "    move $a0, $v0\n");
                    fprintf(ec->out, "    move $a1, $v1\n");
                    fprintf(ec->out, "    li   $a2, 0\n");
                    fprintf(ec->out, "    li   $a3, %d\n", pc->prog_frac);
                    fprintf(ec->out, "    jal  _rt_parse_num\n");
                } else {
                    fprintf(ec->out, "    li   $v0, 0\n");
                }
            } else if (ci_cmp(fn, "RANDOM") == 0) {
                fprintf(ec->out, "    la   $t0, _rt_rng_seed\n");
                fprintf(ec->out, "    lw   $t1, 0($t0)\n");
                fprintf(ec->out, "    li   $t2, 1103515245\n");
                fprintf(ec->out, "    mult $t1, $t2\n");
                fprintf(ec->out, "    mflo $t1\n");
                fprintf(ec->out, "    addiu $t1, $t1, 12345\n");
                fprintf(ec->out, "    sw   $t1, 0($t0)\n");
                fprintf(ec->out, "    srl  $t1, $t1, 16\n");
                fprintf(ec->out, "    andi $t1, $t1, 0x7FFF\n");
                fprintf(ec->out, "    li   $t2, %d\n", pc->prog_scale);
                fprintf(ec->out, "    div  $t1, $t2\n");
                fprintf(ec->out, "    mfhi $v0\n");
            } else if (expr->child_count > 0) {
                emit_eval_num(ec, expr->children[0]);
            } else {
                fprintf(ec->out, "    li   $v0, 0\n");
            }
            return;
        }
        default:
            fprintf(ec->out, "    li   $v0, 0\n");
            return;
    }
}

/* Store scaled numeric value currently in $v0 into target_ref */
static void emit_store_num(EmitCtx *ec, const ASTNode *target_ref, bool val_in_v0) {
    ProgCtx *pc = ec->cur;
    (void)val_in_v0;
    fprintf(ec->out, "    addiu $sp, $sp, -4\n");
    fprintf(ec->out, "    sw   $v0, 0($sp)\n");

    int vi = -1;
    emit_data_ref_addr_len(ec, target_ref, &vi);
    fprintf(ec->out, "    lw   $a2, 0($sp)\n");
    fprintf(ec->out, "    addiu $sp, $sp, 4\n");

    if (vi < 0) return;
    const VarLayout *v = &pc->vars[vi];

    /* Apply COBOL ROUNDED if requested (target_ref->ival == 1) and losing fractional digits */
    if (target_ref->ival == 1 && pc->prog_frac > v->frac_digits) {
        int half = pow10_int(pc->prog_frac - v->frac_digits) / 2;
        int lbl_neg = new_lbl(ec), lbl_rd = new_lbl(ec);
        fprintf(ec->out, "    bltz $a2, L%d\n", lbl_neg);
        fprintf(ec->out, "    addiu $a2, $a2, %d\n", half);
        fprintf(ec->out, "    j    L%d\n", lbl_rd);
        fprintf(ec->out, "L%d:\n", lbl_neg);
        fprintf(ec->out, "    addiu $a2, $a2, %d\n", -half);
        fprintf(ec->out, "L%d:\n", lbl_rd);
    }

    if (v->is_edited && v->pic_str_id >= 0) {
        int packed = (pc->prog_frac << 8) | ((v->frac_digits & 0x7F) << 1) | (pc->decimal_comma ? 1 : 0);
        fprintf(ec->out, "    move $a0, $v0\n");
        fprintf(ec->out, "    la   $a1, cob_s%d\n", v->pic_str_id);
        fprintf(ec->out, "    li   $a3, %d\n", packed);
        fprintf(ec->out, "    jal  _rt_format_edited\n");
    } else if (v->kind == DATA_NUMERIC || v->kind == DATA_INDEX) {
        int packed = (pc->prog_frac << 8) | ((v->frac_digits & 0x7F) << 1) | (v->is_signed ? 1 : 0);
        fprintf(ec->out, "    move $a0, $v0\n");
        fprintf(ec->out, "    move $a1, $v1\n");
        fprintf(ec->out, "    li   $a3, %d\n", packed);
        fprintf(ec->out, "    jal  _rt_format_num\n");
    } else {
        /* Numeric value moved into alphanumeric field */
        int packed = (pc->prog_frac << 8) | (0 << 1) | 0;
        fprintf(ec->out, "    move $a0, $v0\n");
        fprintf(ec->out, "    move $a1, $v1\n");
        fprintf(ec->out, "    li   $a3, %d\n", packed);
        fprintf(ec->out, "    jal  _rt_format_num\n");
    }
}

/* =========================================================================
 * MOVE Emission
 * ========================================================================= */

static void emit_move_one(EmitCtx *ec, const ASTNode *src, const ASTNode *dst_ref) {
    ProgCtx *pc = ec->cur;
    int dvi = resolve_data_ref(pc, dst_ref);
    const VarLayout *dv = (dvi >= 0) ? &pc->vars[dvi] : NULL;

    /* 1. Figurative constants (SPACES, ZEROS, LOW-VALUES, HIGH-VALUES) */
    if (src && src->kind == AST_FIGURATIVE && src->text) {
        if ((ci_cmp(src->text, "ZERO") == 0 || ci_cmp(src->text, "ZEROS") == 0 || ci_cmp(src->text, "ZEROES") == 0) &&
            dv && (dv->kind == DATA_NUMERIC || dv->kind == DATA_NUMERIC_EDITED || dv->kind == DATA_INDEX)) {
            fprintf(ec->out, "    li   $v0, 0\n");
            emit_store_num(ec, dst_ref, true);
            return;
        }
        int fill_ch = ' ';
        if (ci_cmp(src->text, "ZERO") == 0 || ci_cmp(src->text, "ZEROS") == 0 || ci_cmp(src->text, "ZEROES") == 0)
            fill_ch = '0';
        else if (ci_cmp(src->text, "LOW-VALUE") == 0 || ci_cmp(src->text, "LOW-VALUES") == 0)
            fill_ch = 0;
        else if (ci_cmp(src->text, "HIGH-VALUE") == 0 || ci_cmp(src->text, "HIGH-VALUES") == 0)
            fill_ch = 0xFF;
        else if (ci_cmp(src->text, "QUOTE") == 0 || ci_cmp(src->text, "QUOTES") == 0)
            fill_ch = '"';
        emit_data_ref_addr_len(ec, dst_ref, NULL);
        fprintf(ec->out, "    move $a0, $v0\n");
        fprintf(ec->out, "    li   $a1, %d\n", fill_ch);
        fprintf(ec->out, "    move $a2, $v1\n");
        fprintf(ec->out, "    jal  _rt_memset\n");
        return;
    }

    /* 2. Large unsigned numeric target (>=10 digits, e.g. 11-digit CPF) -> digit copy (no 32-bit overflow!) */
    if (dv && is_large_uint_var(dv)) {
        emit_addr_len(ec, src, NULL);
        fprintf(ec->out, "    addiu $sp, $sp, -8\n");
        fprintf(ec->out, "    sw   $v0, 0($sp)\n");
        fprintf(ec->out, "    sw   $v1, 4($sp)\n");
        emit_data_ref_addr_len(ec, dst_ref, NULL);
        fprintf(ec->out, "    move $a0, $v0\n");
        fprintf(ec->out, "    move $a1, $v1\n");
        fprintf(ec->out, "    lw   $a2, 0($sp)\n");
        fprintf(ec->out, "    lw   $a3, 4($sp)\n");
        fprintf(ec->out, "    addiu $sp, $sp, 8\n");
        fprintf(ec->out, "    jal  _rt_move_digits\n");
        return;
    }

    /* 3. Destination is numeric / numeric-edited / index */
    bool has_refmod = false;
    for (size_t i = 0; dst_ref && i < dst_ref->child_count; i++) {
        if (dst_ref->children[i]->kind == AST_REFMOD) has_refmod = true;
    }
    if (dv && !has_refmod && (dv->kind == DATA_NUMERIC || dv->kind == DATA_NUMERIC_EDITED || dv->kind == DATA_INDEX)) {
        emit_eval_num(ec, src);
        emit_store_num(ec, dst_ref, true);
        return;
    }

    /* 4. Alphanumeric / Group / RefMod move */
    emit_addr_len(ec, src, NULL);
    fprintf(ec->out, "    addiu $sp, $sp, -8\n");
    fprintf(ec->out, "    sw   $v0, 0($sp)\n");
    fprintf(ec->out, "    sw   $v1, 4($sp)\n");
    emit_data_ref_addr_len(ec, dst_ref, NULL);
    fprintf(ec->out, "    move $a0, $v0\n");
    fprintf(ec->out, "    move $a1, $v1\n");
    fprintf(ec->out, "    lw   $a2, 0($sp)\n");
    fprintf(ec->out, "    lw   $a3, 4($sp)\n");
    fprintf(ec->out, "    addiu $sp, $sp, 8\n");
    fprintf(ec->out, "    jal  _rt_move_alnum\n");
}

/* =========================================================================
 * Condition Evaluation -> result 0 or 1 in $v0
 * ========================================================================= */

static bool is_numeric_expr(const ProgCtx *pc, const ASTNode *n) {
    if (!n) return false;
    if (n->kind == AST_LITERAL_NUM || n->kind == AST_BINOP || n->kind == AST_UNOP || n->kind == AST_LENGTH_OF)
        return true;
    if (n->kind == AST_FIGURATIVE && n->text &&
        (ci_cmp(n->text, "ZERO") == 0 || ci_cmp(n->text, "ZEROS") == 0 || ci_cmp(n->text, "ZEROES") == 0))
        return true;
    if (n->kind == AST_DATA_REF) {
        for (size_t i = 0; i < n->child_count; i++) {
            if (n->children[i]->kind == AST_REFMOD) return false;
        }
        int vi = resolve_data_ref(pc, n);
        if (vi >= 0 && (pc->vars[vi].kind == DATA_NUMERIC || pc->vars[vi].kind == DATA_INDEX))
            return true;
    }
    return false;
}

static bool is_large_numeric_expr(const ProgCtx *pc, const ASTNode *n) {
    if (!n || n->kind != AST_DATA_REF) return false;
    int vi = resolve_data_ref(pc, n);
    return vi >= 0 && is_large_uint_var(&pc->vars[vi]);
}

/* Given comparison result (<0, 0, >0) in $v0, map to 0/1 according to relop string */
static void emit_relop_test(EmitCtx *ec, const char *op) {
    bool neg = false;
    if (strncmp(op, "NOT ", 4) == 0) {
        neg = true;
        op += 4;
    }
    int lbl_t = new_lbl(ec), lbl_d = new_lbl(ec);
    if (strcmp(op, "=") == 0)       fprintf(ec->out, "    beq  $v0, $zero, L%d\n", lbl_t);
    else if (strcmp(op, "<>") == 0) fprintf(ec->out, "    bne  $v0, $zero, L%d\n", lbl_t);
    else if (strcmp(op, "<") == 0)  fprintf(ec->out, "    bltz $v0, L%d\n", lbl_t);
    else if (strcmp(op, "<=") == 0) fprintf(ec->out, "    blez $v0, L%d\n", lbl_t);
    else if (strcmp(op, ">") == 0)  fprintf(ec->out, "    bgtz $v0, L%d\n", lbl_t);
    else if (strcmp(op, ">=") == 0) fprintf(ec->out, "    bgez $v0, L%d\n", lbl_t);
    else                            fprintf(ec->out, "    beq  $v0, $zero, L%d\n", lbl_t);

    fprintf(ec->out, "    li   $v0, %d\n", neg ? 1 : 0);
    fprintf(ec->out, "    j    L%d\n", lbl_d);
    fprintf(ec->out, "L%d:\n", lbl_t);
    fprintf(ec->out, "    li   $v0, %d\n", neg ? 0 : 1);
    fprintf(ec->out, "L%d:\n", lbl_d);
}

/* Compare lhs vs rhs -> <0, 0, >0 in $v0 */
static void emit_compare_vals(EmitCtx *ec, const ASTNode *lhs, const ASTNode *rhs) {
    ProgCtx *pc = ec->cur;

    /* Special case: 11-digit CPF or similar large unsigned numeric */
    if (is_large_numeric_expr(pc, lhs) || is_large_numeric_expr(pc, rhs)) {
        emit_addr_len(ec, lhs, NULL);
        fprintf(ec->out, "    addiu $sp, $sp, -8\n");
        fprintf(ec->out, "    sw   $v0, 0($sp)\n");
        fprintf(ec->out, "    sw   $v1, 4($sp)\n");
        emit_addr_len(ec, rhs, NULL);
        fprintf(ec->out, "    lw   $a0, 0($sp)\n");
        fprintf(ec->out, "    lw   $a1, 4($sp)\n");
        fprintf(ec->out, "    addiu $sp, $sp, 8\n");
        fprintf(ec->out, "    move $a2, $v0\n");
        fprintf(ec->out, "    move $a3, $v1\n");
        fprintf(ec->out, "    jal  _rt_cmp_digits\n");
        return;
    }

    /* Special case: comparing against figurative constant (SPACES, ZEROS, LOW-VALUES, HIGH-VALUES) */
    if (rhs && rhs->kind == AST_FIGURATIVE && rhs->text) {
        if ((ci_cmp(rhs->text, "ZERO") == 0 || ci_cmp(rhs->text, "ZEROS") == 0 || ci_cmp(rhs->text, "ZEROES") == 0) &&
            is_numeric_expr(pc, lhs)) {
            emit_eval_num(ec, lhs);
            return; /* $v0 - 0 == $v0 */
        }
        /* Compare every byte of lhs against figurative char */
        int ch = ' ';
        if (ci_cmp(rhs->text, "ZERO") == 0 || ci_cmp(rhs->text, "ZEROS") == 0 || ci_cmp(rhs->text, "ZEROES") == 0) ch = '0';
        else if (ci_cmp(rhs->text, "LOW-VALUE") == 0 || ci_cmp(rhs->text, "LOW-VALUES") == 0) ch = 0;
        else if (ci_cmp(rhs->text, "HIGH-VALUE") == 0 || ci_cmp(rhs->text, "HIGH-VALUES") == 0) ch = 0xFF;
        else if (ci_cmp(rhs->text, "QUOTE") == 0 || ci_cmp(rhs->text, "QUOTES") == 0) ch = '"';

        emit_addr_len(ec, lhs, NULL);
        int lp = new_lbl(ec), ldiff = new_lbl(ec), leq = new_lbl(ec);
        fprintf(ec->out, "    move $t0, $v0\n");
        fprintf(ec->out, "    move $t1, $v1\n");
        fprintf(ec->out, "    li   $t2, %d\n", ch);
        fprintf(ec->out, "L%d:\n", lp);
        fprintf(ec->out, "    blez $t1, L%d\n", leq);
        fprintf(ec->out, "    lbu  $t3, 0($t0)\n");
        fprintf(ec->out, "    subu $v0, $t3, $t2\n");
        fprintf(ec->out, "    bne  $v0, $zero, L%d\n", ldiff);
        fprintf(ec->out, "    addiu $t0, $t0, 1\n");
        fprintf(ec->out, "    addiu $t1, $t1, -1\n");
        fprintf(ec->out, "    j    L%d\n", lp);
        fprintf(ec->out, "L%d:\n", leq);
        fprintf(ec->out, "    li   $v0, 0\n");
        fprintf(ec->out, "L%d:\n", ldiff);
        return;
    }

    if (is_numeric_expr(pc, lhs) && is_numeric_expr(pc, rhs)) {
        emit_eval_num(ec, lhs);
        fprintf(ec->out, "    addiu $sp, $sp, -4\n");
        fprintf(ec->out, "    sw   $v0, 0($sp)\n");
        emit_eval_num(ec, rhs);
        fprintf(ec->out, "    lw   $t0, 0($sp)\n");
        fprintf(ec->out, "    addiu $sp, $sp, 4\n");
        /* Safe signed comparison (avoid overflow on subtraction) */
        int l_lt = new_lbl(ec), l_gt = new_lbl(ec), l_dn = new_lbl(ec);
        fprintf(ec->out, "    blt  $t0, $v0, L%d\n", l_lt);
        fprintf(ec->out, "    bgt  $t0, $v0, L%d\n", l_gt);
        fprintf(ec->out, "    li   $v0, 0\n");
        fprintf(ec->out, "    j    L%d\n", l_dn);
        fprintf(ec->out, "L%d:\n", l_lt);
        fprintf(ec->out, "    li   $v0, -1\n");
        fprintf(ec->out, "    j    L%d\n", l_dn);
        fprintf(ec->out, "L%d:\n", l_gt);
        fprintf(ec->out, "    li   $v0, 1\n");
        fprintf(ec->out, "L%d:\n", l_dn);
        return;
    }

    /* Alphanumeric comparison */
    emit_addr_len(ec, lhs, NULL);
    fprintf(ec->out, "    addiu $sp, $sp, -8\n");
    fprintf(ec->out, "    sw   $v0, 0($sp)\n");
    fprintf(ec->out, "    sw   $v1, 4($sp)\n");
    emit_addr_len(ec, rhs, NULL);
    fprintf(ec->out, "    lw   $a0, 0($sp)\n");
    fprintf(ec->out, "    lw   $a1, 4($sp)\n");
    fprintf(ec->out, "    addiu $sp, $sp, 8\n");
    fprintf(ec->out, "    move $a2, $v0\n");
    fprintf(ec->out, "    move $a3, $v1\n");
    fprintf(ec->out, "    jal  _rt_cmp_alnum\n");
}

static void emit_cond_class(EmitCtx *ec, const ASTNode *subject, const char *cls, bool negated) {
    if (!cls) { fprintf(ec->out, "    li   $v0, 0\n"); return; }
    if (ci_cmp(cls, "NUMERIC") == 0) {
        emit_addr_len(ec, subject, NULL);
        fprintf(ec->out, "    move $a0, $v0\n");
        fprintf(ec->out, "    move $a1, $v1\n");
        fprintf(ec->out, "    jal  _rt_is_numeric\n");
    } else if (ci_cmp(cls, "ALPHABETIC") == 0) {
        emit_addr_len(ec, subject, NULL);
        fprintf(ec->out, "    move $a0, $v0\n");
        fprintf(ec->out, "    move $a1, $v1\n");
        fprintf(ec->out, "    jal  _rt_is_alpha\n");
    } else if (ci_cmp(cls, "POSITIVE") == 0) {
        emit_eval_num(ec, subject);
        emit_relop_test(ec, ">");
    } else if (ci_cmp(cls, "NEGATIVE") == 0) {
        emit_eval_num(ec, subject);
        emit_relop_test(ec, "<");
    } else {
        fprintf(ec->out, "    li   $v0, 1\n");
    }
    if (negated) {
        fprintf(ec->out, "    xori $v0, $v0, 1\n");
    }
}

/* Evaluate level-88 condition name -> 0 or 1 in $v0 */
static void emit_level88_cond(EmitCtx *ec, int vi) {
    ProgCtx *pc = ec->cur;
    const VarLayout *v = &pc->vars[vi];
    if (v->parent_idx < 0 || !v->val_ast || v->val_ast->child_count == 0) {
        fprintf(ec->out, "    li   $v0, 0\n");
        return;
    }
    ASTNode parent_ref;
    memset(&parent_ref, 0, sizeof(parent_ref));
    parent_ref.kind = AST_DATA_REF;
    parent_ref.text = pc->vars[v->parent_idx].name;

    int l_true = new_lbl(ec), l_done = new_lbl(ec);
    for (size_t i = 0; i < v->val_ast->child_count; i++) {
        const ASTNode *item = v->val_ast->children[i];
        if (item->kind == AST_BINOP && item->text && ci_cmp(item->text, "THRU") == 0 && item->child_count >= 2) {
            int l_next = new_lbl(ec);
            emit_compare_vals(ec, &parent_ref, item->children[0]);
            fprintf(ec->out, "    bltz $v0, L%d\n", l_next);
            emit_compare_vals(ec, &parent_ref, item->children[1]);
            fprintf(ec->out, "    blez $v0, L%d\n", l_true);
            fprintf(ec->out, "L%d:\n", l_next);
        } else {
            emit_compare_vals(ec, &parent_ref, item);
            fprintf(ec->out, "    beq  $v0, $zero, L%d\n", l_true);
        }
    }
    fprintf(ec->out, "    li   $v0, 0\n");
    fprintf(ec->out, "    j    L%d\n", l_done);
    fprintf(ec->out, "L%d:\n", l_true);
    fprintf(ec->out, "    li   $v0, 1\n");
    fprintf(ec->out, "L%d:\n", l_done);
}

static void emit_cond(EmitCtx *ec, const ASTNode *cond, CondCtx *cctx) {
    ProgCtx *pc = ec->cur;
    if (!cond) {
        fprintf(ec->out, "    li   $v0, 0\n");
        return;
    }

    switch (cond->kind) {
        case AST_COND_NOT: {
            if (cond->child_count > 0) emit_cond(ec, cond->children[0], cctx);
            fprintf(ec->out, "    xori $v0, $v0, 1\n");
            return;
        }
        case AST_COND_LOGIC: {
            if (cond->child_count < 2) { fprintf(ec->out, "    li   $v0, 0\n"); return; }
            bool is_and = (cond->text && ci_cmp(cond->text, "AND") == 0);
            int l_short = new_lbl(ec), l_done = new_lbl(ec);
            emit_cond(ec, cond->children[0], cctx);
            if (is_and) {
                fprintf(ec->out, "    beq  $v0, $zero, L%d\n", l_short);
            } else {
                fprintf(ec->out, "    bne  $v0, $zero, L%d\n", l_short);
            }
            emit_cond(ec, cond->children[1], cctx);
            fprintf(ec->out, "    j    L%d\n", l_done);
            fprintf(ec->out, "L%d:\n", l_short);
            fprintf(ec->out, "    li   $v0, %d\n", is_and ? 0 : 1);
            fprintf(ec->out, "L%d:\n", l_done);
            return;
        }
        case AST_COND_REL: {
            const char *op = cond->text ? cond->text : "=";
            if (cond->child_count >= 2) {
                if (cctx) { cctx->last_lhs = cond->children[0]; cctx->last_op = op; }
                emit_compare_vals(ec, cond->children[0], cond->children[1]);
                emit_relop_test(ec, op);
            } else if (cond->child_count == 1 && cctx && cctx->last_lhs) {
                cctx->last_op = op;
                emit_compare_vals(ec, cctx->last_lhs, cond->children[0]);
                emit_relop_test(ec, op);
            } else {
                fprintf(ec->out, "    li   $v0, 0\n");
            }
            return;
        }
        case AST_COND_CLASS: {
            bool neg = (cond->extra && ci_cmp(cond->extra, "NOT") == 0);
            if (cond->child_count > 0) {
                emit_cond_class(ec, cond->children[0], cond->text, neg);
            } else {
                fprintf(ec->out, "    li   $v0, 0\n");
            }
            return;
        }
        case AST_DATA_REF: {
            int vi = resolve_data_ref(pc, cond);
            if (vi >= 0 && pc->vars[vi].kind == DATA_COND) {
                emit_level88_cond(ec, vi);
                return;
            }
            /* Abbreviated RHS: IF X = A OR B */
            if (cctx && cctx->last_lhs && cctx->last_op) {
                emit_compare_vals(ec, cctx->last_lhs, cond);
                emit_relop_test(ec, cctx->last_op);
                return;
            }
            emit_eval_num(ec, cond);
            emit_relop_test(ec, "<>");
            return;
        }
        default: {
            /* Abbreviated RHS literal/figurative: IF X = 'S' OR 'U' */
            if (cctx && cctx->last_lhs && cctx->last_op) {
                emit_compare_vals(ec, cctx->last_lhs, cond);
                emit_relop_test(ec, cctx->last_op);
                return;
            }
            if (cond->kind == AST_FIGURATIVE && cond->text && ci_cmp(cond->text, "TRUE") == 0) {
                fprintf(ec->out, "    li   $v0, 1\n");
            } else {
                fprintf(ec->out, "    li   $v0, 0\n");
            }
            return;
        }
    }
}

/* =========================================================================
 * File Status Helper
 * ========================================================================= */

static void emit_set_file_status(EmitCtx *ec, int fi, const char *code2) {
    ProgCtx *pc = ec->cur;
    if (fi < 0 || fi >= (int)pc->file_count) return;
    int svi = pc->files[fi].status_var_idx;
    if (svi < 0) return;

    int sid = strpool_add(ec, code2, 2, false);
    ASTNode sref;
    memset(&sref, 0, sizeof(sref));
    sref.kind = AST_DATA_REF;
    sref.text = pc->vars[svi].name;
    emit_data_ref_addr_len(ec, &sref, NULL);
    fprintf(ec->out, "    move $a0, $v0\n");
    fprintf(ec->out, "    move $a1, $v1\n");
    fprintf(ec->out, "    la   $a2, cob_s%d\n", sid);
    fprintf(ec->out, "    li   $a3, 2\n");
    fprintf(ec->out, "    jal  _rt_move_alnum\n");
}

/* =========================================================================
 * PERFORM Helper
 * ========================================================================= */

static void emit_perform_call_target(EmitCtx *ec, const char *target_name, const char *thru_name) {
    ProgCtx *pc = ec->cur;
    int b_start = find_block_by_name(pc, target_name);
    if (b_start < 0) return;

    int b_end = b_start;
    if (thru_name) {
        int bt = find_block_by_name(pc, thru_name);
        if (bt >= 0) b_end = pc->blocks[bt].is_section ? pc->blocks[bt].sec_end_block : bt;
    } else if (pc->blocks[b_start].is_section) {
        b_end = pc->blocks[b_start].sec_end_block;
    }

    int ret_lbl = new_lbl(ec);
    fprintf(ec->out, "    li   $a0, %d\n", pc->blocks[b_end].global_id);
    fprintf(ec->out, "    la   $a1, L%d\n", ret_lbl);
    fprintf(ec->out, "    jal  _rt_perf_push\n");
    fprintf(ec->out, "    j    p%d_b%d\n", pc->prog_id, b_start);
    fprintf(ec->out, "L%d:\n", ret_lbl);
}

static void emit_perform_body(EmitCtx *ec, const ASTNode *st, bool is_inline,
                              const char *target_name, const char *thru_name, size_t inline_start) {
    if (is_inline) {
        emit_stmt_list(ec, st, inline_start);
    } else {
        emit_perform_call_target(ec, target_name, thru_name);
    }
}

static void emit_perform_varying_rec(EmitCtx *ec, const ASTNode *st, const ASTNode *ctrl,
                                     size_t after_idx, bool is_inline,
                                     const char *target_name, const char *thru_name, size_t inline_start) {
    const ASTNode *var_ref, *from_rv, *by_rv, *until_cond;
    if (after_idx == 0) {
        if (ctrl->child_count < 4) return;
        var_ref = ctrl->children[0];
        from_rv = ctrl->children[1];
        by_rv = ctrl->children[2];
        until_cond = ctrl->children[3];
    } else {
        const ASTNode *af = ctrl->children[3 + after_idx];
        if (!af || af->child_count < 4) return;
        var_ref = af->children[0];
        from_rv = af->children[1];
        by_rv = af->children[2];
        until_cond = af->children[3];
    }

    /* Initialize loop var = from_rv */
    emit_move_one(ec, from_rv, var_ref);

    int l_loop = new_lbl(ec), l_end = new_lbl(ec);
    fprintf(ec->out, "L%d:\n", l_loop);
    CondCtx cc = { NULL, NULL };
    emit_cond(ec, until_cond, &cc);
    fprintf(ec->out, "    bne  $v0, $zero, L%d\n", l_end);

    if (4 + after_idx < ctrl->child_count) {
        emit_perform_varying_rec(ec, st, ctrl, after_idx + 1, is_inline, target_name, thru_name, inline_start);
    } else {
        emit_perform_body(ec, st, is_inline, target_name, thru_name, inline_start);
    }

    /* var = var + by_rv */
    emit_eval_num(ec, var_ref);
    fprintf(ec->out, "    addiu $sp, $sp, -4\n");
    fprintf(ec->out, "    sw   $v0, 0($sp)\n");
    emit_eval_num(ec, by_rv);
    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
    fprintf(ec->out, "    addiu $sp, $sp, 4\n");
    fprintf(ec->out, "    addu $v0, $t0, $v0\n");
    emit_store_num(ec, var_ref, true);
    fprintf(ec->out, "    j    L%d\n", l_loop);
    fprintf(ec->out, "L%d:\n", l_end);
}

/* =========================================================================
 * Statement Code Generation
 * ========================================================================= */

static void emit_stmt_list(EmitCtx *ec, const ASTNode *parent, size_t start_idx) {
    if (!parent) return;
    for (size_t i = start_idx; i < parent->child_count; i++) {
        emit_stmt(ec, parent->children[i]);
    }
}

static void emit_stmt(EmitCtx *ec, const ASTNode *st) {
    ProgCtx *pc = ec->cur;
    if (!st) return;

    switch (st->kind) {
        /* --- DISPLAY --- */
        case AST_STMT_DISPLAY: {
            for (size_t i = 0; i < st->child_count; i++) {
                const ASTNode *item = st->children[i];
                if (!item || item->kind == AST_CLAUSE) continue;
                int vi = -1;
                if (item->kind == AST_DATA_REF) {
                    emit_data_ref_addr_len(ec, item, &vi);
                    if (vi >= 0 && pc->vars[vi].kind == DATA_NUMERIC && pc->vars[vi].frac_digits > 0) {
                        fprintf(ec->out, "    move $a0, $v0\n");
                        fprintf(ec->out, "    move $a1, $v1\n");
                        fprintf(ec->out, "    li   $a2, %d\n", pc->vars[vi].frac_digits);
                        fprintf(ec->out, "    li   $a3, %d\n", pc->decimal_comma ? 1 : 0);
                        fprintf(ec->out, "    jal  _rt_print_num_field\n");
                        continue;
                    }
                    fprintf(ec->out, "    move $a0, $v0\n");
                    fprintf(ec->out, "    move $a1, $v1\n");
                    fprintf(ec->out, "    jal  _rt_print_bytes\n");
                } else {
                    emit_addr_len(ec, item, NULL);
                    fprintf(ec->out, "    move $a0, $v0\n");
                    fprintf(ec->out, "    move $a1, $v1\n");
                    fprintf(ec->out, "    jal  _rt_print_bytes\n");
                }
            }
            if (st->ival == 0) {
                fprintf(ec->out, "    jal  _rt_print_nl\n");
            }
            return;
        }

        /* --- ACCEPT --- */
        case AST_STMT_ACCEPT: {
            if (st->child_count == 0) return;
            const ASTNode *dst = st->children[0];
            int vi = -1;
            emit_data_ref_addr_len(ec, dst, &vi);
            if (st->text) {
                /* ACCEPT x FROM DATE / DAY / DAY-OF-WEEK / TIME */
                int mode = 0;
                if (ci_cmp(st->text, "DATE") == 0) {
                    mode = (st->extra && ci_cmp(st->extra, "YYYYMMDD") == 0) ? 0 : 1;
                } else if (ci_cmp(st->text, "DAY") == 0) {
                    mode = 2;
                } else if (ci_cmp(st->text, "DAY-OF-WEEK") == 0) {
                    mode = 3;
                } else if (ci_cmp(st->text, "TIME") == 0) {
                    mode = 4;
                }
                fprintf(ec->out, "    move $a0, $v0\n");
                fprintf(ec->out, "    move $a1, $v1\n");
                fprintf(ec->out, "    li   $a2, %d\n", mode);
                fprintf(ec->out, "    jal  _rt_accept_datetime\n");
                return;
            }
            if (vi >= 0 && is_large_uint_var(&pc->vars[vi])) {
                fprintf(ec->out, "    move $a0, $v0\n");
                fprintf(ec->out, "    move $a1, $v1\n");
                fprintf(ec->out, "    jal  _rt_accept_digits\n");
            } else if (vi >= 0 && (pc->vars[vi].kind == DATA_NUMERIC || pc->vars[vi].kind == DATA_INDEX)) {
                int packed = (pc->prog_frac << 1) | (pc->vars[vi].is_signed ? 1 : 0);
                fprintf(ec->out, "    move $a0, $v0\n");
                fprintf(ec->out, "    move $a1, $v1\n");
                fprintf(ec->out, "    li   $a2, %d\n", pc->vars[vi].frac_digits);
                fprintf(ec->out, "    li   $a3, %d\n", packed);
                fprintf(ec->out, "    jal  _rt_accept_num\n");
            } else {
                fprintf(ec->out, "    move $a0, $v0\n");
                fprintf(ec->out, "    move $a1, $v1\n");
                fprintf(ec->out, "    jal  _rt_accept_str\n");
            }
            return;
        }

        /* --- MOVE --- */
        case AST_STMT_MOVE: {
            if (st->child_count < 2) return;
            const ASTNode *src = st->children[0];
            for (size_t i = 1; i < st->child_count; i++) {
                emit_move_one(ec, src, st->children[i]);
            }
            return;
        }

        /* --- ADD --- */
        case AST_STMT_ADD: {
            if (st->child_count < 2) return;
            const ASTNode *lhs_list = st->children[0];
            const ASTNode *rhs_list = st->children[1];
            const ASTNode *giving = (st->child_count >= 3 && st->children[2] && st->children[2]->kind == AST_CLAUSE &&
                                     st->children[2]->text && ci_cmp(st->children[2]->text, "GIVING") == 0)
                                    ? st->children[2] : NULL;
            /* Compute sum of lhs_list into stack top */
            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
            fprintf(ec->out, "    sw   $zero, 0($sp)\n");
            if (lhs_list->kind == AST_LIST) {
                for (size_t i = 0; i < lhs_list->child_count; i++) {
                    emit_eval_num(ec, lhs_list->children[i]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    fprintf(ec->out, "    addu $t0, $t0, $v0\n");
                    fprintf(ec->out, "    sw   $t0, 0($sp)\n");
                }
            } else {
                emit_eval_num(ec, lhs_list);
                fprintf(ec->out, "    sw   $v0, 0($sp)\n");
            }
            if (st->text && ci_cmp(st->text, "GIVING") == 0) {
                /* ADD a b GIVING c d */
                if (rhs_list->kind == AST_LIST) {
                    for (size_t i = 0; i < rhs_list->child_count; i++) {
                        fprintf(ec->out, "    lw   $v0, 0($sp)\n");
                        emit_store_num(ec, rhs_list->children[i], true);
                    }
                }
            } else if (giving) {
                /* ADD a TO b GIVING c */
                if (rhs_list->kind == AST_LIST && rhs_list->child_count > 0) {
                    emit_eval_num(ec, rhs_list->children[0]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    fprintf(ec->out, "    addu $t0, $t0, $v0\n");
                    fprintf(ec->out, "    sw   $t0, 0($sp)\n");
                }
                for (size_t i = 0; i < giving->child_count; i++) {
                    fprintf(ec->out, "    lw   $v0, 0($sp)\n");
                    emit_store_num(ec, giving->children[i], true);
                }
            } else if (rhs_list->kind == AST_LIST) {
                /* ADD a TO b c */
                for (size_t i = 0; i < rhs_list->child_count; i++) {
                    emit_eval_num(ec, rhs_list->children[i]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    fprintf(ec->out, "    addu $v0, $v0, $t0\n");
                    emit_store_num(ec, rhs_list->children[i], true);
                }
            }
            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            return;
        }

        /* --- SUBTRACT --- */
        case AST_STMT_SUBTRACT: {
            if (st->child_count < 2) return;
            const ASTNode *lhs_list = st->children[0];
            const ASTNode *rhs_list = st->children[1];
            const ASTNode *giving = (st->child_count >= 3 && st->children[2] && st->children[2]->kind == AST_CLAUSE &&
                                     st->children[2]->text && ci_cmp(st->children[2]->text, "GIVING") == 0)
                                    ? st->children[2] : NULL;
            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
            fprintf(ec->out, "    sw   $zero, 0($sp)\n");
            if (lhs_list->kind == AST_LIST) {
                for (size_t i = 0; i < lhs_list->child_count; i++) {
                    emit_eval_num(ec, lhs_list->children[i]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    fprintf(ec->out, "    addu $t0, $t0, $v0\n");
                    fprintf(ec->out, "    sw   $t0, 0($sp)\n");
                }
            } else {
                emit_eval_num(ec, lhs_list);
                fprintf(ec->out, "    sw   $v0, 0($sp)\n");
            }
            if (giving) {
                if (rhs_list->kind == AST_LIST && rhs_list->child_count > 0) {
                    emit_eval_num(ec, rhs_list->children[0]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    fprintf(ec->out, "    subu $t0, $v0, $t0\n");
                    fprintf(ec->out, "    sw   $t0, 0($sp)\n");
                }
                for (size_t i = 0; i < giving->child_count; i++) {
                    fprintf(ec->out, "    lw   $v0, 0($sp)\n");
                    emit_store_num(ec, giving->children[i], true);
                }
            } else if (rhs_list->kind == AST_LIST) {
                for (size_t i = 0; i < rhs_list->child_count; i++) {
                    emit_eval_num(ec, rhs_list->children[i]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    fprintf(ec->out, "    subu $v0, $v0, $t0\n");
                    emit_store_num(ec, rhs_list->children[i], true);
                }
            }
            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            return;
        }

        /* --- MULTIPLY --- */
        case AST_STMT_MULTIPLY: {
            if (st->child_count < 2) return;
            const ASTNode *lhs = st->children[0];
            const ASTNode *rhs_list = st->children[1];
            const ASTNode *giving = (st->child_count >= 3 && st->children[2] && st->children[2]->kind == AST_CLAUSE &&
                                     st->children[2]->text && ci_cmp(st->children[2]->text, "GIVING") == 0)
                                    ? st->children[2] : NULL;
            emit_eval_num(ec, lhs);
            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
            fprintf(ec->out, "    sw   $v0, 0($sp)\n");
            if (giving) {
                if (rhs_list->kind == AST_LIST && rhs_list->child_count > 0) {
                    emit_eval_num(ec, rhs_list->children[0]);
                    fprintf(ec->out, "    lw   $a0, 0($sp)\n");
                    fprintf(ec->out, "    move $a1, $v0\n");
                    fprintf(ec->out, "    li   $a2, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    jal  _rt_mul_scaled\n");
                    fprintf(ec->out, "    sw   $v0, 0($sp)\n");
                }
                for (size_t i = 0; i < giving->child_count; i++) {
                    fprintf(ec->out, "    lw   $v0, 0($sp)\n");
                    emit_store_num(ec, giving->children[i], true);
                }
            } else if (rhs_list->kind == AST_LIST) {
                for (size_t i = 0; i < rhs_list->child_count; i++) {
                    emit_eval_num(ec, rhs_list->children[i]);
                    fprintf(ec->out, "    lw   $a0, 0($sp)\n");
                    fprintf(ec->out, "    move $a1, $v0\n");
                    fprintf(ec->out, "    li   $a2, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    jal  _rt_mul_scaled\n");
                    emit_store_num(ec, rhs_list->children[i], true);
                }
            }
            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            return;
        }

        /* --- DIVIDE --- */
        case AST_STMT_DIVIDE: {
            if (st->child_count < 2) return;
            bool is_by = (st->text && ci_cmp(st->text, "BY") == 0);
            const ASTNode *op1 = st->children[0];
            const ASTNode *rhs_list = st->children[1];
            const ASTNode *giving = NULL, *rem = NULL, *sz_err = NULL;
            for (size_t k = 2; k < st->child_count; k++) {
                const ASTNode *c = st->children[k];
                if (!c || c->kind != AST_CLAUSE || !c->text) continue;
                if (ci_cmp(c->text, "GIVING") == 0) giving = c;
                else if (ci_cmp(c->text, "REMAINDER") == 0) rem = c;
                else if (ci_cmp(c->text, "ON-SIZE-ERROR") == 0) sz_err = c;
            }
            emit_eval_num(ec, op1);
            fprintf(ec->out, "    addiu $sp, $sp, -8\n");
            fprintf(ec->out, "    sw   $v0, 0($sp)\n"); /* op1 */
            if (giving || is_by) {
                if (rhs_list->kind == AST_LIST && rhs_list->child_count > 0) {
                    emit_eval_num(ec, rhs_list->children[0]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    /* BY: op1 / op2 ; INTO: op2 / op1 */
                    if (is_by) {
                        fprintf(ec->out, "    move $a0, $t0\n");
                        fprintf(ec->out, "    move $a1, $v0\n");
                    } else {
                        fprintf(ec->out, "    move $a0, $v0\n");
                        fprintf(ec->out, "    move $a1, $t0\n");
                    }
                    int l_z = new_lbl(ec), l_dn = new_lbl(ec);
                    fprintf(ec->out, "    beq  $a1, $zero, L%d\n", l_z);
                    fprintf(ec->out, "    sw   $a0, 0($sp)\n");
                    fprintf(ec->out, "    sw   $a1, 4($sp)\n");
                    fprintf(ec->out, "    li   $a2, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    jal  _rt_div_scaled\n");
                    if (giving) {
                        for (size_t i = 0; i < giving->child_count; i++) {
                            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
                            fprintf(ec->out, "    sw   $v0, 0($sp)\n");
                            emit_store_num(ec, giving->children[i], true);
                            fprintf(ec->out, "    lw   $v0, 0($sp)\n");
                            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
                        }
                    }
                    if (rem && rem->child_count > 0) {
                        fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                        fprintf(ec->out, "    lw   $t1, 4($sp)\n");
                        fprintf(ec->out, "    div  $t0, $t1\n");
                        fprintf(ec->out, "    mfhi $v0\n");
                        emit_store_num(ec, rem->children[0], true);
                    }
                    fprintf(ec->out, "    j    L%d\n", l_dn);
                    fprintf(ec->out, "L%d:\n", l_z);
                    if (sz_err) {
                        for (size_t i = 0; i < sz_err->child_count; i++) {
                            if (sz_err->children[i]->kind != AST_CLAUSE)
                                emit_stmt(ec, sz_err->children[i]);
                        }
                    }
                    fprintf(ec->out, "L%d:\n", l_dn);
                }
            } else if (rhs_list->kind == AST_LIST) {
                for (size_t i = 0; i < rhs_list->child_count; i++) {
                    emit_eval_num(ec, rhs_list->children[i]);
                    fprintf(ec->out, "    move $a0, $v0\n");
                    fprintf(ec->out, "    lw   $a1, 0($sp)\n");
                    fprintf(ec->out, "    li   $a2, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    jal  _rt_div_scaled\n");
                    emit_store_num(ec, rhs_list->children[i], true);
                }
            }
            fprintf(ec->out, "    addiu $sp, $sp, 8\n");
            return;
        }

        /* --- COMPUTE --- */
        case AST_STMT_COMPUTE: {
            if (st->child_count < 2) return;
            const ASTNode *targets = st->children[0];
            const ASTNode *expr = st->children[1];
            emit_eval_num(ec, expr);
            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
            fprintf(ec->out, "    sw   $v0, 0($sp)\n");
            if (targets->kind == AST_LIST) {
                for (size_t i = 0; i < targets->child_count; i++) {
                    fprintf(ec->out, "    lw   $v0, 0($sp)\n");
                    emit_store_num(ec, targets->children[i], true);
                }
            } else {
                emit_store_num(ec, targets, true);
            }
            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            return;
        }

        /* --- IF --- */
        case AST_STMT_IF: {
            if (st->child_count < 2) return;
            const ASTNode *cond = st->children[0];
            const ASTNode *then_list = st->children[1];
            const ASTNode *else_clause = (st->child_count >= 3) ? st->children[2] : NULL;
            int l_else = new_lbl(ec), l_end = new_lbl(ec);
            CondCtx cc = { NULL, NULL };
            emit_cond(ec, cond, &cc);
            fprintf(ec->out, "    beq  $v0, $zero, L%d\n", else_clause ? l_else : l_end);
            emit_stmt_list(ec, then_list, 0);
            if (else_clause) {
                fprintf(ec->out, "    j    L%d\n", l_end);
                fprintf(ec->out, "L%d:\n", l_else);
                emit_stmt_list(ec, else_clause, 0);
            }
            fprintf(ec->out, "L%d:\n", l_end);
            return;
        }

        /* --- EVALUATE --- */
        case AST_STMT_EVALUATE: {
            if (st->child_count < 2) return;
            const ASTNode *subj_list = st->children[0];
            int l_end = new_lbl(ec);
            for (size_t wi = 1; wi < st->child_count; wi++) {
                const ASTNode *w = st->children[wi];
                if (!w || w->kind != AST_WHEN) continue;
                if (w->text && ci_cmp(w->text, "OTHER") == 0) {
                    emit_stmt_list(ec, w, 0);
                    fprintf(ec->out, "    j    L%d\n", l_end);
                    continue;
                }
                if (w->child_count == 0) continue;
                const ASTNode *objs = w->children[0];
                int l_next = new_lbl(ec);
                size_t nsubj = subj_list->child_count;
                size_t nobj = objs->child_count;
                size_t n = nsubj < nobj ? nsubj : nobj;
                for (size_t k = 0; k < n; k++) {
                    const ASTNode *subj = subj_list->children[k];
                    const ASTNode *obj = objs->children[k];
                    if (obj->kind == AST_FIGURATIVE && obj->text && ci_cmp(obj->text, "ANY") == 0) {
                        continue; /* always matches */
                    }
                    if (subj->kind == AST_FIGURATIVE && subj->text &&
                        (ci_cmp(subj->text, "TRUE") == 0 || ci_cmp(subj->text, "FALSE") == 0)) {
                        bool want = (ci_cmp(subj->text, "TRUE") == 0);
                        CondCtx cc = { NULL, NULL };
                        emit_cond(ec, obj, &cc);
                        if (want) fprintf(ec->out, "    beq  $v0, $zero, L%d\n", l_next);
                        else      fprintf(ec->out, "    bne  $v0, $zero, L%d\n", l_next);
                    } else if (obj->kind == AST_COND_REL && obj->child_count == 1) {
                        emit_compare_vals(ec, subj, obj->children[0]);
                        emit_relop_test(ec, obj->text ? obj->text : "=");
                        fprintf(ec->out, "    beq  $v0, $zero, L%d\n", l_next);
                    } else if (obj->kind == AST_COND_CLASS) {
                        bool neg = (obj->extra && ci_cmp(obj->extra, "NOT") == 0);
                        emit_cond_class(ec, subj, obj->text, neg);
                        fprintf(ec->out, "    beq  $v0, $zero, L%d\n", l_next);
                    } else if (obj->kind == AST_BINOP && obj->text && ci_cmp(obj->text, "THRU") == 0 && obj->child_count >= 2) {
                        emit_compare_vals(ec, subj, obj->children[0]);
                        fprintf(ec->out, "    bltz $v0, L%d\n", l_next);
                        emit_compare_vals(ec, subj, obj->children[1]);
                        fprintf(ec->out, "    bgtz $v0, L%d\n", l_next);
                    } else {
                        emit_compare_vals(ec, subj, obj);
                        fprintf(ec->out, "    bne  $v0, $zero, L%d\n", l_next);
                    }
                }
                emit_stmt_list(ec, w, 1);
                fprintf(ec->out, "    j    L%d\n", l_end);
                fprintf(ec->out, "L%d:\n", l_next);
            }
            fprintf(ec->out, "L%d:\n", l_end);
            return;
        }

        /* --- PERFORM --- */
        case AST_STMT_PERFORM: {
            bool is_inline = (st->text && ci_cmp(st->text, "INLINE") == 0);
            const char *target = is_inline ? NULL : st->text;
            const char *thru = NULL;
            const ASTNode *ctrl = NULL;
            size_t inline_start = 0;

            for (size_t i = 0; i < st->child_count; i++) {
                const ASTNode *c = st->children[i];
                if (c && c->kind == AST_CLAUSE && c->text) {
                    if (ci_cmp(c->text, "THRU") == 0) {
                        thru = c->extra;
                    } else if (ci_cmp(c->text, "TIMES") == 0 || ci_cmp(c->text, "UNTIL") == 0 ||
                               ci_cmp(c->text, "VARYING") == 0) {
                        ctrl = c;
                        if (is_inline) inline_start = i + 1;
                    }
                }
            }

            if (!ctrl) {
                emit_perform_body(ec, st, is_inline, target, thru, inline_start);
            } else if (ci_cmp(ctrl->text, "TIMES") == 0 && ctrl->child_count > 0) {
                emit_eval_num(ec, ctrl->children[0]);
                if (pc->prog_scale > 1) {
                    fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    div  $v0, $t0\n");
                    fprintf(ec->out, "    mflo $v0\n");
                }
                fprintf(ec->out, "    addiu $sp, $sp, -4\n");
                fprintf(ec->out, "    sw   $v0, 0($sp)\n");
                int l_loop = new_lbl(ec), l_end = new_lbl(ec);
                fprintf(ec->out, "L%d:\n", l_loop);
                fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                fprintf(ec->out, "    blez $t0, L%d\n", l_end);
                fprintf(ec->out, "    addiu $t0, $t0, -1\n");
                fprintf(ec->out, "    sw   $t0, 0($sp)\n");
                emit_perform_body(ec, st, is_inline, target, thru, inline_start);
                fprintf(ec->out, "    j    L%d\n", l_loop);
                fprintf(ec->out, "L%d:\n", l_end);
                fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            } else if (ci_cmp(ctrl->text, "UNTIL") == 0 && ctrl->child_count > 0) {
                int l_loop = new_lbl(ec), l_end = new_lbl(ec);
                fprintf(ec->out, "L%d:\n", l_loop);
                CondCtx cc = { NULL, NULL };
                emit_cond(ec, ctrl->children[0], &cc);
                fprintf(ec->out, "    bne  $v0, $zero, L%d\n", l_end);
                emit_perform_body(ec, st, is_inline, target, thru, inline_start);
                fprintf(ec->out, "    j    L%d\n", l_loop);
                fprintf(ec->out, "L%d:\n", l_end);
            } else if (ci_cmp(ctrl->text, "VARYING") == 0) {
                emit_perform_varying_rec(ec, st, ctrl, 0, is_inline, target, thru, inline_start);
            }
            return;
        }

        /* --- OPEN --- */
        case AST_STMT_OPEN: {
            for (size_t g = 0; g < st->child_count; g++) {
                const ASTNode *grp = st->children[g];
                if (!grp || grp->kind != AST_CLAUSE || !grp->text) continue;
                int mode = 0;
                if (ci_cmp(grp->text, "OUTPUT") == 0) mode = 1;
                else if (ci_cmp(grp->text, "I-O") == 0) mode = 2;
                else if (ci_cmp(grp->text, "EXTEND") == 0) mode = 3;

                for (size_t i = 0; i < grp->child_count; i++) {
                    const ASTNode *fref = grp->children[i];
                    if (!fref || !fref->text) continue;
                    int fi = find_file_by_name(pc, fref->text);
                    if (fi < 0) continue;
                    FileLayout *fl = &pc->files[fi];
                    if (fl->assign_var_idx >= 0) {
                        ASTNode vref;
                        memset(&vref, 0, sizeof(vref));
                        vref.kind = AST_DATA_REF;
                        vref.text = pc->vars[fl->assign_var_idx].name;
                        emit_data_ref_addr_len(ec, &vref, NULL);
                        fprintf(ec->out, "    la   $a0, _rt_linebuf\n");
                        fprintf(ec->out, "    move $a1, $v0\n");
                        fprintf(ec->out, "    move $a2, $v1\n");
                        fprintf(ec->out, "    jal  _rt_trim_zstr\n");
                        fprintf(ec->out, "    la   $a0, _rt_linebuf\n");
                    } else {
                        fprintf(ec->out, "    la   $a0, cob_s%d\n", fl->path_str_id);
                    }
                    fprintf(ec->out, "    li   $a1, %d\n", mode);
                    fprintf(ec->out, "    jal  _rt_open_file\n");
                    fprintf(ec->out, "    la   $t0, p%d_fd%d\n", pc->prog_id, fi);
                    fprintf(ec->out, "    sw   $v0, 0($t0)\n");

                    int l_err = new_lbl(ec), l_dn = new_lbl(ec);
                    fprintf(ec->out, "    bltz $v0, L%d\n", l_err);
                    emit_set_file_status(ec, fi, "00");
                    fprintf(ec->out, "    j    L%d\n", l_dn);
                    fprintf(ec->out, "L%d:\n", l_err);
                    emit_set_file_status(ec, fi, "35");
                    fprintf(ec->out, "L%d:\n", l_dn);
                }
            }
            return;
        }

        /* --- CLOSE --- */
        case AST_STMT_CLOSE: {
            for (size_t i = 0; i < st->child_count; i++) {
                const ASTNode *fref = st->children[i];
                if (!fref || !fref->text) continue;
                int fi = find_file_by_name(pc, fref->text);
                if (fi < 0) continue;
                fprintf(ec->out, "    la   $t0, p%d_fd%d\n", pc->prog_id, fi);
                fprintf(ec->out, "    lw   $a0, 0($t0)\n");
                fprintf(ec->out, "    jal  _rt_close_file\n");
                fprintf(ec->out, "    la   $t0, p%d_fd%d\n", pc->prog_id, fi);
                fprintf(ec->out, "    li   $t1, -1\n");
                fprintf(ec->out, "    sw   $t1, 0($t0)\n");
                emit_set_file_status(ec, fi, "00");
            }
            return;
        }

        /* --- READ --- */
        case AST_STMT_READ: {
            int fi = find_file_by_name(pc, st->text);
            if (fi < 0) return;
            const ASTNode *into_cl = NULL, *at_end = NULL, *not_at_end = NULL;
            for (size_t i = 1; i < st->child_count; i++) {
                const ASTNode *c = st->children[i];
                if (!c || c->kind != AST_CLAUSE || !c->text) continue;
                if (ci_cmp(c->text, "INTO") == 0) into_cl = c;
                else if (ci_cmp(c->text, "AT-END") == 0 || ci_cmp(c->text, "INVALID-KEY") == 0) at_end = c;
                else if (ci_cmp(c->text, "NOT-AT-END") == 0 || ci_cmp(c->text, "NOT-INVALID-KEY") == 0) not_at_end = c;
            }
            if (at_end) {
                for (size_t k = 0; k < at_end->child_count; k++) {
                    const ASTNode *c = at_end->children[k];
                    if (c->kind == AST_CLAUSE && c->text &&
                        (ci_cmp(c->text, "NOT-AT-END") == 0 || ci_cmp(c->text, "NOT-INVALID-KEY") == 0)) {
                        not_at_end = c;
                    }
                }
            }

            int rvi = pc->files[fi].rec_var_idx;
            fprintf(ec->out, "    la   $t0, p%d_fd%d\n", pc->prog_id, fi);
            fprintf(ec->out, "    lw   $a0, 0($t0)\n");
            if (rvi >= 0) {
                fprintf(ec->out, "    la   $a1, p%d_d%d\n", pc->prog_id, rvi);
                fprintf(ec->out, "    li   $a2, %d\n", pc->vars[rvi].total_size);
            } else {
                fprintf(ec->out, "    la   $a1, _rt_linebuf\n");
                fprintf(ec->out, "    li   $a2, 256\n");
            }
            fprintf(ec->out, "    jal  _rt_read_line_fd\n");

            int l_eof = new_lbl(ec), l_dn = new_lbl(ec);
            fprintf(ec->out, "    beq  $v0, $zero, L%d\n", l_eof);
            emit_set_file_status(ec, fi, "00");
            if (into_cl && into_cl->child_count > 0 && rvi >= 0) {
                ASTNode rref;
                memset(&rref, 0, sizeof(rref));
                rref.kind = AST_DATA_REF;
                rref.text = pc->vars[rvi].name;
                emit_move_one(ec, &rref, into_cl->children[0]);
            }
            if (not_at_end) {
                for (size_t k = 0; k < not_at_end->child_count; k++) {
                    if (not_at_end->children[k]->kind != AST_CLAUSE)
                        emit_stmt(ec, not_at_end->children[k]);
                }
            }
            fprintf(ec->out, "    j    L%d\n", l_dn);
            fprintf(ec->out, "L%d:\n", l_eof);
            emit_set_file_status(ec, fi, "10");
            if (at_end) {
                for (size_t k = 0; k < at_end->child_count; k++) {
                    if (at_end->children[k]->kind != AST_CLAUSE)
                        emit_stmt(ec, at_end->children[k]);
                }
            }
            fprintf(ec->out, "L%d:\n", l_dn);
            return;
        }

        /* --- WRITE / REWRITE --- */
        case AST_STMT_WRITE:
        case AST_STMT_REWRITE: {
            if (st->child_count == 0) return;
            const ASTNode *rref = st->children[0];
            int rvi = resolve_data_ref(pc, rref);
            if (rvi < 0) return;
            int fi = pc->vars[rvi].fd_idx;
            const ASTNode *from_cl = NULL;
            for (size_t i = 1; i < st->child_count; i++) {
                const ASTNode *c = st->children[i];
                if (c && c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "FROM") == 0) {
                    from_cl = c;
                }
            }
            if (from_cl && from_cl->child_count > 0) {
                emit_move_one(ec, from_cl->children[0], rref);
            }
            if (fi >= 0) {
                emit_data_ref_addr_len(ec, rref, NULL);
                fprintf(ec->out, "    la   $t0, p%d_fd%d\n", pc->prog_id, fi);
                fprintf(ec->out, "    lw   $a0, 0($t0)\n");
                fprintf(ec->out, "    move $a1, $v0\n");
                fprintf(ec->out, "    move $a2, $v1\n");
                fprintf(ec->out, "    jal  _rt_write_line_fd\n");
                emit_set_file_status(ec, fi, "00");
            }
            return;
        }

        /* --- GO TO --- */
        case AST_STMT_GOTO: {
            if (st->extra) return; /* ALTER ignored safely */
            int bi = find_block_by_name(pc, st->text);
            if (bi >= 0) {
                fprintf(ec->out, "    j    p%d_b%d\n", pc->prog_id, bi);
            }
            return;
        }

        /* --- CALL --- */
        case AST_STMT_CALL: {
            if (st->child_count == 0) return;
            const ASTNode *tgt = st->children[0];
            if (!tgt || tgt->kind != AST_LITERAL_STR || !tgt->text) return;
            char subname[128];
            unquote_cobol_str(tgt->text, subname, sizeof(subname));

            int sub_idx = -1;
            for (int i = 0; i < ec->prog_count; i++) {
                if (ci_cmp(ec->progs[i]->prog_name, subname) == 0) { sub_idx = i; break; }
            }
            if (sub_idx < 0) {
                fprintf(ec->out, "    # CALL '%s' (external subprogram not found, skipped)\n", subname);
                return;
            }

            const ASTNode *using_cl = NULL;
            for (size_t i = 1; i < st->child_count; i++) {
                if (st->children[i]->kind == AST_CLAUSE && st->children[i]->text &&
                    ci_cmp(st->children[i]->text, "USING") == 0) {
                    using_cl = st->children[i];
                }
            }

            int nargs = using_cl ? (int)using_cl->child_count : 0;
            if (nargs > 4) nargs = 4;
            if (nargs > 0) {
                fprintf(ec->out, "    addiu $sp, $sp, %d\n", -(nargs * 4));
                for (int a = 0; a < nargs; a++) {
                    emit_addr_len(ec, using_cl->children[a], NULL);
                    fprintf(ec->out, "    sw   $v0, %d($sp)\n", a * 4);
                }
                for (int a = 0; a < nargs; a++) {
                    fprintf(ec->out, "    lw   $a%d, %d($sp)\n", a, a * 4);
                }
                fprintf(ec->out, "    addiu $sp, $sp, %d\n", nargs * 4);
            }
            fprintf(ec->out, "    jal  cob_sub_%s\n", ec->progs[sub_idx]->san_name);
            return;
        }

        /* --- STOP RUN / GOBACK --- */
        case AST_STMT_STOP_RUN: {
            fprintf(ec->out, "    li   $a0, 0\n");
            fprintf(ec->out, "    jal  _rt_exit\n");
            return;
        }
        case AST_STMT_GOBACK: {
            fprintf(ec->out, "    j    p%d_exit\n", pc->prog_id);
            return;
        }

        /* --- EXIT / CONTINUE / NEXT SENTENCE / CANCEL / DELETE / START / SORT / MERGE / RELEASE / RETURN --- */
        case AST_STMT_EXIT:
        case AST_STMT_CONTINUE:
        case AST_STMT_NEXT_SENTENCE:
        case AST_STMT_CANCEL:
        case AST_STMT_DELETE:
        case AST_STMT_START:
        case AST_STMT_SORT:
        case AST_STMT_MERGE:
        case AST_STMT_RELEASE:
        case AST_STMT_RETURN:
            return;

        /* --- INITIALIZE --- */
        case AST_STMT_INITIALIZE: {
            const ASTNode *repl_num = NULL, *repl_alnum = NULL;
            for (size_t i = 0; i < st->child_count; i++) {
                const ASTNode *c = st->children[i];
                if (c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "REPLACING") == 0 && c->child_count > 0) {
                    if (c->extra && ci_cmp(c->extra, "NUMERIC") == 0) repl_num = c->children[0];
                    else repl_alnum = c->children[0];
                }
            }
            for (size_t i = 0; i < st->child_count; i++) {
                const ASTNode *t = st->children[i];
                if (t->kind != AST_DATA_REF) continue;
                int vi = resolve_data_ref(pc, t);
                if (vi < 0) continue;
                if (pc->vars[vi].kind == DATA_GROUP) {
                    /* Initialize every elementary descendant */
                    for (size_t j = (size_t)vi + 1; j < pc->var_count; j++) {
                        int p = pc->vars[j].parent_idx;
                        bool is_desc = false;
                        while (p >= 0) {
                            if (p == vi) { is_desc = true; break; }
                            p = pc->vars[p].parent_idx;
                        }
                        if (!is_desc) break;
                        if (pc->vars[j].kind == DATA_GROUP || pc->vars[j].kind == DATA_COND || pc->vars[j].is_redefines)
                            continue;
                        if (pc->vars[j].occurs_depth > pc->vars[vi].occurs_depth) continue;
                        ASTNode cref;
                        memset(&cref, 0, sizeof(cref));
                        cref.kind = AST_DATA_REF;
                        cref.text = pc->vars[j].name;
                        if (pc->vars[j].kind == DATA_NUMERIC || pc->vars[j].kind == DATA_NUMERIC_EDITED) {
                            if (repl_num) {
                                emit_move_one(ec, repl_num, &cref);
                            } else {
                                fprintf(ec->out, "    li   $v0, 0\n");
                                emit_store_num(ec, &cref, true);
                            }
                        } else {
                            if (repl_alnum) {
                                emit_move_one(ec, repl_alnum, &cref);
                            } else {
                                emit_data_ref_addr_len(ec, &cref, NULL);
                                fprintf(ec->out, "    move $a0, $v0\n");
                                fprintf(ec->out, "    li   $a1, 32\n");
                                fprintf(ec->out, "    move $a2, $v1\n");
                                fprintf(ec->out, "    jal  _rt_memset\n");
                            }
                        }
                    }
                } else if (pc->vars[vi].kind == DATA_NUMERIC || pc->vars[vi].kind == DATA_NUMERIC_EDITED) {
                    if (repl_num) emit_move_one(ec, repl_num, t);
                    else { fprintf(ec->out, "    li   $v0, 0\n"); emit_store_num(ec, t, true); }
                } else {
                    if (repl_alnum) emit_move_one(ec, repl_alnum, t);
                    else {
                        emit_data_ref_addr_len(ec, t, NULL);
                        fprintf(ec->out, "    move $a0, $v0\n");
                        fprintf(ec->out, "    li   $a1, 32\n");
                        fprintf(ec->out, "    move $a2, $v1\n");
                        fprintf(ec->out, "    jal  _rt_memset\n");
                    }
                }
            }
            return;
        }

        /* --- INSPECT --- */
        case AST_STMT_INSPECT: {
            if (st->child_count < 2) return;
            const ASTNode *subj = st->children[0];
            emit_addr_len(ec, subj, NULL);
            fprintf(ec->out, "    addiu $sp, $sp, -8\n");
            fprintf(ec->out, "    sw   $v0, 0($sp)\n"); /* str */
            fprintf(ec->out, "    sw   $v1, 4($sp)\n"); /* slen */

            for (size_t ci = 1; ci < st->child_count; ci++) {
                const ASTNode *cl = st->children[ci];
                if (!cl || cl->kind != AST_CLAUSE || !cl->text) continue;

                if (ci_cmp(cl->text, "TALLYING") == 0) {
                    for (size_t ti = 0; ti < cl->child_count; ti++) {
                        const ASTNode *titem = cl->children[ti];
                        if (!titem || titem->child_count < 2) continue;
                        const ASTNode *cnt_ref = titem->children[0];
                        for (size_t fi = 1; fi < titem->child_count; fi++) {
                            const ASTNode *f = titem->children[fi];
                            int mode = 0;
                            if (f->text && ci_cmp(f->text, "FOR-ALL") == 0) mode = 1;
                            else if (f->text && ci_cmp(f->text, "FOR-LEADING") == 0) mode = 2;

                            if (mode == 0) {
                                fprintf(ec->out, "    lw   $a0, 0($sp)\n");
                                fprintf(ec->out, "    lw   $a1, 4($sp)\n");
                                fprintf(ec->out, "    move $a2, $zero\n");
                                fprintf(ec->out, "    li   $a3, 0\n");
                                fprintf(ec->out, "    jal  _rt_inspect_tally\n");
                            } else if (f->child_count > 0) {
                                emit_addr_len(ec, f->children[0], NULL);
                                fprintf(ec->out, "    lw   $a0, 0($sp)\n");
                                fprintf(ec->out, "    lw   $a1, 4($sp)\n");
                                fprintf(ec->out, "    move $a2, $v0\n");
                                fprintf(ec->out, "    li   $t0, %d\n", mode << 16);
                                fprintf(ec->out, "    or   $a3, $t0, $v1\n");
                                fprintf(ec->out, "    jal  _rt_inspect_tally\n");
                            }
                            /* Add count ($v0 * prog_scale) to cnt_ref */
                            if (pc->prog_scale > 1) {
                                fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                                fprintf(ec->out, "    mult $v0, $t0\n");
                                fprintf(ec->out, "    mflo $v0\n");
                            }
                            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
                            fprintf(ec->out, "    sw   $v0, 0($sp)\n");
                            emit_eval_num(ec, cnt_ref);
                            fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
                            fprintf(ec->out, "    addu $v0, $v0, $t0\n");
                            emit_store_num(ec, cnt_ref, true);
                        }
                    }
                } else if (ci_cmp(cl->text, "REPLACING") == 0) {
                    for (size_t ri = 0; ri < cl->child_count; ri++) {
                        const ASTNode *r = cl->children[ri];
                        if (!r || !r->text) continue;
                        int mode = 0;
                        if (ci_cmp(r->text, "REPL-ALL") == 0) mode = 1;
                        else if (ci_cmp(r->text, "REPL-LEADING") == 0) mode = 2;
                        else if (ci_cmp(r->text, "REPL-FIRST") == 0) mode = 3;

                        if (mode == 0 && r->child_count >= 1) {
                            emit_addr_len(ec, r->children[0], NULL);
                            fprintf(ec->out, "    addiu $sp, $sp, -4\n");
                            fprintf(ec->out, "    sw   $zero, 0($sp)\n");
                            fprintf(ec->out, "    lw   $a0, 4($sp)\n");
                            fprintf(ec->out, "    lw   $a1, 8($sp)\n");
                            fprintf(ec->out, "    move $a2, $zero\n");
                            fprintf(ec->out, "    move $a3, $v0\n");
                            fprintf(ec->out, "    jal  _rt_inspect_replace\n");
                            fprintf(ec->out, "    addiu $sp, $sp, 4\n");
                        } else if (r->child_count >= 2) {
                            emit_addr_len(ec, r->children[0], NULL);
                            fprintf(ec->out, "    addiu $sp, $sp, -12\n");
                            fprintf(ec->out, "    sw   $v0, 4($sp)\n"); /* pat */
                            fprintf(ec->out, "    li   $t0, %d\n", mode << 16);
                            fprintf(ec->out, "    or   $t0, $t0, $v1\n");
                            fprintf(ec->out, "    sw   $t0, 0($sp)\n"); /* (mode<<16)|plen */
                            emit_addr_len(ec, r->children[1], NULL);
                            fprintf(ec->out, "    lw   $a0, 12($sp)\n");
                            fprintf(ec->out, "    lw   $a1, 16($sp)\n");
                            fprintf(ec->out, "    lw   $a2, 4($sp)\n");
                            fprintf(ec->out, "    move $a3, $v0\n");
                            fprintf(ec->out, "    jal  _rt_inspect_replace\n");
                            fprintf(ec->out, "    addiu $sp, $sp, 12\n");
                        }
                    }
                } else if (ci_cmp(cl->text, "CONVERTING") == 0 && cl->child_count >= 2) {
                    emit_addr_len(ec, cl->children[0], NULL);
                    fprintf(ec->out, "    addiu $sp, $sp, -8\n");
                    fprintf(ec->out, "    sw   $v1, 0($sp)\n"); /* flen */
                    fprintf(ec->out, "    sw   $v0, 4($sp)\n"); /* from */
                    emit_addr_len(ec, cl->children[1], NULL);
                    fprintf(ec->out, "    lw   $a0, 8($sp)\n");
                    fprintf(ec->out, "    lw   $a1, 12($sp)\n");
                    fprintf(ec->out, "    lw   $a2, 4($sp)\n");
                    fprintf(ec->out, "    move $a3, $v0\n");
                    fprintf(ec->out, "    jal  _rt_inspect_convert\n");
                    fprintf(ec->out, "    addiu $sp, $sp, 8\n");
                }
            }
            fprintf(ec->out, "    addiu $sp, $sp, 8\n");
            return;
        }

        /* --- STRING --- */
        case AST_STMT_STRING: {
            if (st->child_count < 2) return;
            const ASTNode *parts = st->children[0];
            const ASTNode *dst = st->children[1];
            const ASTNode *ptr_cl = NULL;
            for (size_t i = 2; i < st->child_count; i++) {
                if (st->children[i] && st->children[i]->kind == AST_CLAUSE &&
                    st->children[i]->text && ci_cmp(st->children[i]->text, "POINTER") == 0) {
                    ptr_cl = st->children[i];
                }
            }
            emit_data_ref_addr_len(ec, dst, NULL);
            fprintf(ec->out, "    addiu $sp, $sp, -12\n");
            fprintf(ec->out, "    sw   $v0, 0($sp)\n"); /* dst */
            fprintf(ec->out, "    sw   $v1, 4($sp)\n"); /* dlen */
            if (ptr_cl && ptr_cl->child_count > 0) {
                emit_eval_num(ec, ptr_cl->children[0]);
                if (pc->prog_scale > 1) {
                    fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    div  $v0, $t0\n");
                    fprintf(ec->out, "    mflo $v0\n");
                }
                fprintf(ec->out, "    addiu $v0, $v0, -1\n");
                fprintf(ec->out, "    sw   $v0, 8($sp)\n");
            } else {
                fprintf(ec->out, "    sw   $zero, 8($sp)\n"); /* cur_pos = 0 */
            }
            for (size_t pi = 0; pi < parts->child_count; pi++) {
                const ASTNode *p = parts->children[pi];
                if (!p || p->child_count < 2) continue;
                const ASTNode *rv_list = p->children[0];
                const ASTNode *delim = p->children[1];
                bool by_size = (delim->kind == AST_CLAUSE && delim->text && ci_cmp(delim->text, "DELIM-SIZE") == 0);
                int delim_ch = -1;
                if (!by_size) {
                    if (delim->kind == AST_FIGURATIVE && delim->text &&
                        (ci_cmp(delim->text, "SPACE") == 0 || ci_cmp(delim->text, "SPACES") == 0))
                        delim_ch = ' ';
                    else if (delim->kind == AST_LITERAL_STR && delim->text) {
                        char db[16];
                        if (unquote_cobol_str(delim->text, db, sizeof(db)) > 0) delim_ch = (unsigned char)db[0];
                    }
                }
                for (size_t ri = 0; ri < rv_list->child_count; ri++) {
                    emit_addr_len(ec, rv_list->children[ri], NULL);
                    int lp = new_lbl(ec), ldone = new_lbl(ec);
                    fprintf(ec->out, "    li   $t0, 0\n");
                    fprintf(ec->out, "    lw   $t1, 0($sp)\n"); /* dst */
                    fprintf(ec->out, "    lw   $t2, 4($sp)\n"); /* dlen */
                    fprintf(ec->out, "    lw   $t3, 8($sp)\n"); /* pos */
                    fprintf(ec->out, "L%d:\n", lp);
                    fprintf(ec->out, "    bge  $t0, $v1, L%d\n", ldone);
                    fprintf(ec->out, "    bge  $t3, $t2, L%d\n", ldone);
                    fprintf(ec->out, "    addu $t4, $v0, $t0\n");
                    fprintf(ec->out, "    lbu  $t5, 0($t4)\n");
                    if (delim_ch >= 0) {
                        fprintf(ec->out, "    li   $t6, %d\n", delim_ch);
                        fprintf(ec->out, "    beq  $t5, $t6, L%d\n", ldone);
                    }
                    fprintf(ec->out, "    addu $t4, $t1, $t3\n");
                    fprintf(ec->out, "    sb   $t5, 0($t4)\n");
                    fprintf(ec->out, "    addiu $t0, $t0, 1\n");
                    fprintf(ec->out, "    addiu $t3, $t3, 1\n");
                    fprintf(ec->out, "    j    L%d\n", lp);
                    fprintf(ec->out, "L%d:\n", ldone);
                    fprintf(ec->out, "    sw   $t3, 8($sp)\n");
                }
            }
            if (ptr_cl && ptr_cl->child_count > 0) {
                fprintf(ec->out, "    lw   $v0, 8($sp)\n");
                fprintf(ec->out, "    addiu $v0, $v0, 1\n");
                if (pc->prog_scale > 1) {
                    fprintf(ec->out, "    li   $t0, %d\n", pc->prog_scale);
                    fprintf(ec->out, "    mult $v0, $t0\n");
                    fprintf(ec->out, "    mflo $v0\n");
                }
                emit_store_num(ec, ptr_cl->children[0], true);
            }
            fprintf(ec->out, "    addiu $sp, $sp, 12\n");
            return;
        }

        /* --- UNSTRING --- */
        case AST_STMT_UNSTRING: {
            if (st->child_count < 2) return;
            const ASTNode *src = st->children[0];
            const ASTNode *delim_cl = NULL, *targets = NULL;
            for (size_t i = 1; i < st->child_count; i++) {
                const ASTNode *c = st->children[i];
                if (!c) continue;
                if (c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "UNSTRING-DELIM") == 0) delim_cl = c;
                else if (c->kind == AST_LIST) targets = c;
            }
            if (!targets) return;
            int dch = ' ';
            if (delim_cl && delim_cl->child_count > 0) {
                const ASTNode *d = delim_cl->children[0];
                if (d->kind == AST_LITERAL_STR && d->text) {
                    char db[16];
                    if (unquote_cobol_str(d->text, db, sizeof(db)) > 0) dch = (unsigned char)db[0];
                }
            }
            emit_addr_len(ec, src, NULL);
            fprintf(ec->out, "    addiu $sp, $sp, -12\n");
            fprintf(ec->out, "    sw   $v0, 0($sp)\n"); /* src */
            fprintf(ec->out, "    sw   $v1, 4($sp)\n"); /* slen */
            fprintf(ec->out, "    sw   $zero, 8($sp)\n"); /* pos = 0 */

            for (size_t ti = 0; ti < targets->child_count; ti++) {
                emit_data_ref_addr_len(ec, targets->children[ti], NULL);
                /* Space-pad target first */
                fprintf(ec->out, "    addiu $sp, $sp, -8\n");
                fprintf(ec->out, "    sw   $v0, 0($sp)\n");
                fprintf(ec->out, "    sw   $v1, 4($sp)\n");
                fprintf(ec->out, "    move $a0, $v0\n");
                fprintf(ec->out, "    li   $a1, 32\n");
                fprintf(ec->out, "    move $a2, $v1\n");
                fprintf(ec->out, "    jal  _rt_memset\n");
                fprintf(ec->out, "    lw   $t0, 0($sp)\n"); /* dst */
                fprintf(ec->out, "    lw   $t1, 4($sp)\n"); /* dlen */
                fprintf(ec->out, "    addiu $sp, $sp, 8\n");
                fprintf(ec->out, "    lw   $t2, 0($sp)\n"); /* src */
                fprintf(ec->out, "    lw   $t3, 4($sp)\n"); /* slen */
                fprintf(ec->out, "    lw   $t4, 8($sp)\n"); /* pos */
                fprintf(ec->out, "    li   $t5, 0\n");      /* j */
                int lp = new_lbl(ec), lhit = new_lbl(ec), ldn = new_lbl(ec);
                fprintf(ec->out, "L%d:\n", lp);
                fprintf(ec->out, "    bge  $t4, $t3, L%d\n", ldn);
                fprintf(ec->out, "    addu $t6, $t2, $t4\n");
                fprintf(ec->out, "    lbu  $t7, 0($t6)\n");
                fprintf(ec->out, "    li   $v0, %d\n", dch);
                fprintf(ec->out, "    beq  $t7, $v0, L%d\n", lhit);
                fprintf(ec->out, "    bge  $t5, $t1, L%d\n", ldn);
                fprintf(ec->out, "    addu $t6, $t0, $t5\n");
                fprintf(ec->out, "    sb   $t7, 0($t6)\n");
                fprintf(ec->out, "    addiu $t4, $t4, 1\n");
                fprintf(ec->out, "    addiu $t5, $t5, 1\n");
                fprintf(ec->out, "    j    L%d\n", lp);
                fprintf(ec->out, "L%d:\n", lhit);
                fprintf(ec->out, "    addiu $t4, $t4, 1\n");
                fprintf(ec->out, "L%d:\n", ldn);
                fprintf(ec->out, "    sw   $t4, 8($sp)\n");
            }
            fprintf(ec->out, "    addiu $sp, $sp, 12\n");
            return;
        }

        /* --- SET --- */
        case AST_STMT_SET: {
            if (!st->text || st->child_count == 0) return;
            if (ci_cmp(st->text, "TO-TRUE") == 0) {
                for (size_t i = 0; i < st->child_count; i++) {
                    int vi = resolve_data_ref(pc, st->children[i]);
                    if (vi >= 0 && pc->vars[vi].kind == DATA_COND && pc->vars[vi].parent_idx >= 0 &&
                        pc->vars[vi].val_ast && pc->vars[vi].val_ast->child_count > 0) {
                        ASTNode pref;
                        memset(&pref, 0, sizeof(pref));
                        pref.kind = AST_DATA_REF;
                        pref.text = pc->vars[pc->vars[vi].parent_idx].name;
                        const ASTNode *val = pc->vars[vi].val_ast->children[0];
                        if (val->kind == AST_BINOP && val->child_count > 0) val = val->children[0];
                        emit_move_one(ec, val, &pref);
                    }
                }
            } else if (ci_cmp(st->text, "TO") == 0 && st->child_count >= 2) {
                const ASTNode *rv = st->children[st->child_count - 1];
                for (size_t i = 0; i + 1 < st->child_count; i++) {
                    emit_move_one(ec, rv, st->children[i]);
                }
            } else if ((ci_cmp(st->text, "UP") == 0 || ci_cmp(st->text, "DOWN") == 0) && st->child_count >= 2) {
                bool is_up = (ci_cmp(st->text, "UP") == 0);
                const ASTNode *rv = st->children[st->child_count - 1];
                emit_eval_num(ec, rv);
                fprintf(ec->out, "    addiu $sp, $sp, -4\n");
                fprintf(ec->out, "    sw   $v0, 0($sp)\n");
                for (size_t i = 0; i + 1 < st->child_count; i++) {
                    emit_eval_num(ec, st->children[i]);
                    fprintf(ec->out, "    lw   $t0, 0($sp)\n");
                    if (is_up) fprintf(ec->out, "    addu $v0, $v0, $t0\n");
                    else       fprintf(ec->out, "    subu $v0, $v0, $t0\n");
                    emit_store_num(ec, st->children[i], true);
                }
                fprintf(ec->out, "    addiu $sp, $sp, 4\n");
            }
            return;
        }

        /* --- SEARCH --- */
        case AST_STMT_SEARCH: {
            if (st->child_count < 2) return;
            const ASTNode *tbl_ref = st->children[0];
            int tvi = resolve_data_ref(pc, tbl_ref);
            int max_occ = (tvi >= 0 && pc->vars[tvi].occurs > 0) ? pc->vars[tvi].occurs : 10;

            const ASTNode *idx_ref = NULL, *at_end = NULL;
            for (size_t i = 1; i < st->child_count; i++) {
                const ASTNode *c = st->children[i];
                if (!c) continue;
                if (c->kind == AST_DATA_REF) idx_ref = c;
                else if (c->kind == AST_CLAUSE && c->text && ci_cmp(c->text, "AT-END") == 0) at_end = c;
            }
            /* If no VARYING index, find first DATA_INDEX var */
            ASTNode fallback_ix;
            if (!idx_ref) {
                for (size_t v = 0; v < pc->var_count; v++) {
                    if (pc->vars[v].kind == DATA_INDEX) {
                        memset(&fallback_ix, 0, sizeof(fallback_ix));
                        fallback_ix.kind = AST_DATA_REF;
                        fallback_ix.text = pc->vars[v].name;
                        idx_ref = &fallback_ix;
                        break;
                    }
                }
            }
            int l_loop = new_lbl(ec), l_eof = new_lbl(ec), l_end = new_lbl(ec);
            fprintf(ec->out, "L%d:\n", l_loop);
            if (idx_ref) {
                emit_eval_num(ec, idx_ref);
                fprintf(ec->out, "    li   $t0, %d\n", max_occ * pc->prog_scale);
                fprintf(ec->out, "    bgt  $v0, $t0, L%d\n", l_eof);
            }
            for (size_t i = 1; i < st->child_count; i++) {
                const ASTNode *w = st->children[i];
                if (!w || w->kind != AST_WHEN || w->child_count == 0) continue;
                int l_nw = new_lbl(ec);
                CondCtx cc = { NULL, NULL };
                emit_cond(ec, w->children[0], &cc);
                fprintf(ec->out, "    beq  $v0, $zero, L%d\n", l_nw);
                emit_stmt_list(ec, w, 1);
                fprintf(ec->out, "    j    L%d\n", l_end);
                fprintf(ec->out, "L%d:\n", l_nw);
            }
            if (idx_ref) {
                emit_eval_num(ec, idx_ref);
                fprintf(ec->out, "    addiu $v0, $v0, %d\n", pc->prog_scale);
                emit_store_num(ec, idx_ref, true);
                fprintf(ec->out, "    j    L%d\n", l_loop);
            }
            fprintf(ec->out, "L%d:\n", l_eof);
            if (at_end) emit_stmt_list(ec, at_end, 0);
            fprintf(ec->out, "L%d:\n", l_end);
            return;
        }

        default:
            return;
    }
}

/* =========================================================================
 * Module Emission (.data and .text)
 * ========================================================================= */

static void emit_prog_text(EmitCtx *ec, ProgCtx *pc) {
    ec->cur = pc;
    fprintf(ec->out, "\n# -------------------------------------------------------------------------\n");
    fprintf(ec->out, "# Program %d: %s (prog_frac=%d, prog_scale=%d, decimal_comma=%d)\n",
            pc->prog_id, pc->prog_name, pc->prog_frac, pc->prog_scale, pc->decimal_comma ? 1 : 0);
    fprintf(ec->out, "# -------------------------------------------------------------------------\n");

    if (pc->prog_id == 0) {
        if (ec->target == MIPS_TARGET_MARS) {
            fprintf(ec->out, "    .globl main\nmain:\n");
        } else {
            fprintf(ec->out, "    .globl __start\n__start:\n");
        }
        /* Start at block 0 */
        fprintf(ec->out, "    j    p0_b0\n");
    } else {
        fprintf(ec->out, "cob_sub_%s:\n", pc->san_name);
        fprintf(ec->out, "    la   $t0, p%d_ret_ra\n", pc->prog_id);
        fprintf(ec->out, "    sw   $ra, 0($t0)\n");
        fprintf(ec->out, "    la   $t0, p%d_ret_sp\n", pc->prog_id);
        fprintf(ec->out, "    sw   $sp, 0($t0)\n");
        fprintf(ec->out, "    la   $t0, _rt_perf_top\n");
        fprintf(ec->out, "    lw   $t1, 0($t0)\n");
        fprintf(ec->out, "    la   $t0, p%d_ret_pt\n", pc->prog_id);
        fprintf(ec->out, "    sw   $t1, 0($t0)\n");
        for (int u = 0; u < pc->using_count && u < 4; u++) {
            fprintf(ec->out, "    la   $t0, p%d_lp%d\n", pc->prog_id, pc->using_vars[u]);
            fprintf(ec->out, "    sw   $a%d, 0($t0)\n", u);
        }
        fprintf(ec->out, "    j    p%d_b0\n", pc->prog_id);
    }

    for (size_t bi = 0; bi < pc->block_count; bi++) {
        ec->cur_block_idx = (int)bi;
        ProcBlock *b = &pc->blocks[bi];
        fprintf(ec->out, "\np%d_b%zu: # %s %s\n",
                pc->prog_id, bi, b->is_section ? "SECTION" : "PARA", b->name);
        for (size_t si = 0; si < b->stmt_count; si++) {
            emit_stmt(ec, b->stmts[si]);
        }
        /* PERFORM return check at end of block */
        fprintf(ec->out, "    li   $a0, %d\n", b->global_id);
        fprintf(ec->out, "    jal  _rt_perf_chk\n");
    }

    /* Program epilogue */
    fprintf(ec->out, "\np%d_exit:\n", pc->prog_id);
    if (pc->prog_id == 0) {
        fprintf(ec->out, "    li   $a0, 0\n");
        fprintf(ec->out, "    jal  _rt_exit\n");
    } else {
        fprintf(ec->out, "    la   $t0, p%d_ret_pt\n", pc->prog_id);
        fprintf(ec->out, "    lw   $t1, 0($t0)\n");
        fprintf(ec->out, "    la   $t0, _rt_perf_top\n");
        fprintf(ec->out, "    sw   $t1, 0($t0)\n");
        fprintf(ec->out, "    la   $t0, p%d_ret_sp\n", pc->prog_id);
        fprintf(ec->out, "    lw   $sp, 0($t0)\n");
        fprintf(ec->out, "    la   $t0, p%d_ret_ra\n", pc->prog_id);
        fprintf(ec->out, "    lw   $ra, 0($t0)\n");
        fprintf(ec->out, "    jr   $ra\n");
    }
}

static void emit_all_data(EmitCtx *ec) {
    fprintf(ec->out, "\n    .data\n");

    for (int pi = 0; pi < ec->prog_count; pi++) {
        ProgCtx *pc = ec->progs[pi];
        fprintf(ec->out, "\n# --- Data for Program %d (%s) ---\n", pc->prog_id, pc->prog_name);

        /* Subprogram return state words */
        if (pc->prog_id > 0) {
            fprintf(ec->out, "    .align 2\n");
            fprintf(ec->out, "p%d_ret_ra: .word 0\n", pc->prog_id);
            fprintf(ec->out, "p%d_ret_sp: .word 0\n", pc->prog_id);
            fprintf(ec->out, "p%d_ret_pt: .word 0\n", pc->prog_id);
        }

        /* File descriptors */
        if (pc->file_count > 0) {
            fprintf(ec->out, "    .align 2\n");
            for (size_t fi = 0; fi < pc->file_count; fi++) {
                fprintf(ec->out, "p%d_fd%zu: .word -1 # %s\n", pc->prog_id, fi, pc->files[fi].name);
            }
        }

        /* Root data buffers (01 / 77 / INDEXED BY) */
        for (size_t vi = 0; vi < pc->var_count; vi++) {
            const VarLayout *v = &pc->vars[vi];
            if (v->kind == DATA_COND || v->parent_idx >= 0 || v->is_redefines || v->is_global_retcode)
                continue;
            size_t sz = v->total_size > 0 ? (size_t)v->total_size : 1;
            unsigned char *buf = malloc(sz);
            memset(buf, ' ', sz);
            init_var_tree(pc, (int)vi, buf, sz, 0);

            fprintf(ec->out, "p%d_d%zu: # %s (%zu bytes)\n", pc->prog_id, vi, v->name, sz);
            emit_bytes_data(ec->out, buf, sz);
            free(buf);

            if (v->section_kind == 3) {
                /* LINKAGE pointer word initialized to default standalone buffer */
                fprintf(ec->out, "    .align 2\n");
                fprintf(ec->out, "p%d_lp%zu: .word p%d_d%zu\n", pc->prog_id, vi, pc->prog_id, vi);
            }
        }
    }

    /* String / literal pool */
    if (ec->str_count > 0) {
        fprintf(ec->out, "\n# --- String & Picture Literal Pool ---\n");
        for (size_t i = 0; i < ec->str_count; i++) {
            fprintf(ec->out, "cob_s%zu:\n", i);
            size_t total = ec->strs[i].len + (ec->strs[i].zterm ? 1 : 0);
            emit_bytes_data(ec->out, (const unsigned char *)ec->strs[i].bytes, total);
        }
    }
    fprintf(ec->out, "    .align 2\n");
}

static void free_prog_ctx(ProgCtx *pc) {
    if (!pc) return;
    for (size_t i = 0; i < pc->block_count; i++) free(pc->blocks[i].stmts);
    free(pc->blocks);
    free(pc->vars);
    free(pc->files);
    if (pc->owns_ast) ast_free((ASTNode *)pc->root);
    free(pc);
}

/* =========================================================================
 * Public Entry Point
 * ========================================================================= */

bool codegen_mips(const ASTNode *root, const SymTab *symtab,
                  const CodegenOpts *opts, FILE *out) {
    (void)symtab;
    if (!root || !out) return false;

    EmitCtx ec;
    memset(&ec, 0, sizeof(ec));
    ec.out = out;
    ec.target = opts ? opts->target : MIPS_TARGET_LINUX;
    ec.opts = opts;

    /* 1. Build main program context and auto-discover called COBOL subprograms */
    build_prog_ctx(&ec, root, false);
    scan_calls_recursive(&ec, root);

    /* 2. Emit assembly header */
    fprintf(out, "# =========================================================================\n");
    fprintf(out, "# Generated by cbc (COBOL -> Generic MIPS32 Compiler)\n");
    fprintf(out, "# Program : %s\n", ec.progs[0]->prog_name);
    fprintf(out, "# Target  : %s\n",
            ec.target == MIPS_TARGET_MARS ? "MARS / SPIM simulator" : "Linux MIPS32 o32 ELF (static, no libc)");
    fprintf(out, "# Modules : %d\n", ec.prog_count);
    fprintf(out, "# =========================================================================\n");
    if (ec.target == MIPS_TARGET_LINUX) {
        fprintf(out, "    .set reorder\n");
    }

    /* 3. Emit .text (populates string pool on the fly) */
    fprintf(out, "    .text\n");
    for (int i = 0; i < ec.prog_count; i++) {
        emit_prog_text(&ec, ec.progs[i]);
    }

    /* 4. Emit .data (all program buffers + string literal pool) */
    emit_all_data(&ec);

    /* 5. Emit self-contained MIPS32 runtime library */
    codegen_emit_runtime(out, ec.target);

    /* 6. Cleanup */
    for (int i = 0; i < ec.prog_count; i++) free_prog_ctx(ec.progs[i]);
    for (size_t i = 0; i < ec.str_count; i++) free(ec.strs[i].bytes);
    free(ec.strs);
    return true;
}
