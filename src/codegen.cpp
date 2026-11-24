#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "util.h"

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

static const int MAX_OCCURS_DEPTH = 4;
static const size_t MAX_MODULES = 16;

/* =========================================================================
 * Data Layout, File Table, Procedure Block & String Pool
 * ========================================================================= */

struct VarLayout {
    std::string name;
    int level = 0;
    int parent_idx = -1;
    int root_idx = -1;
    int fd_idx = -1;
    int section_kind = 1; /* 0=FILE, 1=WS, 2=LS, 3=LINKAGE, 4=SCREEN */
    DataKind kind = DATA_ALPHANUMERIC;
    std::string pic_raw;
    std::string pic_exp;
    int elem_size = 0;
    int total_size = 0;
    int offset_in_root = 0;
    int int_digits = 0;
    int frac_digits = 0;
    bool is_signed = false;
    bool is_edited = false;
    bool is_redefines = false;
    int redefines_idx = -1;
    int occurs = 1;
    int occurs_depth = 0;
    int occurs_stride[MAX_OCCURS_DEPTH] = {0, 0, 0, 0};
    int pic_str_id = -1;
    const ASTNode *val_ast = nullptr;
    bool is_global_retcode = false;

    bool is_large_uint() const {
        return kind == DATA_NUMERIC && !is_signed && frac_digits == 0 && elem_size >= 10;
    }
};

struct FileLayout {
    std::string name;
    std::string assign_lit;
    int assign_var_idx = -1;
    int status_var_idx = -1;
    int rec_var_idx = -1;
    bool is_line_seq = false;
    int path_str_id = -1;
};

struct ProcBlock {
    std::string name;
    bool is_section = false;
    int sec_end_block = 0;
    int global_id = 0;
    std::vector<const ASTNode *> stmts;
};

struct StrPoolItem {
    std::string bytes;
    bool zterm = false;
};

class StrPool {
public:
    int add(const std::string &bytes, bool zterm) {
        std::string key;
        key.reserve(bytes.size() + 1);
        key.push_back(zterm ? '\1' : '\0');
        key.append(bytes);

        auto it = index_.find(key);
        if (it != index_.end()) return it->second;

        int id = (int)items_.size();
        StrPoolItem item = {bytes, zterm};
        items_.push_back(item);
        index_[key] = id;
        return id;
    }

    int add_zstr(const std::string &s) { return add(s, true); }
    const std::vector<StrPoolItem> &items() const { return items_; }

private:
    std::vector<StrPoolItem> items_;
    std::unordered_map<std::string, int> index_;
};

struct ProgCtx {
    int prog_id = 0;
    std::string prog_name;
    std::string san_name;
    const ASTNode *root = nullptr;
    ASTNode *owned_ast = nullptr;
    bool decimal_comma = false;
    bool uses_perform = false;
    int prog_frac = 0;
    int prog_scale = 1;

    std::vector<VarLayout> vars;
    std::vector<FileLayout> files;
    std::vector<ProcBlock> blocks;
    std::vector<int> using_vars;

    ~ProgCtx() { ast_free(owned_ast); }

    int find_file(const std::string &name) const {
        for (size_t i = 0; i < files.size(); i++) {
            if (ci_eq(files[i].name, name)) return (int)i;
        }
        return -1;
    }

    int find_block(const std::string &name) const {
        for (size_t i = 0; i < blocks.size(); i++) {
            if (ci_eq(blocks[i].name, name)) return (int)i;
        }
        return -1;
    }

    bool has_ancestor_named(int var_idx, const std::string &qname) const {
        int p = vars[var_idx].parent_idx;
        while (p >= 0) {
            if (ci_eq(vars[p].name, qname)) return true;
            p = vars[p].parent_idx;
        }
        int fd = vars[var_idx].fd_idx;
        return fd >= 0 && fd < (int)files.size() && ci_eq(files[fd].name, qname);
    }

    int resolve_data_ref(const ASTNode *ref) const {
        if (!ref || ref->kind != AST_DATA_REF || ref->text.empty()) return -1;

        std::vector<std::string> quals;
        bool has_sub = false;
        for (size_t i = 0; i < ref->children.size(); i++) {
            const ASTNode *ch = ref->children[i];
            if (ch->kind == AST_DATA_REF && !ch->text.empty()) quals.push_back(ch->text);
            else if (ch->kind == AST_SUBSCRIPT) has_sub = true;
        }

        int best = -1;
        for (size_t i = 0; i < vars.size(); i++) {
            if (!ci_eq(vars[i].name, ref->text)) continue;
            bool ok = true;
            for (size_t q = 0; q < quals.size(); q++) {
                if (!has_ancestor_named((int)i, quals[q])) { ok = false; break; }
            }
            if (!ok) continue;
            if (best < 0 || (has_sub && vars[i].occurs_depth > vars[best].occurs_depth)) {
                best = (int)i;
            }
        }
        return best;
    }
};

struct CondCtx {
    const ASTNode *last_lhs = nullptr;
    std::string last_op;
};

/* =========================================================================
 * Small Numeric, PIC & Assembly Formatting Helpers
 * ========================================================================= */

static std::string sanitize_label(const std::string &src) {
    if (src.empty()) return "UNNAMED";
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        unsigned char c = (unsigned char)src[i];
        out.push_back(std::isalnum(c) ? (char)std::toupper(c) : '_');
    }
    return out;
}

static int pow10_int(int e) {
    int r = 1;
    for (int i = 0; i < e; i++) r *= 10;
    return r;
}

static int32_t parse_num_lit_scaled(const std::string &s, int prog_frac) {
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; i++; }
    else if (i < s.size() && s[i] == '+') { i++; }

    int64_t acc = 0;
    bool seen_dec = false;
    int frac = 0;
    for (; i < s.size(); i++) {
        char c = s[i];
        if ((c == '.' || c == ',') && !seen_dec) { seen_dec = true; continue; }
        if (c >= '0' && c <= '9') {
            acc = acc * 10 + (c - '0');
            if (seen_dec) frac++;
        }
    }
    int diff = prog_frac - frac;
    while (diff > 0) { acc *= 10; diff--; }
    while (diff < 0) { acc /= 10; diff++; }
    return (int32_t)(neg ? -acc : acc);
}

static bool is_printable_ascii(const std::string &s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126) return false;
    }
    return true;
}

static void emit_string_data(FILE *out, const std::string &s, bool zterm) {
    if (is_printable_ascii(s)) {
        std::fprintf(out, "    .%s \"", zterm ? "asciiz" : "ascii");
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '"' || s[i] == '\\') std::fputc('\\', out);
            std::fputc(s[i], out);
        }
        std::fputs("\"\n", out);
        return;
    }
    size_t total = s.size() + (zterm ? 1 : 0);
    if (total == 0) {
        std::fprintf(out, "    .byte 0\n");
        return;
    }
    const unsigned char *buf = (const unsigned char *)s.c_str();
    for (size_t i = 0; i < total; i += 16) {
        std::fprintf(out, "    .byte ");
        for (size_t j = 0; j < 16 && i + j < total; j++) {
            if (j > 0) std::fputc(',', out);
            std::fprintf(out, "%u", (unsigned)buf[i + j]);
        }
        std::fputc('\n', out);
    }
}

static void emit_raw_bytes(FILE *out, const std::vector<unsigned char> &buf) {
    if (buf.empty()) {
        std::fprintf(out, "    .byte 0\n");
        return;
    }
    /* Emit compact .space when buffer is all NUL, otherwise readable .ascii or .byte */
    bool all_ascii = true;
    for (size_t i = 0; i < buf.size(); i++) {
        if (buf[i] < 32 || buf[i] > 126) { all_ascii = false; break; }
    }
    if (all_ascii && buf.size() <= 80) {
        std::string s((const char *)buf.data(), buf.size());
        emit_string_data(out, s, false);
        return;
    }
    for (size_t i = 0; i < buf.size(); i += 16) {
        std::fprintf(out, "    .byte ");
        for (size_t j = 0; j < 16 && i + j < buf.size(); j++) {
            if (j > 0) std::fputc(',', out);
            std::fprintf(out, "%u", (unsigned)buf[i + j]);
        }
        std::fputc('\n', out);
    }
}

static void parse_pic_into_var(VarLayout &v, const std::string &pic, bool dec_comma) {
    v.pic_raw = pic;
    v.pic_exp.clear();
    v.elem_size = 0;
    v.int_digits = 0;
    v.frac_digits = 0;
    v.is_signed = false;
    v.is_edited = false;
    v.kind = DATA_ALPHANUMERIC;
    if (pic.empty()) return;

    std::string exp;
    bool has_x = false, has_a = false, has_9 = false, has_edit = false;
    bool has_s = false, seen_v = false;

    for (size_t i = 0; i < pic.size(); i++) {
        char c = (char)std::toupper((unsigned char)pic[i]);
        if (c == ' ' || c == '\t') continue;
        int rep = 1;
        if (i + 1 < pic.size() && pic[i + 1] == '(') {
            size_t j = i + 2;
            int val = 0;
            while (j < pic.size() && std::isdigit((unsigned char)pic[j])) {
                val = val * 10 + (pic[j] - '0');
                j++;
            }
            if (j < pic.size() && pic[j] == ')' && val > 0) {
                rep = val;
                i = j;
            }
        }
        if (c == 'S') { has_s = true; continue; }
        if (c == 'V') { seen_v = true; continue; }
        for (int r = 0; r < rep; r++) {
            exp.push_back(c);
            if (c == 'X') has_x = true;
            else if (c == 'A') has_a = true;
            else if (c == '9') {
                has_9 = true;
                if (seen_v) v.frac_digits++; else v.int_digits++;
            } else {
                has_edit = true;
                if (c == 'Z') {
                    if (seen_v) v.frac_digits++; else v.int_digits++;
                }
            }
        }
    }
    v.pic_exp = exp;

    if (has_x) {
        v.kind = DATA_ALPHANUMERIC;
        v.elem_size = (int)exp.size();
        v.frac_digits = 0;
    } else if (has_a && !has_9 && !has_edit) {
        v.kind = DATA_ALPHA;
        v.elem_size = (int)exp.size();
        v.frac_digits = 0;
    } else if (has_edit) {
        v.kind = DATA_NUMERIC_EDITED;
        v.is_edited = true;
        v.elem_size = (int)exp.size();
        char dec_sep = dec_comma ? ',' : '.';
        size_t dp = exp.rfind(dec_sep);
        v.frac_digits = 0;
        if (dp != std::string::npos) {
            for (size_t k = dp + 1; k < exp.size(); k++) {
                if (exp[k] == '9' || exp[k] == 'Z') v.frac_digits++;
            }
        }
    } else if (has_9) {
        v.kind = DATA_NUMERIC;
        v.is_signed = has_s;
        v.elem_size = (int)exp.size() + (has_s ? 1 : 0);
    } else {
        v.kind = DATA_ALPHANUMERIC;
        v.elem_size = exp.empty() ? 1 : (int)exp.size();
    }
    if (v.elem_size <= 0) v.elem_size = 1;
}

static unsigned char figurative_byte(const std::string &text) {
    if (ci_eq(text, "ZERO") || ci_eq(text, "ZEROS") || ci_eq(text, "ZEROES")) return '0';
    if (ci_eq(text, "LOW-VALUE") || ci_eq(text, "LOW-VALUES")) return 0;
    if (ci_eq(text, "HIGH-VALUE") || ci_eq(text, "HIGH-VALUES")) return 0xFF;
    if (ci_eq(text, "QUOTE") || ci_eq(text, "QUOTES")) return '"';
    return ' ';
}

static void fill_value_bytes(const VarLayout &v, const ASTNode *val_clause,
                             unsigned char *dst, size_t dlen) {
    if (!val_clause || val_clause->children.empty()) return;
    const ASTNode *item = val_clause->children[0];
    if (!item) return;

    if (item->kind == AST_FIGURATIVE && !item->text.empty()) {
        unsigned char ch = figurative_byte(item->text);
        std::memset(dst, ch, dlen);
        if (v.kind == DATA_NUMERIC && v.is_signed && ch == '0' && dlen > 0) dst[0] = '+';
        return;
    }

    if (item->kind == AST_LITERAL_STR && !item->text.empty()) {
        std::string buf = unquote_cobol(item->text);
        if (!item->extra.empty() && ci_eq(item->extra, "ALL") && !buf.empty()) {
            for (size_t i = 0; i < dlen; i++) dst[i] = (unsigned char)buf[i % buf.size()];
        } else {
            std::memset(dst, ' ', dlen);
            size_t cp = buf.size() < dlen ? buf.size() : dlen;
            if (cp > 0) std::memcpy(dst, buf.data(), cp);
        }
        return;
    }

    if (item->kind == AST_LITERAL_NUM && !item->text.empty()) {
        if (v.kind == DATA_NUMERIC) {
            int64_t val = parse_num_lit_scaled(item->text, v.frac_digits);
            bool neg = val < 0;
            if (neg) val = -val;
            size_t start = 0;
            if (v.is_signed && dlen > 0) {
                dst[0] = neg ? '-' : '+';
                start = 1;
            }
            for (size_t p = dlen; p > start; p--) {
                dst[p - 1] = (unsigned char)('0' + (val % 10));
                val /= 10;
            }
        } else {
            std::memset(dst, ' ', dlen);
            size_t cp = item->text.size() < dlen ? item->text.size() : dlen;
            std::memcpy(dst, item->text.data(), cp);
        }
    }
}

static void init_var_tree(const ProgCtx &pc, int idx, std::vector<unsigned char> &root_buf, int base_off) {
    const VarLayout &v = pc.vars[idx];
    if (v.kind == DATA_COND || (v.is_redefines && !v.val_ast)) return;

    if (v.kind != DATA_GROUP) {
        if (base_off >= 0 && (size_t)(base_off + v.elem_size) <= root_buf.size()) {
            unsigned char *dst = root_buf.data() + base_off;
            if (!v.is_redefines) {
                if (v.kind == DATA_NUMERIC || v.kind == DATA_INDEX) {
                    std::memset(dst, '0', (size_t)v.elem_size);
                    if (v.is_signed && v.elem_size > 0) dst[0] = '+';
                } else {
                    std::memset(dst, ' ', (size_t)v.elem_size);
                }
            }
            if (v.val_ast) fill_value_bytes(v, v.val_ast, dst, (size_t)v.elem_size);
        }
    } else {
        if (v.val_ast && base_off >= 0 && (size_t)(base_off + v.elem_size) <= root_buf.size()) {
            fill_value_bytes(v, v.val_ast, root_buf.data() + base_off, (size_t)v.elem_size);
        }
        for (size_t j = (size_t)idx + 1; j < pc.vars.size(); j++) {
            if (pc.vars[j].parent_idx != idx || pc.vars[j].kind == DATA_COND) continue;
            int child_rel = base_off + (pc.vars[j].offset_in_root - v.offset_in_root);
            init_var_tree(pc, (int)j, root_buf, child_rel);
        }
    }

    if (v.occurs > 1 && base_off >= 0 && (size_t)(base_off + v.total_size) <= root_buf.size()) {
        for (int k = 1; k < v.occurs; k++) {
            std::memcpy(root_buf.data() + base_off + k * v.elem_size,
                        root_buf.data() + base_off, (size_t)v.elem_size);
        }
    }
}

static bool ast_has_perform(const ASTNode *n) {
    if (!n) return false;
    if (n->kind == AST_STMT_PERFORM && !ci_eq(n->text, "INLINE")) return true;
    for (size_t i = 0; i < n->children.size(); i++) {
        if (ast_has_perform(n->children[i])) return true;
    }
    return false;
}

/* =========================================================================
 * MIPS32 Code Generator (Dragon Book Style: Direct Control Flow & On-Demand RT)
 * ========================================================================= */

class MipsCodegen {
public:
    MipsCodegen(FILE *out, const CodegenOpts &opts)
        : out_(out), target_(opts.target), opts_(opts) {}

    ~MipsCodegen() {
        for (size_t i = 0; i < progs_.size(); i++) delete progs_[i];
    }

    bool generate(const ASTNode *root) {
        build_prog_ctx(root, nullptr);
        scan_calls_recursive(root);

        emit("# =========================================================================\n");
        emit("# Generated by cbc (COBOL -> Generic MIPS32 Compiler)\n");
        emit("# Program : %s\n", progs_[0]->prog_name.c_str());
        emit("# Target  : %s\n",
             target_ == MIPS_TARGET_MARS
                 ? "MARS / SPIM simulator"
                 : "Linux MIPS32 o32 ELF (static, no libc)");
        emit("# Modules : %zu\n", progs_.size());
        emit("# =========================================================================\n");
        if (target_ == MIPS_TARGET_LINUX) emit("    .set reorder\n");

        emit("    .text\n");
        for (size_t i = 0; i < progs_.size(); i++) emit_prog_text(*progs_[i]);
        emit_all_data();
        codegen_emit_runtime(out_, target_, used_rt_);
        return true;
    }

private:
    FILE *out_;
    MipsTarget target_;
    const CodegenOpts &opts_;
    std::vector<ProgCtx *> progs_;
    StrPool strs_;
    uint32_t used_rt_ = RT_EXIT;
    int next_label_ = 0;
    int next_block_gid_ = 0;
    const ProgCtx *cur_ = nullptr;

    void emit(const char *fmt, ...) const {
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(out_, fmt, ap);
        va_end(ap);
    }

    void use_rt(uint32_t mask) { used_rt_ |= mask; }

    void call_rt(const char *fn, uint32_t mask) {
        used_rt_ |= mask;
        emit("    jal  %s\n", fn);
    }

    int new_lbl() { return next_label_++; }

    void push_reg(const char *r) const {
        emit("    addiu $sp, $sp, -4\n    sw   %s, 0($sp)\n", r);
    }

    void pop_reg(const char *r) const {
        emit("    lw   %s, 0($sp)\n    addiu $sp, $sp, 4\n", r);
    }

    void push_pair(const char *r1, const char *r2) const {
        emit("    addiu $sp, $sp, -8\n    sw   %s, 0($sp)\n    sw   %s, 4($sp)\n", r1, r2);
    }

    void pop_pair(const char *r1, const char *r2) const {
        emit("    lw   %s, 0($sp)\n    lw   %s, 4($sp)\n    addiu $sp, $sp, 8\n", r1, r2);
    }

    void unscale_v0() const {
        if (cur_->prog_scale > 1) {
            emit("    li   $t0, %d\n    div  $v0, $t0\n    mflo $v0\n", cur_->prog_scale);
        }
    }

    static ASTNode make_ref(const std::string &name) {
        ASTNode n;
        n.kind = AST_DATA_REF;
        n.text = name;
        return n;
    }

    bool try_const_num(const ASTNode *n, int32_t &out_val) const {
        if (!n) return false;
        if (n->kind == AST_LITERAL_NUM) {
            out_val = parse_num_lit_scaled(n->text, cur_->prog_frac);
            return true;
        }
        if (n->kind == AST_FIGURATIVE) {
            if (ci_eq(n->text, "ZERO") || ci_eq(n->text, "ZEROS") || ci_eq(n->text, "ZEROES")) {
                out_val = 0;
                return true;
            }
            if (ci_eq(n->text, "TRUE")) { out_val = cur_->prog_scale; return true; }
            if (ci_eq(n->text, "FALSE")) { out_val = 0; return true; }
        }
        return false;
    }

    /* =====================================================================
     * Phase 1: Layout & Program Discovery
     * ===================================================================== */

    static void collect_env_info(ProgCtx &pc, const ASTNode *node) {
        if (!node) return;
        if (node->is_clause("DECIMAL-POINT-IS-COMMA")) pc.decimal_comma = true;
        if (node->kind == AST_SELECT && !node->text.empty()) {
            FileLayout f;
            f.name = to_upper(node->text);
            if (const ASTNode *asgn = node->find_clause("ASSIGN")) {
                if (!asgn->children.empty()) {
                    const ASTNode *a = asgn->children[0];
                    f.assign_lit = (a->kind == AST_LITERAL_STR) ? unquote_cobol(a->text) : a->text;
                }
            }
            if (const ASTNode *org = node->find_clause("ORGANIZATION")) {
                if (org->ival == 1) f.is_line_seq = true;
            }
            pc.files.push_back(f);
        }
        for (size_t i = 0; i < node->children.size(); i++) collect_env_info(pc, node->children[i]);
    }

    void collect_data_items(ProgCtx &pc, const ASTNode *parent, int sec_kind, int fd_idx) {
        struct StackFrame { int idx; int level; };
        std::vector<StackFrame> stack;

        for (size_t i = 0; i < parent->children.size(); i++) {
            const ASTNode *n = parent->children[i];
            if (n->kind != AST_DATA_ITEM) continue;

            VarLayout v;
            v.name = to_upper(n->text.empty() ? "FILLER" : n->text);
            v.level = n->ival;
            v.fd_idx = fd_idx;
            v.section_kind = sec_kind;

            const ASTNode *pic_cl = n->find_clause("PIC");
            v.val_ast = n->find_clause("VALUE");
            const ASTNode *redef_cl = n->find_clause("REDEFINES");
            const ASTNode *occ_cl = n->find_clause("OCCURS");

            const ASTNode *indexed_by = nullptr;
            if (occ_cl) {
                v.occurs = occ_cl->ival > 0 ? occ_cl->ival : 1;
                if (!occ_cl->children.empty()) indexed_by = occ_cl->children[0];
            }

            if (v.level == 88) {
                v.kind = DATA_COND;
                if (!stack.empty()) v.parent_idx = stack.back().idx;
                pc.vars.push_back(v);
                continue;
            }

            if (v.level == 1 || v.level == 77) {
                stack.clear();
            } else {
                while (!stack.empty() && stack.back().level >= v.level) stack.pop_back();
                if (!stack.empty()) v.parent_idx = stack.back().idx;
            }

            if (pic_cl && !pic_cl->extra.empty()) {
                parse_pic_into_var(v, pic_cl->extra, pc.decimal_comma);
                if (v.is_edited) v.pic_str_id = strs_.add_zstr(v.pic_exp);
            } else {
                v.kind = DATA_GROUP;
            }

            if (redef_cl && !redef_cl->extra.empty()) {
                for (int j = (int)pc.vars.size() - 1; j >= 0; j--) {
                    if (pc.vars[j].parent_idx == v.parent_idx && ci_eq(pc.vars[j].name, redef_cl->extra)) {
                        v.is_redefines = true;
                        v.redefines_idx = j;
                        break;
                    }
                }
            }

            int idx = (int)pc.vars.size();
            pc.vars.push_back(v);
            if (fd_idx >= 0 && v.level == 1 && pc.files[fd_idx].rec_var_idx < 0) {
                pc.files[fd_idx].rec_var_idx = idx;
            }
            StackFrame sf = {idx, v.level};
            stack.push_back(sf);

            if (indexed_by && indexed_by->kind == AST_CLAUSE) {
                for (size_t m = 0; m < indexed_by->children.size(); m++) {
                    const ASTNode *ix = indexed_by->children[m];
                    if (ix->kind == AST_DATA_REF && !ix->text.empty()) {
                        VarLayout iv;
                        iv.name = to_upper(ix->text);
                        iv.level = 77;
                        iv.kind = DATA_INDEX;
                        iv.elem_size = 9;
                        iv.total_size = 9;
                        iv.int_digits = 9;
                        pc.vars.push_back(iv);
                    }
                }
            }
        }
    }

    void resolve_select_links(ProgCtx &pc, const ASTNode *node) {
        if (!node) return;
        if (node->kind == AST_SELECT && !node->text.empty()) {
            int fi = pc.find_file(node->text);
            if (fi >= 0) {
                if (const ASTNode *fs = node->find_clause("FILE-STATUS")) {
                    if (!fs->children.empty()) pc.files[fi].status_var_idx = pc.resolve_data_ref(fs->children[0]);
                }
                if (const ASTNode *asgn = node->find_clause("ASSIGN")) {
                    if (!asgn->children.empty() && asgn->children[0]->kind == AST_DATA_REF) {
                        pc.files[fi].assign_var_idx = pc.resolve_data_ref(asgn->children[0]);
                    }
                }
                if (pc.files[fi].assign_var_idx < 0) {
                    const std::string &p = !pc.files[fi].assign_lit.empty()
                                               ? pc.files[fi].assign_lit
                                               : pc.files[fi].name;
                    pc.files[fi].path_str_id = strs_.add_zstr(p);
                }
            }
        }
        for (size_t i = 0; i < node->children.size(); i++) resolve_select_links(pc, node->children[i]);
    }

    static void compute_var_sizes_and_offsets(ProgCtx &pc) {
        for (int i = (int)pc.vars.size() - 1; i >= 0; i--) {
            VarLayout &v = pc.vars[i];
            if (v.kind == DATA_COND) continue;
            if (v.kind == DATA_GROUP) {
                int cur_off = 0, max_end = 0;
                for (size_t j = (size_t)i + 1; j < pc.vars.size(); j++) {
                    if (pc.vars[j].parent_idx != i || pc.vars[j].kind == DATA_COND) continue;
                    int child_off = (pc.vars[j].is_redefines && pc.vars[j].redefines_idx >= 0)
                                        ? pc.vars[pc.vars[j].redefines_idx].offset_in_root
                                        : cur_off;
                    if (!pc.vars[j].is_redefines) cur_off += pc.vars[j].total_size;
                    pc.vars[j].offset_in_root = child_off;
                    if (child_off + pc.vars[j].total_size > max_end) {
                        max_end = child_off + pc.vars[j].total_size;
                    }
                }
                v.elem_size = max_end > 0 ? max_end : 1;
            }
            v.total_size = v.elem_size * (v.occurs > 0 ? v.occurs : 1);
        }

        for (size_t i = 0; i < pc.vars.size(); i++) {
            VarLayout &v = pc.vars[i];
            if (v.kind == DATA_COND) continue;
            if (v.parent_idx < 0) {
                v.root_idx = (v.is_redefines && v.redefines_idx >= 0)
                                 ? pc.vars[v.redefines_idx].root_idx
                                 : (int)i;
                v.offset_in_root = 0;
                v.occurs_depth = 0;
                if (v.occurs > 1) v.occurs_stride[v.occurs_depth++] = v.elem_size;
            } else {
                const VarLayout &p = pc.vars[v.parent_idx];
                v.root_idx = p.root_idx;
                v.offset_in_root += p.offset_in_root;
                v.occurs_depth = p.occurs_depth;
                for (int d = 0; d < p.occurs_depth && d < MAX_OCCURS_DEPTH; d++) {
                    v.occurs_stride[d] = p.occurs_stride[d];
                }
                if (v.occurs > 1 && v.occurs_depth < MAX_OCCURS_DEPTH) {
                    v.occurs_stride[v.occurs_depth++] = v.elem_size;
                }
            }
        }

        int max_frac = 0;
        for (size_t i = 0; i < pc.vars.size(); i++) {
            if (pc.vars[i].frac_digits > max_frac) max_frac = pc.vars[i].frac_digits;
        }
        if (max_frac > 4) max_frac = 4;
        pc.prog_frac = max_frac;
        pc.prog_scale = pow10_int(max_frac);
    }

    int add_proc_block(ProgCtx &pc, const std::string &name, bool is_sec) {
        ProcBlock b;
        b.name = to_upper(name);
        b.is_section = is_sec;
        b.sec_end_block = (int)pc.blocks.size();
        b.global_id = ++next_block_gid_;
        pc.blocks.push_back(b);
        return (int)pc.blocks.size() - 1;
    }

    void build_proc_blocks(ProgCtx &pc, const ASTNode *proc_div) {
        if (!proc_div) return;
        pc.uses_perform = ast_has_perform(proc_div);
        int cur = -1, cur_sec = -1;

        for (size_t i = 0; i < proc_div->children.size(); i++) {
            const ASTNode *n = proc_div->children[i];
            if (!n) continue;
            if (n->is_clause("USING")) {
                for (size_t k = 0; k < n->children.size() && pc.using_vars.size() < 8; k++) {
                    int vi = pc.resolve_data_ref(n->children[k]);
                    if (vi >= 0) pc.using_vars.push_back(pc.vars[vi].root_idx);
                }
                continue;
            }
            if (n->kind == AST_PROC_SECTION) {
                cur = add_proc_block(pc, n->text.empty() ? "SEC" : n->text, true);
                cur_sec = cur;
            } else if (n->kind == AST_PARAGRAPH) {
                cur = add_proc_block(pc, n->text.empty() ? "PARA" : n->text, false);
                if (cur_sec >= 0) pc.blocks[cur_sec].sec_end_block = cur;
            } else if (n->kind == AST_SENTENCE) {
                if (cur < 0) cur = add_proc_block(pc, "__ENTRY__", false);
                for (size_t k = 0; k < n->children.size(); k++) {
                    if (n->children[k]) pc.blocks[cur].stmts.push_back(n->children[k]);
                }
            }
        }
        if (pc.blocks.empty()) add_proc_block(pc, "__EMPTY__", false);
    }

    ProgCtx *build_prog_ctx(const ASTNode *root, ASTNode *owned_ast) {
        ProgCtx *pc = new ProgCtx();
        pc->prog_id = (int)progs_.size();
        pc->root = root;
        pc->owned_ast = owned_ast;
        pc->prog_name = to_upper((root && !root->text.empty()) ? root->text : "MAIN");
        pc->san_name = sanitize_label(pc->prog_name);

        VarLayout rc;
        rc.name = "RETURN-CODE"; rc.level = 77; rc.root_idx = 0;
        rc.kind = DATA_NUMERIC; rc.is_signed = true; rc.elem_size = 10; rc.total_size = 10;
        rc.int_digits = 9; rc.is_global_retcode = true;
        pc->vars.push_back(rc);

        VarLayout tl;
        tl.name = "TALLY"; tl.level = 77; tl.root_idx = 1;
        tl.kind = DATA_NUMERIC; tl.elem_size = 9; tl.total_size = 9; tl.int_digits = 9;
        pc->vars.push_back(tl);

        for (size_t i = 0; root && i < root->children.size(); i++) {
            if (root->children[i]->kind == AST_ENVIRONMENT) collect_env_info(*pc, root->children[i]);
        }

        for (size_t i = 0; root && i < root->children.size(); i++) {
            const ASTNode *dd = root->children[i];
            if (dd->kind != AST_DATA_DIVISION) continue;
            for (size_t s = 0; s < dd->children.size(); s++) {
                const ASTNode *sec = dd->children[s];
                if (sec->kind != AST_DATA_SECTION) continue;
                int sk = 1;
                if (sec->text == "FILE") sk = 0;
                else if (sec->text == "LOCAL-STORAGE") sk = 2;
                else if (sec->text == "LINKAGE") sk = 3;
                else if (sec->text == "SCREEN") sk = 4;

                if (sk == 0) {
                    for (size_t f = 0; f < sec->children.size(); f++) {
                        const ASTNode *fd = sec->children[f];
                        if (fd->kind != AST_FD || fd->text.empty()) continue;
                        collect_data_items(*pc, fd, 0, pc->find_file(fd->text));
                    }
                } else {
                    collect_data_items(*pc, sec, sk, -1);
                }
            }
        }

        compute_var_sizes_and_offsets(*pc);

        for (size_t i = 0; root && i < root->children.size(); i++) {
            if (root->children[i]->kind == AST_ENVIRONMENT) resolve_select_links(*pc, root->children[i]);
        }
        for (size_t i = 0; root && i < root->children.size(); i++) {
            if (root->children[i]->kind == AST_PROCEDURE_DIVISION) build_proc_blocks(*pc, root->children[i]);
        }

        progs_.push_back(pc);
        return pc;
    }

    void scan_calls_recursive(const ASTNode *node) {
        if (!node) return;
        if (node->kind == AST_STMT_CALL && !node->children.empty()) {
            const ASTNode *tgt = node->children[0];
            if (tgt && tgt->kind == AST_LITERAL_STR && !tgt->text.empty()) {
                std::string subname = unquote_cobol(tgt->text);
                bool exists = false;
                for (size_t i = 0; i < progs_.size(); i++) {
                    if (ci_eq(progs_[i]->prog_name, subname)) { exists = true; break; }
                }
                static const std::vector<std::string> sub_exts = {".cbl", ".cob", ".CBL", ".COB", ""};
                if (!exists && progs_.size() < MAX_MODULES) {
                    std::string path = find_cobol_file(
                        subname, opts_.input_path, opts_.copy_dirs, sub_exts, true);
                    if (!path.empty()) {
                        TokenList toks;
                        lexer_reset_errors();
                        OPTS lo = opts_.lex_opts ? *opts_.lex_opts : OPTS{false, false};
                        if (lex_cobol_file(path, lo, nullptr, &toks, opts_.copy_dirs, true) &&
                            lexer_error_count() == 0) {
                            int perr = 0;
                            ASTNode *sub_ast = parse_tokens(toks, path, &perr);
                            if (sub_ast && perr == 0) {
                                SymTab stab;
                                if (sema_analyze(sub_ast, path, stab)) {
                                    ProgCtx *sub_pc = build_prog_ctx(sub_ast, sub_ast);
                                    if (!ci_eq(sub_pc->prog_name, subname)) {
                                        sub_pc->prog_name = to_upper(subname);
                                        sub_pc->san_name = sanitize_label(subname);
                                    }
                                    scan_calls_recursive(sub_ast);
                                } else {
                                    ast_free(sub_ast);
                                }
                            } else {
                                ast_free(sub_ast);
                            }
                        }
                    }
                }
            }
        }
        for (size_t i = 0; i < node->children.size(); i++) scan_calls_recursive(node->children[i]);
    }

    /* =====================================================================
     * Phase 2: Address & Numeric Expression Generation (Dragon Book Ch. 6/8)
     * ===================================================================== */

    /* Load address of data_ref into addr_reg and length into len_reg.
     * Direct register targeting avoids redundant move $a0, $v0; move $a1, $v1. */
    void emit_data_ref_addr_len(const ASTNode *ref, int *out_var_idx,
                                const char *addr_reg = "$v0", const char *len_reg = "$v1") {
        int vi = cur_->resolve_data_ref(ref);
        if (out_var_idx) *out_var_idx = vi;
        if (vi < 0) {
            use_rt(RT_NUM);
            emit("    la   %s, _rt_numbuf\n    li   %s, 1\n", addr_reg, len_reg);
            return;
        }

        const VarLayout &v = cur_->vars[vi];
        int root = v.root_idx >= 0 ? v.root_idx : vi;
        const VarLayout &rv = cur_->vars[root];

        const ASTNode *sub_node = nullptr, *refmod_node = nullptr;
        for (size_t i = 0; i < ref->children.size(); i++) {
            if (ref->children[i]->kind == AST_SUBSCRIPT) sub_node = ref->children[i];
            else if (ref->children[i]->kind == AST_REFMOD) refmod_node = ref->children[i];
        }

        /* Fast path: simple static or linkage variable with no subscript/refmod */
        const char *dst_a = (sub_node || refmod_node) ? "$v0" : addr_reg;
        if (v.is_global_retcode) {
            use_rt(RT_RETCODE);
            emit("    la   %s, cob_g_retcode\n", dst_a);
        } else if (rv.section_kind == 3) {
            emit("    la   $t9, p%d_lp%d\n    lw   %s, 0($t9)\n", cur_->prog_id, root, dst_a);
        } else {
            emit("    la   %s, p%d_d%d\n", dst_a, cur_->prog_id, root);
        }
        if (v.offset_in_root > 0) emit("    addiu %s, %s, %d\n", dst_a, dst_a, v.offset_in_root);

        if (!sub_node && !refmod_node) {
            int cur_len = (v.occurs <= 1) ? v.elem_size : v.total_size;
            emit("    li   %s, %d\n", len_reg, cur_len);
            return;
        }

        if (sub_node && v.occurs_depth > 0) {
            push_reg("$v0");
            size_t nsubs = sub_node->children.size();
            if ((int)nsubs > v.occurs_depth) nsubs = (size_t)v.occurs_depth;
            for (size_t s = 0; s < nsubs; s++) {
                emit_eval_num(sub_node->children[s]);
                unscale_v0();
                emit("    addiu $v0, $v0, -1\n");
                int lbl_ok = new_lbl();
                emit("    bgez $v0, L%d\n    li   $v0, 0\nL%d:\n", lbl_ok, lbl_ok);
                emit("    li   $t0, %d\n    mult $v0, $t0\n    mflo $t0\n", v.occurs_stride[s]);
                emit("    lw   $t1, 0($sp)\n    addu $t1, $t1, $t0\n    sw   $t1, 0($sp)\n");
            }
            pop_reg("$v0");
        }

        int cur_len = (sub_node || v.occurs <= 1) ? v.elem_size : v.total_size;
        if (refmod_node && !refmod_node->children.empty()) {
            emit("    addiu $sp, $sp, -8\n    sw   $v0, 0($sp)\n");
            emit_eval_num(refmod_node->children[0]);
            unscale_v0();
            emit("    addiu $v0, $v0, -1\n");
            int lbl_p = new_lbl();
            emit("    bgez $v0, L%d\n    li   $v0, 0\nL%d:\n    sw   $v0, 4($sp)\n", lbl_p, lbl_p);
            if (refmod_node->children.size() >= 2) {
                emit_eval_num(refmod_node->children[1]);
                unscale_v0();
                emit("    move $v1, $v0\n");
            } else {
                emit("    lw   $t0, 4($sp)\n    li   $v1, %d\n    subu $v1, $v1, $t0\n", cur_len);
            }
            emit("    lw   $v0, 0($sp)\n    lw   $t0, 4($sp)\n    addu $v0, $v0, $t0\n    addiu $sp, $sp, 8\n");
        } else {
            emit("    li   $v1, %d\n", cur_len);
        }
        if (std::strcmp(addr_reg, "$v0") != 0) emit("    move %s, $v0\n", addr_reg);
        if (std::strcmp(len_reg, "$v1") != 0)  emit("    move %s, $v1\n", len_reg);
    }

    void emit_addr_len(const ASTNode *rv, int *out_var_idx = nullptr,
                       const char *addr_reg = "$v0", const char *len_reg = "$v1") {
        if (out_var_idx) *out_var_idx = -1;
        if (!rv) {
            use_rt(RT_NUM);
            emit("    la   %s, _rt_numbuf\n    li   %s, 0\n", addr_reg, len_reg);
            return;
        }
        if (rv->kind == AST_DATA_REF) {
            emit_data_ref_addr_len(rv, out_var_idx, addr_reg, len_reg);
            return;
        }
        if (rv->kind == AST_LITERAL_STR) {
            std::string buf = unquote_cobol(rv->text);
            int sid = strs_.add(buf, false);
            emit("    la   %s, cob_s%d\n    li   %s, %zu\n", addr_reg, sid, len_reg, buf.size());
            return;
        }
        if (rv->kind == AST_LITERAL_NUM) {
            std::string s = rv->text.empty() ? "0" : rv->text;
            int sid = strs_.add(s, false);
            emit("    la   %s, cob_s%d\n    li   %s, %zu\n", addr_reg, sid, len_reg, s.size());
            return;
        }
        if (rv->kind == AST_FIGURATIVE && !rv->text.empty()) {
            std::string s(1, (char)figurative_byte(rv->text));
            int sid = strs_.add(s, false);
            emit("    la   %s, cob_s%d\n    li   %s, 1\n", addr_reg, sid, len_reg);
            return;
        }
        if (rv->kind == AST_FUNC_CALL && !rv->text.empty()) {
            if ((ci_eq(rv->text, "REVERSE") || ci_eq(rv->text, "UPPER-CASE")) && !rv->children.empty()) {
                const char *helper = ci_eq(rv->text, "REVERSE") ? "_rt_reverse" : "_rt_upper_case";
                emit_addr_len(rv->children[0], nullptr, "$a1", "$a2");
                push_reg("$a2");
                emit("    la   $a0, _rt_revbuf\n");
                call_rt(helper, RT_STR_FUNCS);
                emit("    la   %s, _rt_revbuf\n", addr_reg);
                pop_reg(len_reg);
                return;
            }
            if (ci_eq(rv->text, "CURRENT-DATE")) {
                emit("    la   $a0, _rt_datebuf\n    li   $a1, 8\n    li   $a2, 0\n");
                call_rt("_rt_accept_datetime", RT_DATETIME);
                emit("    la   $a0, _rt_datebuf\n    addiu $a0, $a0, 8\n    li   $a1, 8\n    li   $a2, 4\n");
                call_rt("_rt_accept_datetime", RT_DATETIME);
                emit("    la   %s, _rt_datebuf\n    li   %s, 16\n", addr_reg, len_reg);
                return;
            }
            if (!rv->children.empty()) {
                emit_addr_len(rv->children[0], nullptr, addr_reg, len_reg);
                return;
            }
        }

        emit_eval_num(rv);
        int packed = (cur_->prog_frac << 8) | (cur_->prog_frac << 1) | 1;
        emit("    la   $a0, _rt_numbuf\n    li   $a1, 12\n    move $a2, $v0\n    li   $a3, %d\n", packed);
        call_rt("_rt_format_num", RT_NUM);
        emit("    la   %s, _rt_numbuf\n    li   %s, 12\n", addr_reg, len_reg);
    }

    void emit_eval_num(const ASTNode *expr) {
        if (!expr) { emit("    li   $v0, 0\n"); return; }

        int32_t kval = 0;
        if (try_const_num(expr, kval)) {
            emit("    li   $v0, %d\n", (int)kval);
            return;
        }

        switch (expr->kind) {
            case AST_LITERAL_STR:
                emit("    li   $v0, %d\n", (int)parse_num_lit_scaled(unquote_cobol(expr->text), cur_->prog_frac));
                return;
            case AST_DATA_REF: {
                int vi = -1;
                emit_data_ref_addr_len(expr, &vi, "$a0", "$a1");
                int imp_frac = (vi >= 0 && cur_->vars[vi].kind == DATA_NUMERIC) ? cur_->vars[vi].frac_digits : 0;
                emit("    li   $a2, %d\n    li   $a3, %d\n", imp_frac, cur_->prog_frac);
                call_rt("_rt_parse_num", RT_NUM);
                return;
            }
            case AST_LENGTH_OF: {
                int vi = (!expr->children.empty()) ? cur_->resolve_data_ref(expr->children[0]) : -1;
                int bytes = (vi >= 0) ? cur_->vars[vi].total_size : 0;
                emit("    li   $v0, %d\n", bytes * cur_->prog_scale);
                return;
            }
            case AST_UNOP:
                if (!expr->children.empty()) emit_eval_num(expr->children[0]);
                if (expr->text == "-") emit("    subu $v0, $zero, $v0\n");
                return;
            case AST_BINOP: {
                if (expr->children.size() < 2) { emit("    li   $v0, 0\n"); return; }
                const std::string &op = expr->text;
                int32_t rconst = 0;
                /* Fast path: no stack spill when RHS is a constant literal */
                if (try_const_num(expr->children[1], rconst)) {
                    emit_eval_num(expr->children[0]);
                    if (op == "+") {
                        if (rconst >= -32768 && rconst <= 32767) emit("    addiu $v0, $v0, %d\n", (int)rconst);
                        else emit("    li   $t0, %d\n    addu $v0, $v0, $t0\n", (int)rconst);
                        return;
                    }
                    if (op == "-") {
                        if (-rconst >= -32768 && -rconst <= 32767) emit("    addiu $v0, $v0, %d\n", (int)(-rconst));
                        else emit("    li   $t0, %d\n    subu $v0, $v0, $t0\n", (int)rconst);
                        return;
                    }
                    const char *fn = (op == "*") ? "_rt_mul_scaled" :
                                     (op == "/") ? "_rt_div_scaled" : "_rt_pow_scaled";
                    emit("    move $a0, $v0\n    li   $a1, %d\n    li   $a2, %d\n", (int)rconst, cur_->prog_scale);
                    call_rt(fn, RT_ARITH);
                    return;
                }

                emit_eval_num(expr->children[0]);
                push_reg("$v0");
                emit_eval_num(expr->children[1]);
                pop_reg("$t0");
                if (op == "+") emit("    addu $v0, $t0, $v0\n");
                else if (op == "-") emit("    subu $v0, $t0, $v0\n");
                else {
                    const char *fn = (op == "*") ? "_rt_mul_scaled" :
                                     (op == "/") ? "_rt_div_scaled" : "_rt_pow_scaled";
                    emit("    move $a0, $t0\n    move $a1, $v0\n    li   $a2, %d\n", cur_->prog_scale);
                    call_rt(fn, RT_ARITH);
                }
                return;
            }
            case AST_FUNC_CALL: {
                const std::string &fn = expr->text;
                if (ci_eq(fn, "LENGTH") && !expr->children.empty()) {
                    emit_addr_len(expr->children[0]);
                    emit("    li   $t0, %d\n    mult $v1, $t0\n    mflo $v0\n", cur_->prog_scale);
                } else if (ci_eq(fn, "ABS") && !expr->children.empty()) {
                    emit_eval_num(expr->children[0]);
                    int lbl = new_lbl();
                    emit("    bgez $v0, L%d\n    subu $v0, $zero, $v0\nL%d:\n", lbl, lbl);
                } else if ((ci_eq(fn, "INTEGER") || ci_eq(fn, "INTEGER-PART")) && !expr->children.empty()) {
                    emit_eval_num(expr->children[0]);
                    if (cur_->prog_scale > 1) {
                        emit("    li   $t0, %d\n    div  $v0, $t0\n    mflo $v0\n    mult $v0, $t0\n    mflo $v0\n",
                             cur_->prog_scale);
                    }
                } else if (ci_eq(fn, "MOD") && expr->children.size() >= 2) {
                    emit_eval_num(expr->children[0]);
                    push_reg("$v0");
                    emit_eval_num(expr->children[1]);
                    pop_reg("$t0");
                    int lbl = new_lbl();
                    emit("    beq  $v0, $zero, L%d\n    div  $t0, $v0\n    mfhi $v0\nL%d:\n", lbl, lbl);
                } else if (ci_eq(fn, "ORD") && !expr->children.empty()) {
                    emit_addr_len(expr->children[0]);
                    emit("    lbu  $v0, 0($v0)\n    addiu $v0, $v0, 1\n");
                    if (cur_->prog_scale > 1) emit("    li   $t0, %d\n    mult $v0, $t0\n    mflo $v0\n", cur_->prog_scale);
                } else if ((ci_eq(fn, "NUMVAL") || ci_eq(fn, "NUMVAL-C")) && !expr->children.empty()) {
                    emit_addr_len(expr->children[0], nullptr, "$a0", "$a1");
                    emit("    li   $a2, 0\n    li   $a3, %d\n", cur_->prog_frac);
                    call_rt("_rt_parse_num", RT_NUM);
                } else if (ci_eq(fn, "RANDOM")) {
                    use_rt(RT_RNG);
                    emit("    la   $t0, _rt_rng_seed\n    lw   $t1, 0($t0)\n    li   $t2, 1103515245\n"
                         "    mult $t1, $t2\n    mflo $t1\n    addiu $t1, $t1, 12345\n    sw   $t1, 0($t0)\n"
                         "    srl  $t1, $t1, 16\n    andi $t1, $t1, 0x7FFF\n    li   $t2, %d\n"
                         "    div  $t1, $t2\n    mfhi $v0\n", cur_->prog_scale);
                } else if (!expr->children.empty()) {
                    emit_eval_num(expr->children[0]);
                } else {
                    emit("    li   $v0, 0\n");
                }
                return;
            }
            default:
                emit("    li   $v0, 0\n");
                return;
        }
    }

    void emit_store_num(const ASTNode *target_ref) {
        push_reg("$v0");
        int vi = -1;
        emit_data_ref_addr_len(target_ref, &vi, "$a0", "$a1");
        pop_reg("$a2");
        if (vi < 0) return;

        const VarLayout &v = cur_->vars[vi];
        if (target_ref->ival == 1 && cur_->prog_frac > v.frac_digits) {
            int half = pow10_int(cur_->prog_frac - v.frac_digits) / 2;
            int lneg = new_lbl(), lrd = new_lbl();
            emit("    bltz $a2, L%d\n    addiu $a2, $a2, %d\n    j    L%d\nL%d:\n    addiu $a2, $a2, %d\nL%d:\n",
                 lneg, half, lrd, lneg, -half, lrd);
        }

        if (v.is_edited && v.pic_str_id >= 0) {
            int packed = (cur_->prog_frac << 8) | ((v.frac_digits & 0x7F) << 1) | (cur_->decimal_comma ? 1 : 0);
            emit("    la   $a1, cob_s%d\n    li   $a3, %d\n", v.pic_str_id, packed);
            call_rt("_rt_format_edited", RT_EDITED);
        } else {
            int df = (v.kind == DATA_NUMERIC || v.kind == DATA_INDEX) ? (v.frac_digits & 0x7F) : 0;
            int sg = (v.kind == DATA_NUMERIC || v.kind == DATA_INDEX) ? (v.is_signed ? 1 : 0) : 0;
            int packed = (cur_->prog_frac << 8) | (df << 1) | sg;
            emit("    li   $a3, %d\n", packed);
            call_rt("_rt_format_num", RT_NUM);
        }
    }

    void emit_move_one(const ASTNode *src, const ASTNode *dst_ref) {
        int dvi = cur_->resolve_data_ref(dst_ref);
        const VarLayout *dv = (dvi >= 0) ? &cur_->vars[dvi] : nullptr;

        if (src && src->kind == AST_FIGURATIVE && !src->text.empty()) {
            if ((ci_eq(src->text, "ZERO") || ci_eq(src->text, "ZEROS") || ci_eq(src->text, "ZEROES")) &&
                dv && (dv->kind == DATA_NUMERIC || dv->kind == DATA_NUMERIC_EDITED || dv->kind == DATA_INDEX)) {
                emit("    li   $v0, 0\n");
                emit_store_num(dst_ref);
                return;
            }
            emit_data_ref_addr_len(dst_ref, nullptr, "$a0", "$a2");
            emit("    li   $a1, %d\n", figurative_byte(src->text));
            call_rt("_rt_memset", RT_MEM);
            return;
        }

        if (dv && dv->is_large_uint()) {
            emit_addr_len(src);
            push_pair("$v0", "$v1");
            emit_data_ref_addr_len(dst_ref, nullptr, "$a0", "$a1");
            pop_pair("$a2", "$a3");
            call_rt("_rt_move_digits", RT_BIG_DIGITS);
            return;
        }

        bool has_refmod = false;
        for (size_t i = 0; dst_ref && i < dst_ref->children.size(); i++) {
            if (dst_ref->children[i]->kind == AST_REFMOD) has_refmod = true;
        }
        if (dv && !has_refmod && (dv->kind == DATA_NUMERIC || dv->kind == DATA_NUMERIC_EDITED || dv->kind == DATA_INDEX)) {
            emit_eval_num(src);
            emit_store_num(dst_ref);
            return;
        }

        emit_addr_len(src);
        push_pair("$v0", "$v1");
        emit_data_ref_addr_len(dst_ref, nullptr, "$a0", "$a1");
        pop_pair("$a2", "$a3");
        call_rt("_rt_move_alnum", RT_MEM);
    }

    /* =====================================================================
     * Phase 3: Direct Control-Flow Branching (Dragon Book Ch. 6.6)
     * ===================================================================== */

    bool is_numeric_expr(const ASTNode *n) const {
        if (!n) return false;
        if (n->kind == AST_LITERAL_NUM || n->kind == AST_BINOP || n->kind == AST_UNOP || n->kind == AST_LENGTH_OF)
            return true;
        if (n->kind == AST_FIGURATIVE &&
            (ci_eq(n->text, "ZERO") || ci_eq(n->text, "ZEROS") || ci_eq(n->text, "ZEROES")))
            return true;
        if (n->kind == AST_DATA_REF) {
            for (size_t i = 0; i < n->children.size(); i++) {
                if (n->children[i]->kind == AST_REFMOD) return false;
            }
            int vi = cur_->resolve_data_ref(n);
            return vi >= 0 && (cur_->vars[vi].kind == DATA_NUMERIC || cur_->vars[vi].kind == DATA_INDEX);
        }
        return false;
    }

    bool is_large_numeric_expr(const ASTNode *n) const {
        if (!n || n->kind != AST_DATA_REF) return false;
        int vi = cur_->resolve_data_ref(n);
        return vi >= 0 && cur_->vars[vi].is_large_uint();
    }

    /* Emit a single MIPS branch instruction testing (lhs_reg op rhs_reg) -> target_lbl */
    void emit_branch_relop(const std::string &raw_op, const char *lhs_reg, const char *rhs_reg,
                           int target_lbl, bool jump_if_true) const {
        std::string op = raw_op;
        if (op.size() >= 4 && op.substr(0, 4) == "NOT ") {
            op = op.substr(4);
            jump_if_true = !jump_if_true;
        }
        if (!jump_if_true) {
            if (op == "=") op = "<>";
            else if (op == "<>") op = "=";
            else if (op == "<") op = ">=";
            else if (op == "<=") op = ">";
            else if (op == ">") op = "<=";
            else if (op == ">=") op = "<";
        }
        if (std::strcmp(rhs_reg, "$zero") == 0) {
            if (op == "=")       emit("    beq  %s, $zero, L%d\n", lhs_reg, target_lbl);
            else if (op == "<>") emit("    bne  %s, $zero, L%d\n", lhs_reg, target_lbl);
            else if (op == "<")  emit("    bltz %s, L%d\n", lhs_reg, target_lbl);
            else if (op == "<=") emit("    blez %s, L%d\n", lhs_reg, target_lbl);
            else if (op == ">")  emit("    bgtz %s, L%d\n", lhs_reg, target_lbl);
            else if (op == ">=") emit("    bgez %s, L%d\n", lhs_reg, target_lbl);
            else                 emit("    beq  %s, $zero, L%d\n", lhs_reg, target_lbl);
        } else {
            if (op == "=")       emit("    beq  %s, %s, L%d\n", lhs_reg, rhs_reg, target_lbl);
            else if (op == "<>") emit("    bne  %s, %s, L%d\n", lhs_reg, rhs_reg, target_lbl);
            else if (op == "<")  emit("    blt  %s, %s, L%d\n", lhs_reg, rhs_reg, target_lbl);
            else if (op == "<=") emit("    ble  %s, %s, L%d\n", lhs_reg, rhs_reg, target_lbl);
            else if (op == ">")  emit("    bgt  %s, %s, L%d\n", lhs_reg, rhs_reg, target_lbl);
            else if (op == ">=") emit("    bge  %s, %s, L%d\n", lhs_reg, rhs_reg, target_lbl);
            else                 emit("    beq  %s, %s, L%d\n", lhs_reg, rhs_reg, target_lbl);
        }
    }

    /* Compare lhs op rhs and jump directly to target_lbl (0 extra labels for numeric!) */
    void emit_branch_cmp(const ASTNode *lhs, const ASTNode *rhs, const std::string &op,
                         int target_lbl, bool jump_if_true) {
        if (is_large_numeric_expr(lhs) || is_large_numeric_expr(rhs)) {
            emit_addr_len(lhs);
            push_pair("$v0", "$v1");
            emit_addr_len(rhs, nullptr, "$a2", "$a3");
            pop_pair("$a0", "$a1");
            call_rt("_rt_cmp_digits", RT_CMP_DIGITS);
            emit_branch_relop(op, "$v0", "$zero", target_lbl, jump_if_true);
            return;
        }

        if (rhs && rhs->kind == AST_FIGURATIVE && !rhs->text.empty()) {
            if ((ci_eq(rhs->text, "ZERO") || ci_eq(rhs->text, "ZEROS") || ci_eq(rhs->text, "ZEROES")) &&
                is_numeric_expr(lhs)) {
                emit_eval_num(lhs);
                emit_branch_relop(op, "$v0", "$zero", target_lbl, jump_if_true);
                return;
            }
            int ch = figurative_byte(rhs->text);
            emit_addr_len(lhs, nullptr, "$t0", "$t1");
            int lp = new_lbl(), ldiff = new_lbl(), leq = new_lbl();
            emit("    li   $t2, %d\nL%d:\n    blez $t1, L%d\n    lbu  $t3, 0($t0)\n"
                 "    subu $v0, $t3, $t2\n    bne  $v0, $zero, L%d\n    addiu $t0, $t0, 1\n"
                 "    addiu $t1, $t1, -1\n    j    L%d\nL%d:\n    li   $v0, 0\nL%d:\n",
                 ch, lp, leq, ldiff, lp, leq, ldiff);
            emit_branch_relop(op, "$v0", "$zero", target_lbl, jump_if_true);
            return;
        }

        if (is_numeric_expr(lhs) && is_numeric_expr(rhs)) {
            int32_t rconst = 0;
            if (try_const_num(rhs, rconst)) {
                emit_eval_num(lhs);
                if (rconst == 0) {
                    emit_branch_relop(op, "$v0", "$zero", target_lbl, jump_if_true);
                } else {
                    emit("    li   $t0, %d\n", (int)rconst);
                    emit_branch_relop(op, "$v0", "$t0", target_lbl, jump_if_true);
                }
                return;
            }
            emit_eval_num(lhs);
            push_reg("$v0");
            emit_eval_num(rhs);
            pop_reg("$t0");
            emit_branch_relop(op, "$t0", "$v0", target_lbl, jump_if_true);
            return;
        }

        emit_addr_len(lhs);
        push_pair("$v0", "$v1");
        emit_addr_len(rhs, nullptr, "$a2", "$a3");
        pop_pair("$a0", "$a1");
        call_rt("_rt_cmp_alnum", RT_CMP_ALNUM);
        emit_branch_relop(op, "$v0", "$zero", target_lbl, jump_if_true);
    }

    void emit_branch_class(const ASTNode *subject, const std::string &cls, bool negated,
                           int target_lbl, bool jump_if_true) {
        if (negated) jump_if_true = !jump_if_true;
        if (ci_eq(cls, "NUMERIC")) {
            emit_addr_len(subject, nullptr, "$a0", "$a1");
            call_rt("_rt_is_numeric", RT_CLASS);
            emit("    %s  $v0, $zero, L%d\n", jump_if_true ? "bne" : "beq", target_lbl);
        } else if (ci_eq(cls, "ALPHABETIC")) {
            emit_addr_len(subject, nullptr, "$a0", "$a1");
            call_rt("_rt_is_alpha", RT_CLASS);
            emit("    %s  $v0, $zero, L%d\n", jump_if_true ? "bne" : "beq", target_lbl);
        } else if (ci_eq(cls, "POSITIVE")) {
            emit_eval_num(subject);
            emit_branch_relop(">", "$v0", "$zero", target_lbl, jump_if_true);
        } else if (ci_eq(cls, "NEGATIVE")) {
            emit_eval_num(subject);
            emit_branch_relop("<", "$v0", "$zero", target_lbl, jump_if_true);
        } else if (jump_if_true) {
            emit("    j    L%d\n", target_lbl);
        }
    }

    void emit_branch_level88(int vi, int target_lbl, bool jump_if_true) {
        const VarLayout &v = cur_->vars[vi];
        if (v.parent_idx < 0 || !v.val_ast || v.val_ast->children.empty()) {
            if (!jump_if_true) emit("    j    L%d\n", target_lbl);
            return;
        }
        ASTNode pref = make_ref(cur_->vars[v.parent_idx].name);
        int l_hit = jump_if_true ? target_lbl : new_lbl();
        for (size_t i = 0; i < v.val_ast->children.size(); i++) {
            const ASTNode *item = v.val_ast->children[i];
            if (item->kind == AST_BINOP && ci_eq(item->text, "THRU") && item->children.size() >= 2) {
                int l_next = new_lbl();
                emit_branch_cmp(&pref, item->children[0], "<", l_next, true);
                emit_branch_cmp(&pref, item->children[1], "<=", l_hit, true);
                emit("L%d:\n", l_next);
            } else {
                emit_branch_cmp(&pref, item, "=", l_hit, true);
            }
        }
        if (!jump_if_true) {
            emit("    j    L%d\nL%d:\n", target_lbl, l_hit);
        }
    }

    /* Dragon Book Ch. 6.6: jump to target_lbl if cond evaluates to jump_if_true */
    void emit_branch_cond(const ASTNode *cond, CondCtx *cctx, int target_lbl, bool jump_if_true) {
        if (!cond) {
            if (!jump_if_true) emit("    j    L%d\n", target_lbl);
            return;
        }
        switch (cond->kind) {
            case AST_COND_NOT:
                if (!cond->children.empty()) emit_branch_cond(cond->children[0], cctx, target_lbl, !jump_if_true);
                return;
            case AST_COND_LOGIC: {
                if (cond->children.size() < 2) return;
                bool is_and = ci_eq(cond->text, "AND");
                if (is_and && !jump_if_true) {
                    /* !(A && B): jump to target_lbl if A is false OR B is false */
                    emit_branch_cond(cond->children[0], cctx, target_lbl, false);
                    emit_branch_cond(cond->children[1], cctx, target_lbl, false);
                } else if (!is_and && jump_if_true) {
                    /* (A || B): jump to target_lbl if A is true OR B is true */
                    emit_branch_cond(cond->children[0], cctx, target_lbl, true);
                    emit_branch_cond(cond->children[1], cctx, target_lbl, true);
                } else if (is_and && jump_if_true) {
                    int l_skip = new_lbl();
                    emit_branch_cond(cond->children[0], cctx, l_skip, false);
                    emit_branch_cond(cond->children[1], cctx, target_lbl, true);
                    emit("L%d:\n", l_skip);
                } else {
                    int l_skip = new_lbl();
                    emit_branch_cond(cond->children[0], cctx, l_skip, true);
                    emit_branch_cond(cond->children[1], cctx, target_lbl, false);
                    emit("L%d:\n", l_skip);
                }
                return;
            }
            case AST_COND_REL: {
                std::string op = cond->text.empty() ? "=" : cond->text;
                if (cond->children.size() >= 2) {
                    if (cctx) { cctx->last_lhs = cond->children[0]; cctx->last_op = op; }
                    emit_branch_cmp(cond->children[0], cond->children[1], op, target_lbl, jump_if_true);
                } else if (cond->children.size() == 1 && cctx && cctx->last_lhs) {
                    cctx->last_op = op;
                    emit_branch_cmp(cctx->last_lhs, cond->children[0], op, target_lbl, jump_if_true);
                }
                return;
            }
            case AST_COND_CLASS:
                if (!cond->children.empty()) {
                    emit_branch_class(cond->children[0], cond->text, ci_eq(cond->extra, "NOT"),
                                      target_lbl, jump_if_true);
                }
                return;
            case AST_DATA_REF: {
                int vi = cur_->resolve_data_ref(cond);
                if (vi >= 0 && cur_->vars[vi].kind == DATA_COND) {
                    emit_branch_level88(vi, target_lbl, jump_if_true);
                    return;
                }
                if (cctx && cctx->last_lhs && !cctx->last_op.empty()) {
                    emit_branch_cmp(cctx->last_lhs, cond, cctx->last_op, target_lbl, jump_if_true);
                    return;
                }
                emit_eval_num(cond);
                emit_branch_relop("<>", "$v0", "$zero", target_lbl, jump_if_true);
                return;
            }
            default:
                if (cctx && cctx->last_lhs && !cctx->last_op.empty()) {
                    emit_branch_cmp(cctx->last_lhs, cond, cctx->last_op, target_lbl, jump_if_true);
                    return;
                }
                bool is_true = (cond->kind == AST_FIGURATIVE && ci_eq(cond->text, "TRUE"));
                if (is_true == jump_if_true) emit("    j    L%d\n", target_lbl);
                return;
        }
    }

    /* =====================================================================
     * Phase 4: Statement Code Generation
     * ===================================================================== */

    void emit_set_file_status(int fi, const std::string &code2) {
        if (fi < 0 || fi >= (int)cur_->files.size()) return;
        int svi = cur_->files[fi].status_var_idx;
        if (svi < 0) return;

        int sid = strs_.add(code2, false);
        ASTNode sref = make_ref(cur_->vars[svi].name);
        emit_data_ref_addr_len(&sref, nullptr, "$a0", "$a1");
        emit("    la   $a2, cob_s%d\n    li   $a3, 2\n", sid);
        call_rt("_rt_move_alnum", RT_MEM);
    }

    void emit_perform_call_target(const std::string &target_name, const std::string &thru_name) {
        int b_start = cur_->find_block(target_name);
        if (b_start < 0) return;

        int b_end = b_start;
        if (!thru_name.empty()) {
            int bt = cur_->find_block(thru_name);
            if (bt >= 0) b_end = cur_->blocks[bt].is_section ? cur_->blocks[bt].sec_end_block : bt;
        } else if (cur_->blocks[b_start].is_section) {
            b_end = cur_->blocks[b_start].sec_end_block;
        }

        int ret_lbl = new_lbl();
        emit("    li   $a0, %d\n    la   $a1, L%d\n", cur_->blocks[b_end].global_id, ret_lbl);
        call_rt("_rt_perf_push", RT_PERF);
        emit("    j    p%d_b%d\nL%d:\n", cur_->prog_id, b_start, ret_lbl);
    }

    void emit_perform_body(const ASTNode *st, bool is_inline,
                           const std::string &target_name, const std::string &thru_name,
                           size_t inline_start) {
        if (is_inline) emit_stmt_list(st, inline_start);
        else emit_perform_call_target(target_name, thru_name);
    }

    void emit_perform_varying_rec(const ASTNode *st, const ASTNode *ctrl, size_t after_idx,
                                  bool is_inline, const std::string &target_name,
                                  const std::string &thru_name, size_t inline_start) {
        const ASTNode *src_node = (after_idx == 0) ? ctrl : ctrl->children[3 + after_idx];
        if (!src_node || src_node->children.size() < 4) return;
        const ASTNode *var_ref = src_node->children[0];
        const ASTNode *from_rv = src_node->children[1];
        const ASTNode *by_rv = src_node->children[2];
        const ASTNode *until_cond = src_node->children[3];

        emit_move_one(from_rv, var_ref);
        int l_loop = new_lbl(), l_end = new_lbl();
        emit("L%d:\n", l_loop);
        CondCtx cc;
        emit_branch_cond(until_cond, &cc, l_end, true);

        if (4 + after_idx < ctrl->children.size()) {
            emit_perform_varying_rec(st, ctrl, after_idx + 1, is_inline, target_name, thru_name, inline_start);
        } else {
            emit_perform_body(st, is_inline, target_name, thru_name, inline_start);
        }

        int32_t by_k = 0;
        if (try_const_num(by_rv, by_k) && by_k >= -32768 && by_k <= 32767) {
            emit_eval_num(var_ref);
            emit("    addiu $v0, $v0, %d\n", (int)by_k);
        } else {
            emit_eval_num(var_ref);
            push_reg("$v0");
            emit_eval_num(by_rv);
            pop_reg("$t0");
            emit("    addu $v0, $t0, $v0\n");
        }
        emit_store_num(var_ref);
        emit("    j    L%d\nL%d:\n", l_loop, l_end);
    }

    void emit_stmt_list(const ASTNode *parent, size_t start_idx = 0) {
        if (!parent) return;
        for (size_t i = start_idx; i < parent->children.size(); i++) emit_stmt(parent->children[i]);
    }

    void emit_stmt_display(const ASTNode *st) {
        for (size_t i = 0; i < st->children.size(); i++) {
            const ASTNode *item = st->children[i];
            if (!item || item->kind == AST_CLAUSE) continue;
            int vi = -1;
            if (item->kind == AST_DATA_REF) {
                emit_data_ref_addr_len(item, &vi, "$a0", "$a1");
                if (vi >= 0 && cur_->vars[vi].kind == DATA_NUMERIC && cur_->vars[vi].frac_digits > 0) {
                    emit("    li   $a2, %d\n    li   $a3, %d\n",
                         cur_->vars[vi].frac_digits, cur_->decimal_comma ? 1 : 0);
                    call_rt("_rt_print_num_field", RT_PRINT_NUM);
                    continue;
                }
            } else {
                emit_addr_len(item, nullptr, "$a0", "$a1");
            }
            call_rt("_rt_print_bytes", RT_PRINT);
        }
        if (st->ival == 0) call_rt("_rt_print_nl", RT_PRINT);
    }

    void emit_stmt_accept(const ASTNode *st) {
        if (st->children.empty()) return;
        const ASTNode *dst = st->children[0];
        int vi = -1;
        emit_data_ref_addr_len(dst, &vi, "$a0", "$a1");

        if (!st->text.empty()) {
            int mode = 0;
            if (ci_eq(st->text, "DATE")) mode = ci_eq(st->extra, "YYYYMMDD") ? 0 : 1;
            else if (ci_eq(st->text, "DAY")) mode = 2;
            else if (ci_eq(st->text, "DAY-OF-WEEK")) mode = 3;
            else if (ci_eq(st->text, "TIME")) mode = 4;
            emit("    li   $a2, %d\n", mode);
            call_rt("_rt_accept_datetime", RT_DATETIME);
            return;
        }
        if (vi >= 0 && cur_->vars[vi].is_large_uint()) {
            call_rt("_rt_accept_digits", RT_BIG_DIGITS);
        } else if (vi >= 0 && (cur_->vars[vi].kind == DATA_NUMERIC || cur_->vars[vi].kind == DATA_INDEX)) {
            int packed = (cur_->prog_frac << 1) | (cur_->vars[vi].is_signed ? 1 : 0);
            emit("    li   $a2, %d\n    li   $a3, %d\n", cur_->vars[vi].frac_digits, packed);
            call_rt("_rt_accept_num", RT_ACCEPT);
        } else {
            call_rt("_rt_accept_str", RT_ACCEPT);
        }
    }

    void emit_stmt_add_sub(const ASTNode *st, bool is_sub) {
        if (st->children.size() < 2) return;
        const ASTNode *lhs = st->children[0];
        const ASTNode *rhs = st->children[1];
        const ASTNode *giving = (st->children.size() >= 3 && st->children[2] && st->children[2]->is_clause("GIVING"))
                                    ? st->children[2] : nullptr;

        /* Fast path: ADD/SUBTRACT <const> TO/FROM <var> (e.g., ADD 1 TO WRK-IND1) */
        int32_t kval = 0;
        if (!giving && !ci_eq(st->text, "GIVING") &&
            lhs->kind == AST_LIST && lhs->children.size() == 1 &&
            rhs->kind == AST_LIST && try_const_num(lhs->children[0], kval)) {
            int32_t delta = is_sub ? -kval : kval;
            for (size_t i = 0; i < rhs->children.size(); i++) {
                emit_eval_num(rhs->children[i]);
                if (delta >= -32768 && delta <= 32767) emit("    addiu $v0, $v0, %d\n", (int)delta);
                else emit("    li   $t0, %d\n    addu $v0, $v0, $t0\n", (int)delta);
                emit_store_num(rhs->children[i]);
            }
            return;
        }

        emit("    addiu $sp, $sp, -4\n    sw   $zero, 0($sp)\n");
        if (lhs->kind == AST_LIST) {
            for (size_t i = 0; i < lhs->children.size(); i++) {
                emit_eval_num(lhs->children[i]);
                emit("    lw   $t0, 0($sp)\n    addu $t0, $t0, $v0\n    sw   $t0, 0($sp)\n");
            }
        } else {
            emit_eval_num(lhs);
            emit("    sw   $v0, 0($sp)\n");
        }

        const char *acc_op = is_sub ? "subu" : "addu";
        if (!is_sub && ci_eq(st->text, "GIVING")) {
            if (rhs->kind == AST_LIST) {
                for (size_t i = 0; i < rhs->children.size(); i++) {
                    emit("    lw   $v0, 0($sp)\n");
                    emit_store_num(rhs->children[i]);
                }
            }
        } else if (giving) {
            if (rhs->kind == AST_LIST && !rhs->children.empty()) {
                emit_eval_num(rhs->children[0]);
                emit("    lw   $t0, 0($sp)\n    %s $t0, $v0, $t0\n    sw   $t0, 0($sp)\n", acc_op);
            }
            for (size_t i = 0; i < giving->children.size(); i++) {
                emit("    lw   $v0, 0($sp)\n");
                emit_store_num(giving->children[i]);
            }
        } else if (rhs->kind == AST_LIST) {
            for (size_t i = 0; i < rhs->children.size(); i++) {
                emit_eval_num(rhs->children[i]);
                emit("    lw   $t0, 0($sp)\n    %s $v0, $v0, $t0\n", acc_op);
                emit_store_num(rhs->children[i]);
            }
        }
        emit("    addiu $sp, $sp, 4\n");
    }

    void emit_stmt_multiply(const ASTNode *st) {
        if (st->children.size() < 2) return;
        const ASTNode *lhs = st->children[0], *rhs = st->children[1];
        const ASTNode *giving = (st->children.size() >= 3 && st->children[2] && st->children[2]->is_clause("GIVING"))
                                    ? st->children[2] : nullptr;
        emit_eval_num(lhs);
        push_reg("$v0");
        if (giving) {
            if (rhs->kind == AST_LIST && !rhs->children.empty()) {
                emit_eval_num(rhs->children[0]);
                emit("    lw   $a0, 0($sp)\n    move $a1, $v0\n    li   $a2, %d\n", cur_->prog_scale);
                call_rt("_rt_mul_scaled", RT_ARITH);
                emit("    sw   $v0, 0($sp)\n");
            }
            for (size_t i = 0; i < giving->children.size(); i++) {
                emit("    lw   $v0, 0($sp)\n");
                emit_store_num(giving->children[i]);
            }
        } else if (rhs->kind == AST_LIST) {
            for (size_t i = 0; i < rhs->children.size(); i++) {
                emit_eval_num(rhs->children[i]);
                emit("    lw   $a0, 0($sp)\n    move $a1, $v0\n    li   $a2, %d\n", cur_->prog_scale);
                call_rt("_rt_mul_scaled", RT_ARITH);
                emit_store_num(rhs->children[i]);
            }
        }
        emit("    addiu $sp, $sp, 4\n");
    }

    void emit_stmt_divide(const ASTNode *st) {
        if (st->children.size() < 2) return;
        bool is_by = ci_eq(st->text, "BY");
        const ASTNode *op1 = st->children[0], *rhs = st->children[1];
        const ASTNode *giving = st->find_clause("GIVING", 2);
        const ASTNode *rem = st->find_clause("REMAINDER", 2);
        const ASTNode *sz_err = st->find_clause("ON-SIZE-ERROR", 2);

        emit_eval_num(op1);
        emit("    addiu $sp, $sp, -8\n    sw   $v0, 0($sp)\n");
        if (giving || is_by) {
            if (rhs->kind == AST_LIST && !rhs->children.empty()) {
                emit_eval_num(rhs->children[0]);
                emit("    lw   $t0, 0($sp)\n");
                if (is_by) emit("    move $a0, $t0\n    move $a1, $v0\n");
                else       emit("    move $a0, $v0\n    move $a1, $t0\n");
                int lz = new_lbl(), ldn = new_lbl();
                emit("    beq  $a1, $zero, L%d\n    sw   $a0, 0($sp)\n    sw   $a1, 4($sp)\n"
                     "    li   $a2, %d\n", lz, cur_->prog_scale);
                call_rt("_rt_div_scaled", RT_ARITH);
                if (giving) {
                    for (size_t i = 0; i < giving->children.size(); i++) {
                        push_reg("$v0");
                        emit_store_num(giving->children[i]);
                        pop_reg("$v0");
                    }
                }
                if (rem && !rem->children.empty()) {
                    emit("    lw   $t0, 0($sp)\n    lw   $t1, 4($sp)\n    div  $t0, $t1\n    mfhi $v0\n");
                    emit_store_num(rem->children[0]);
                }
                emit("    j    L%d\nL%d:\n", ldn, lz);
                if (sz_err) {
                    for (size_t i = 0; i < sz_err->children.size(); i++) {
                        if (sz_err->children[i]->kind != AST_CLAUSE) emit_stmt(sz_err->children[i]);
                    }
                }
                emit("L%d:\n", ldn);
            }
        } else if (rhs->kind == AST_LIST) {
            for (size_t i = 0; i < rhs->children.size(); i++) {
                emit_eval_num(rhs->children[i]);
                emit("    move $a0, $v0\n    lw   $a1, 0($sp)\n    li   $a2, %d\n", cur_->prog_scale);
                call_rt("_rt_div_scaled", RT_ARITH);
                emit_store_num(rhs->children[i]);
            }
        }
        emit("    addiu $sp, $sp, 8\n");
    }

    void emit_stmt_compute(const ASTNode *st) {
        if (st->children.size() < 2) return;
        const ASTNode *targets = st->children[0];
        emit_eval_num(st->children[1]);
        if (targets->kind == AST_LIST && targets->children.size() == 1) {
            emit_store_num(targets->children[0]);
            return;
        }
        push_reg("$v0");
        if (targets->kind == AST_LIST) {
            for (size_t i = 0; i < targets->children.size(); i++) {
                emit("    lw   $v0, 0($sp)\n");
                emit_store_num(targets->children[i]);
            }
        } else {
            emit_store_num(targets);
        }
        emit("    addiu $sp, $sp, 4\n");
    }

    void emit_stmt_if(const ASTNode *st) {
        if (st->children.size() < 2) return;
        const ASTNode *else_cl = (st->children.size() >= 3) ? st->children[2] : nullptr;
        int l_else = new_lbl(), l_end = new_lbl();
        CondCtx cc;
        emit_branch_cond(st->children[0], &cc, else_cl ? l_else : l_end, false);
        emit_stmt_list(st->children[1]);
        if (else_cl) {
            emit("    j    L%d\nL%d:\n", l_end, l_else);
            emit_stmt_list(else_cl);
        }
        emit("L%d:\n", l_end);
    }

    void emit_stmt_evaluate(const ASTNode *st) {
        if (st->children.size() < 2) return;
        const ASTNode *subj_list = st->children[0];
        int l_end = new_lbl();
        for (size_t wi = 1; wi < st->children.size(); wi++) {
            const ASTNode *w = st->children[wi];
            if (!w || w->kind != AST_WHEN) continue;
            if (ci_eq(w->text, "OTHER")) {
                emit_stmt_list(w);
                emit("    j    L%d\n", l_end);
                continue;
            }
            if (w->children.empty()) continue;
            const ASTNode *objs = w->children[0];
            int l_next = new_lbl();
            size_t n = subj_list->children.size() < objs->children.size()
                           ? subj_list->children.size() : objs->children.size();
            for (size_t k = 0; k < n; k++) {
                const ASTNode *subj = subj_list->children[k], *obj = objs->children[k];
                if (obj->kind == AST_FIGURATIVE && ci_eq(obj->text, "ANY")) continue;
                if (subj->kind == AST_FIGURATIVE && (ci_eq(subj->text, "TRUE") || ci_eq(subj->text, "FALSE"))) {
                    bool want = ci_eq(subj->text, "TRUE");
                    CondCtx cc;
                    emit_branch_cond(obj, &cc, l_next, !want);
                } else if (obj->kind == AST_COND_REL && obj->children.size() == 1) {
                    emit_branch_cmp(subj, obj->children[0], obj->text.empty() ? "=" : obj->text, l_next, false);
                } else if (obj->kind == AST_COND_CLASS) {
                    emit_branch_class(subj, obj->text, ci_eq(obj->extra, "NOT"), l_next, false);
                } else if (obj->kind == AST_BINOP && ci_eq(obj->text, "THRU") && obj->children.size() >= 2) {
                    emit_branch_cmp(subj, obj->children[0], "<", l_next, true);
                    emit_branch_cmp(subj, obj->children[1], ">", l_next, true);
                } else {
                    emit_branch_cmp(subj, obj, "=", l_next, false);
                }
            }
            emit_stmt_list(w, 1);
            emit("    j    L%d\nL%d:\n", l_end, l_next);
        }
        emit("L%d:\n", l_end);
    }

    void emit_stmt_perform(const ASTNode *st) {
        bool is_inline = ci_eq(st->text, "INLINE");
        std::string target = is_inline ? "" : st->text;
        std::string thru;
        const ASTNode *ctrl = nullptr;
        size_t inline_start = 0;

        for (size_t i = 0; i < st->children.size(); i++) {
            const ASTNode *c = st->children[i];
            if (c && c->is_clause("THRU")) {
                thru = c->extra;
            } else if (c && (c->is_clause("TIMES") || c->is_clause("UNTIL") || c->is_clause("VARYING"))) {
                ctrl = c;
                if (is_inline) inline_start = i + 1;
            }
        }

        if (!ctrl) {
            emit_perform_body(st, is_inline, target, thru, inline_start);
        } else if (ctrl->is_clause("TIMES") && !ctrl->children.empty()) {
            emit_eval_num(ctrl->children[0]);
            unscale_v0();
            push_reg("$v0");
            int lp = new_lbl(), lend = new_lbl();
            emit("L%d:\n    lw   $t0, 0($sp)\n    blez $t0, L%d\n    addiu $t0, $t0, -1\n    sw   $t0, 0($sp)\n",
                 lp, lend);
            emit_perform_body(st, is_inline, target, thru, inline_start);
            emit("    j    L%d\nL%d:\n    addiu $sp, $sp, 4\n", lp, lend);
        } else if (ctrl->is_clause("UNTIL") && !ctrl->children.empty()) {
            int lp = new_lbl(), lend = new_lbl();
            emit("L%d:\n", lp);
            CondCtx cc;
            emit_branch_cond(ctrl->children[0], &cc, lend, true);
            emit_perform_body(st, is_inline, target, thru, inline_start);
            emit("    j    L%d\nL%d:\n", lp, lend);
        } else if (ctrl->is_clause("VARYING")) {
            emit_perform_varying_rec(st, ctrl, 0, is_inline, target, thru, inline_start);
        }
    }

    void emit_stmt_open(const ASTNode *st) {
        for (size_t g = 0; g < st->children.size(); g++) {
            const ASTNode *grp = st->children[g];
            if (!grp || grp->kind != AST_CLAUSE) continue;
            int mode = 0;
            if (ci_eq(grp->text, "OUTPUT")) mode = 1;
            else if (ci_eq(grp->text, "I-O")) mode = 2;
            else if (ci_eq(grp->text, "EXTEND")) mode = 3;

            for (size_t i = 0; i < grp->children.size(); i++) {
                const ASTNode *fref = grp->children[i];
                if (!fref || fref->text.empty()) continue;
                int fi = cur_->find_file(fref->text);
                if (fi < 0) continue;
                const FileLayout &fl = cur_->files[fi];
                if (fl.assign_var_idx >= 0) {
                    ASTNode vref = make_ref(cur_->vars[fl.assign_var_idx].name);
                    emit_data_ref_addr_len(&vref, nullptr, "$a1", "$a2");
                    emit("    la   $a0, _rt_linebuf\n");
                    call_rt("_rt_trim_zstr", RT_LINE_IO);
                    emit("    la   $a0, _rt_linebuf\n");
                } else {
                    emit("    la   $a0, cob_s%d\n", fl.path_str_id);
                }
                emit("    li   $a1, %d\n", mode);
                call_rt("_rt_open_file", RT_OPEN_CLOSE);
                emit("    la   $t0, p%d_fd%d\n    sw   $v0, 0($t0)\n", cur_->prog_id, fi);
                if (fl.status_var_idx >= 0) {
                    int lerr = new_lbl(), ldn = new_lbl();
                    emit("    bltz $v0, L%d\n", lerr);
                    emit_set_file_status(fi, "00");
                    emit("    j    L%d\nL%d:\n", ldn, lerr);
                    emit_set_file_status(fi, "35");
                    emit("L%d:\n", ldn);
                }
            }
        }
    }

    void emit_stmt_close(const ASTNode *st) {
        for (size_t i = 0; i < st->children.size(); i++) {
            const ASTNode *fref = st->children[i];
            if (!fref || fref->text.empty()) continue;
            int fi = cur_->find_file(fref->text);
            if (fi < 0) continue;
            emit("    la   $t0, p%d_fd%d\n    lw   $a0, 0($t0)\n", cur_->prog_id, fi);
            call_rt("_rt_close_file", RT_OPEN_CLOSE);
            emit("    la   $t0, p%d_fd%d\n    li   $t1, -1\n    sw   $t1, 0($t0)\n", cur_->prog_id, fi);
            emit_set_file_status(fi, "00");
        }
    }

    void emit_stmt_read(const ASTNode *st) {
        int fi = cur_->find_file(st->text);
        if (fi < 0) return;
        const ASTNode *into_cl = st->find_clause("INTO", 1);
        const ASTNode *at_end = st->find_clause("AT-END", 1);
        if (!at_end) at_end = st->find_clause("INVALID-KEY", 1);
        const ASTNode *not_at_end = st->find_clause("NOT-AT-END", 1);
        if (!not_at_end) not_at_end = st->find_clause("NOT-INVALID-KEY", 1);
        if (at_end && !not_at_end) {
            not_at_end = at_end->find_clause("NOT-AT-END");
            if (!not_at_end) not_at_end = at_end->find_clause("NOT-INVALID-KEY");
        }

        int rvi = cur_->files[fi].rec_var_idx;
        emit("    la   $t0, p%d_fd%d\n    lw   $a0, 0($t0)\n", cur_->prog_id, fi);
        if (rvi >= 0) {
            emit("    la   $a1, p%d_d%d\n    li   $a2, %d\n", cur_->prog_id, rvi, cur_->vars[rvi].total_size);
        } else {
            emit("    la   $a1, _rt_linebuf\n    li   $a2, 256\n");
        }
        call_rt("_rt_read_line_fd", RT_LINE_IO);

        int leof = new_lbl(), ldn = new_lbl();
        emit("    beq  $v0, $zero, L%d\n", leof);
        emit_set_file_status(fi, "00");
        if (into_cl && !into_cl->children.empty() && rvi >= 0) {
            ASTNode rref = make_ref(cur_->vars[rvi].name);
            emit_move_one(&rref, into_cl->children[0]);
        }
        if (not_at_end) {
            for (size_t k = 0; k < not_at_end->children.size(); k++) {
                if (not_at_end->children[k]->kind != AST_CLAUSE) emit_stmt(not_at_end->children[k]);
            }
        }
        emit("    j    L%d\nL%d:\n", ldn, leof);
        emit_set_file_status(fi, "10");
        if (at_end) {
            for (size_t k = 0; k < at_end->children.size(); k++) {
                if (at_end->children[k]->kind != AST_CLAUSE) emit_stmt(at_end->children[k]);
            }
        }
        emit("L%d:\n", ldn);
    }

    void emit_stmt_write(const ASTNode *st) {
        if (st->children.empty()) return;
        const ASTNode *rref = st->children[0];
        int rvi = cur_->resolve_data_ref(rref);
        if (rvi < 0) return;
        int fi = cur_->vars[rvi].fd_idx;
        if (const ASTNode *from_cl = st->find_clause("FROM", 1)) {
            if (!from_cl->children.empty()) emit_move_one(from_cl->children[0], rref);
        }
        if (fi >= 0) {
            emit_data_ref_addr_len(rref, nullptr, "$a1", "$a2");
            emit("    la   $t0, p%d_fd%d\n    lw   $a0, 0($t0)\n", cur_->prog_id, fi);
            call_rt("_rt_write_line_fd", RT_LINE_IO);
            emit_set_file_status(fi, "00");
        }
    }

    void emit_stmt_call(const ASTNode *st) {
        if (st->children.empty()) return;
        const ASTNode *tgt = st->children[0];
        if (!tgt || tgt->kind != AST_LITERAL_STR || tgt->text.empty()) return;
        std::string subname = unquote_cobol(tgt->text);

        int sub_idx = -1;
        for (size_t i = 0; i < progs_.size(); i++) {
            if (ci_eq(progs_[i]->prog_name, subname)) { sub_idx = (int)i; break; }
        }
        if (sub_idx < 0) {
            emit("    # CALL '%s' (external subprogram not found, skipped)\n", subname.c_str());
            return;
        }
        if (const ASTNode *using_cl = st->find_clause("USING", 1)) {
            int nargs = (int)using_cl->children.size();
            if (nargs > 4) nargs = 4;
            if (nargs > 0) {
                emit("    addiu $sp, $sp, %d\n", -(nargs * 4));
                for (int a = 0; a < nargs; a++) {
                    emit_addr_len(using_cl->children[a]);
                    emit("    sw   $v0, %d($sp)\n", a * 4);
                }
                for (int a = 0; a < nargs; a++) emit("    lw   $a%d, %d($sp)\n", a, a * 4);
                emit("    addiu $sp, $sp, %d\n", nargs * 4);
            }
        }
        emit("    jal  cob_sub_%s\n", progs_[sub_idx]->san_name.c_str());
    }

    void emit_init_leaf(const ASTNode *ref, const VarLayout &v,
                        const ASTNode *repl_num, const ASTNode *repl_alnum) {
        if (v.kind == DATA_NUMERIC || v.kind == DATA_NUMERIC_EDITED) {
            if (repl_num) emit_move_one(repl_num, ref);
            else { emit("    li   $v0, 0\n"); emit_store_num(ref); }
        } else if (repl_alnum) {
            emit_move_one(repl_alnum, ref);
        } else {
            emit_data_ref_addr_len(ref, nullptr, "$a0", "$a2");
            emit("    li   $a1, 32\n");
            call_rt("_rt_memset", RT_MEM);
        }
    }

    void emit_stmt_initialize(const ASTNode *st) {
        const ASTNode *repl_num = nullptr, *repl_alnum = nullptr;
        for (size_t i = 0; i < st->children.size(); i++) {
            const ASTNode *c = st->children[i];
            if (c->is_clause("REPLACING") && !c->children.empty()) {
                if (ci_eq(c->extra, "NUMERIC")) repl_num = c->children[0];
                else repl_alnum = c->children[0];
            }
        }
        for (size_t i = 0; i < st->children.size(); i++) {
            const ASTNode *t = st->children[i];
            if (t->kind != AST_DATA_REF) continue;
            int vi = cur_->resolve_data_ref(t);
            if (vi < 0) continue;
            if (cur_->vars[vi].kind != DATA_GROUP) {
                emit_init_leaf(t, cur_->vars[vi], repl_num, repl_alnum);
                continue;
            }
            for (size_t j = (size_t)vi + 1; j < cur_->vars.size(); j++) {
                int p = cur_->vars[j].parent_idx;
                bool is_desc = false;
                while (p >= 0) {
                    if (p == vi) { is_desc = true; break; }
                    p = cur_->vars[p].parent_idx;
                }
                if (!is_desc) break;
                const VarLayout &dv = cur_->vars[j];
                if (dv.kind == DATA_GROUP || dv.kind == DATA_COND || dv.is_redefines) continue;
                if (dv.occurs_depth > cur_->vars[vi].occurs_depth) continue;
                ASTNode cref = make_ref(dv.name);
                emit_init_leaf(&cref, dv, repl_num, repl_alnum);
            }
        }
    }

    void emit_stmt_inspect(const ASTNode *st) {
        if (st->children.size() < 2) return;
        emit_addr_len(st->children[0]);
        push_pair("$v0", "$v1");

        for (size_t ci = 1; ci < st->children.size(); ci++) {
            const ASTNode *cl = st->children[ci];
            if (!cl || cl->kind != AST_CLAUSE) continue;

            if (ci_eq(cl->text, "TALLYING")) {
                for (size_t ti = 0; ti < cl->children.size(); ti++) {
                    const ASTNode *titem = cl->children[ti];
                    if (!titem || titem->children.size() < 2) continue;
                    const ASTNode *cnt_ref = titem->children[0];
                    for (size_t fi = 1; fi < titem->children.size(); fi++) {
                        const ASTNode *f = titem->children[fi];
                        int mode = f->is_clause("FOR-ALL") ? 1 : (f->is_clause("FOR-LEADING") ? 2 : 0);
                        if (mode == 0) {
                            emit("    lw   $a0, 0($sp)\n    lw   $a1, 4($sp)\n    move $a2, $zero\n    li   $a3, 0\n");
                            call_rt("_rt_inspect_tally", RT_INSPECT);
                        } else if (!f->children.empty()) {
                            emit_addr_len(f->children[0]);
                            emit("    lw   $a0, 0($sp)\n    lw   $a1, 4($sp)\n    move $a2, $v0\n"
                                 "    li   $t0, %d\n    or   $a3, $t0, $v1\n", mode << 16);
                            call_rt("_rt_inspect_tally", RT_INSPECT);
                        }
                        if (cur_->prog_scale > 1) {
                            emit("    li   $t0, %d\n    mult $v0, $t0\n    mflo $v0\n", cur_->prog_scale);
                        }
                        push_reg("$v0");
                        emit_eval_num(cnt_ref);
                        pop_reg("$t0");
                        emit("    addu $v0, $v0, $t0\n");
                        emit_store_num(cnt_ref);
                    }
                }
            } else if (ci_eq(cl->text, "REPLACING")) {
                for (size_t ri = 0; ri < cl->children.size(); ri++) {
                    const ASTNode *r = cl->children[ri];
                    if (!r) continue;
                    int mode = ci_eq(r->text, "REPL-ALL") ? 1 :
                               ci_eq(r->text, "REPL-LEADING") ? 2 :
                               ci_eq(r->text, "REPL-FIRST") ? 3 : 0;
                    if (mode == 0 && !r->children.empty()) {
                        emit_addr_len(r->children[0], nullptr, "$a3", "$v1");
                        emit("    addiu $sp, $sp, -4\n    sw   $zero, 0($sp)\n"
                             "    lw   $a0, 4($sp)\n    lw   $a1, 8($sp)\n    move $a2, $zero\n");
                        call_rt("_rt_inspect_replace", RT_INSPECT);
                        emit("    addiu $sp, $sp, 4\n");
                    } else if (r->children.size() >= 2) {
                        emit_addr_len(r->children[0]);
                        emit("    addiu $sp, $sp, -12\n    sw   $v0, 4($sp)\n    li   $t0, %d\n"
                             "    or   $t0, $t0, $v1\n    sw   $t0, 0($sp)\n", mode << 16);
                        emit_addr_len(r->children[1], nullptr, "$a3", "$v1");
                        emit("    lw   $a0, 12($sp)\n    lw   $a1, 16($sp)\n    lw   $a2, 4($sp)\n");
                        call_rt("_rt_inspect_replace", RT_INSPECT);
                        emit("    addiu $sp, $sp, 12\n");
                    }
                }
            } else if (ci_eq(cl->text, "CONVERTING") && cl->children.size() >= 2) {
                emit_addr_len(cl->children[0]);
                push_pair("$v1", "$v0");
                emit_addr_len(cl->children[1], nullptr, "$a3", "$v1");
                emit("    lw   $a0, 8($sp)\n    lw   $a1, 12($sp)\n    lw   $a2, 4($sp)\n");
                call_rt("_rt_inspect_convert", RT_INSPECT);
                emit("    addiu $sp, $sp, 8\n");
            }
        }
        emit("    addiu $sp, $sp, 8\n");
    }

    void emit_stmt_string(const ASTNode *st) {
        if (st->children.size() < 2) return;
        const ASTNode *parts = st->children[0], *dst = st->children[1];
        const ASTNode *ptr_cl = st->find_clause("POINTER", 2);

        emit_data_ref_addr_len(dst, nullptr);
        emit("    addiu $sp, $sp, -12\n    sw   $v0, 0($sp)\n    sw   $v1, 4($sp)\n");
        if (ptr_cl && !ptr_cl->children.empty()) {
            emit_eval_num(ptr_cl->children[0]);
            unscale_v0();
            emit("    addiu $v0, $v0, -1\n    sw   $v0, 8($sp)\n");
        } else {
            emit("    sw   $zero, 8($sp)\n");
        }

        for (size_t pi = 0; pi < parts->children.size(); pi++) {
            const ASTNode *p = parts->children[pi];
            if (!p || p->children.size() < 2) continue;
            const ASTNode *rv_list = p->children[0], *delim = p->children[1];
            int delim_ch = -1;
            if (!delim->is_clause("DELIM-SIZE")) {
                if (delim->kind == AST_FIGURATIVE && (ci_eq(delim->text, "SPACE") || ci_eq(delim->text, "SPACES"))) {
                    delim_ch = ' ';
                } else if (delim->kind == AST_LITERAL_STR) {
                    std::string db = unquote_cobol(delim->text);
                    if (!db.empty()) delim_ch = (unsigned char)db[0];
                }
            }
            for (size_t ri = 0; ri < rv_list->children.size(); ri++) {
                emit_addr_len(rv_list->children[ri]);
                int lp = new_lbl(), ldone = new_lbl();
                emit("    li   $t0, 0\n    lw   $t1, 0($sp)\n    lw   $t2, 4($sp)\n    lw   $t3, 8($sp)\nL%d:\n"
                     "    bge  $t0, $v1, L%d\n    bge  $t3, $t2, L%d\n    addu $t4, $v0, $t0\n    lbu  $t5, 0($t4)\n",
                     lp, ldone, ldone);
                if (delim_ch >= 0) emit("    li   $t6, %d\n    beq  $t5, $t6, L%d\n", delim_ch, ldone);
                emit("    addu $t4, $t1, $t3\n    sb   $t5, 0($t4)\n    addiu $t0, $t0, 1\n    addiu $t3, $t3, 1\n"
                     "    j    L%d\nL%d:\n    sw   $t3, 8($sp)\n", lp, ldone);
            }
        }
        if (ptr_cl && !ptr_cl->children.empty()) {
            emit("    lw   $v0, 8($sp)\n    addiu $v0, $v0, 1\n");
            if (cur_->prog_scale > 1) emit("    li   $t0, %d\n    mult $v0, $t0\n    mflo $v0\n", cur_->prog_scale);
            emit_store_num(ptr_cl->children[0]);
        }
        emit("    addiu $sp, $sp, 12\n");
    }

    void emit_stmt_unstring(const ASTNode *st) {
        if (st->children.size() < 2) return;
        const ASTNode *delim_cl = nullptr, *targets = nullptr;
        for (size_t i = 1; i < st->children.size(); i++) {
            const ASTNode *c = st->children[i];
            if (!c) continue;
            if (c->is_clause("UNSTRING-DELIM")) delim_cl = c;
            else if (c->kind == AST_LIST) targets = c;
        }
        if (!targets) return;
        int dch = ' ';
        if (delim_cl && !delim_cl->children.empty() && delim_cl->children[0]->kind == AST_LITERAL_STR) {
            std::string db = unquote_cobol(delim_cl->children[0]->text);
            if (!db.empty()) dch = (unsigned char)db[0];
        }
        emit_addr_len(st->children[0]);
        emit("    addiu $sp, $sp, -12\n    sw   $v0, 0($sp)\n    sw   $v1, 4($sp)\n    sw   $zero, 8($sp)\n");

        for (size_t ti = 0; ti < targets->children.size(); ti++) {
            emit_data_ref_addr_len(targets->children[ti], nullptr);
            push_pair("$v0", "$v1");
            emit("    move $a0, $v0\n    li   $a1, 32\n    move $a2, $v1\n");
            call_rt("_rt_memset", RT_MEM);
            pop_pair("$t0", "$t1");
            emit("    lw   $t2, 0($sp)\n    lw   $t3, 4($sp)\n    lw   $t4, 8($sp)\n    li   $t5, 0\n");
            int lp = new_lbl(), lhit = new_lbl(), ldn = new_lbl();
            emit("L%d:\n    bge  $t4, $t3, L%d\n    addu $t6, $t2, $t4\n    lbu  $t7, 0($t6)\n"
                 "    li   $v0, %d\n    beq  $t7, $v0, L%d\n    bge  $t5, $t1, L%d\n"
                 "    addu $t6, $t0, $t5\n    sb   $t7, 0($t6)\n    addiu $t4, $t4, 1\n    addiu $t5, $t5, 1\n"
                 "    j    L%d\nL%d:\n    addiu $t4, $t4, 1\nL%d:\n    sw   $t4, 8($sp)\n",
                 lp, ldn, dch, lhit, ldn, lp, lhit, ldn);
        }
        emit("    addiu $sp, $sp, 12\n");
    }

    void emit_stmt_set(const ASTNode *st) {
        if (st->text.empty() || st->children.empty()) return;
        if (ci_eq(st->text, "TO-TRUE")) {
            for (size_t i = 0; i < st->children.size(); i++) {
                int vi = cur_->resolve_data_ref(st->children[i]);
                if (vi >= 0 && cur_->vars[vi].kind == DATA_COND && cur_->vars[vi].parent_idx >= 0 &&
                    cur_->vars[vi].val_ast && !cur_->vars[vi].val_ast->children.empty()) {
                    ASTNode pref = make_ref(cur_->vars[cur_->vars[vi].parent_idx].name);
                    const ASTNode *val = cur_->vars[vi].val_ast->children[0];
                    if (val->kind == AST_BINOP && !val->children.empty()) val = val->children[0];
                    emit_move_one(val, &pref);
                }
            }
        } else if (ci_eq(st->text, "TO") && st->children.size() >= 2) {
            const ASTNode *rv = st->children.back();
            for (size_t i = 0; i + 1 < st->children.size(); i++) emit_move_one(rv, st->children[i]);
        } else if ((ci_eq(st->text, "UP") || ci_eq(st->text, "DOWN")) && st->children.size() >= 2) {
            bool is_up = ci_eq(st->text, "UP");
            emit_eval_num(st->children.back());
            push_reg("$v0");
            for (size_t i = 0; i + 1 < st->children.size(); i++) {
                emit_eval_num(st->children[i]);
                emit("    lw   $t0, 0($sp)\n    %s $v0, $v0, $t0\n", is_up ? "addu" : "subu");
                emit_store_num(st->children[i]);
            }
            emit("    addiu $sp, $sp, 4\n");
        }
    }

    void emit_stmt_search(const ASTNode *st) {
        if (st->children.size() < 2) return;
        int tvi = cur_->resolve_data_ref(st->children[0]);
        int max_occ = (tvi >= 0 && cur_->vars[tvi].occurs > 0) ? cur_->vars[tvi].occurs : 10;

        const ASTNode *idx_ref = nullptr, *at_end = nullptr;
        for (size_t i = 1; i < st->children.size(); i++) {
            const ASTNode *c = st->children[i];
            if (!c) continue;
            if (c->kind == AST_DATA_REF) idx_ref = c;
            else if (c->is_clause("AT-END")) at_end = c;
        }
        ASTNode fallback_ix;
        if (!idx_ref) {
            for (size_t i = 0; i < cur_->vars.size(); i++) {
                if (cur_->vars[i].kind == DATA_INDEX) {
                    fallback_ix = make_ref(cur_->vars[i].name);
                    idx_ref = &fallback_ix;
                    break;
                }
            }
        }
        int l_loop = new_lbl(), l_eof = new_lbl(), l_end = new_lbl();
        emit("L%d:\n", l_loop);
        if (idx_ref) {
            emit_eval_num(idx_ref);
            emit("    li   $t0, %d\n    bgt  $v0, $t0, L%d\n", max_occ * cur_->prog_scale, l_eof);
        }
        for (size_t i = 1; i < st->children.size(); i++) {
            const ASTNode *w = st->children[i];
            if (!w || w->kind != AST_WHEN || w->children.empty()) continue;
            int l_nw = new_lbl();
            CondCtx cc;
            emit_branch_cond(w->children[0], &cc, l_nw, false);
            emit_stmt_list(w, 1);
            emit("    j    L%d\nL%d:\n", l_end, l_nw);
        }
        if (idx_ref) {
            emit_eval_num(idx_ref);
            emit("    addiu $v0, $v0, %d\n", cur_->prog_scale);
            emit_store_num(idx_ref);
            emit("    j    L%d\n", l_loop);
        }
        emit("L%d:\n", l_eof);
        if (at_end) emit_stmt_list(at_end);
        emit("L%d:\n", l_end);
    }

    void emit_stmt(const ASTNode *st) {
        if (!st) return;
        switch (st->kind) {
            case AST_STMT_DISPLAY:    emit_stmt_display(st); return;
            case AST_STMT_ACCEPT:     emit_stmt_accept(st); return;
            case AST_STMT_MOVE:
                for (size_t i = 1; i < st->children.size(); i++) emit_move_one(st->children[0], st->children[i]);
                return;
            case AST_STMT_ADD:        emit_stmt_add_sub(st, false); return;
            case AST_STMT_SUBTRACT:   emit_stmt_add_sub(st, true); return;
            case AST_STMT_MULTIPLY:   emit_stmt_multiply(st); return;
            case AST_STMT_DIVIDE:     emit_stmt_divide(st); return;
            case AST_STMT_COMPUTE:    emit_stmt_compute(st); return;
            case AST_STMT_IF:         emit_stmt_if(st); return;
            case AST_STMT_EVALUATE:   emit_stmt_evaluate(st); return;
            case AST_STMT_PERFORM:    emit_stmt_perform(st); return;
            case AST_STMT_OPEN:       emit_stmt_open(st); return;
            case AST_STMT_CLOSE:      emit_stmt_close(st); return;
            case AST_STMT_READ:       emit_stmt_read(st); return;
            case AST_STMT_WRITE:
            case AST_STMT_REWRITE:    emit_stmt_write(st); return;
            case AST_STMT_GOTO:
                if (st->extra.empty() && !st->text.empty()) {
                    int bi = cur_->find_block(st->text);
                    if (bi >= 0) emit("    j    p%d_b%d\n", cur_->prog_id, bi);
                }
                return;
            case AST_STMT_CALL:       emit_stmt_call(st); return;
            case AST_STMT_STOP_RUN:   emit("    li   $a0, 0\n"); call_rt("_rt_exit", RT_EXIT); return;
            case AST_STMT_GOBACK:     emit("    j    p%d_exit\n", cur_->prog_id); return;
            case AST_STMT_INITIALIZE: emit_stmt_initialize(st); return;
            case AST_STMT_INSPECT:    emit_stmt_inspect(st); return;
            case AST_STMT_STRING:     emit_stmt_string(st); return;
            case AST_STMT_UNSTRING:   emit_stmt_unstring(st); return;
            case AST_STMT_SET:        emit_stmt_set(st); return;
            case AST_STMT_SEARCH:     emit_stmt_search(st); return;
            default:                  return;
        }
    }

    /* =====================================================================
     * Phase 5: Emit .text and .data
     * ===================================================================== */

    void emit_prog_text(const ProgCtx &pc) {
        cur_ = &pc;
        emit("\n# -------------------------------------------------------------------------\n");
        emit("# Program %d: %s (prog_frac=%d, prog_scale=%d, decimal_comma=%d)\n",
             pc.prog_id, pc.prog_name.c_str(), pc.prog_frac, pc.prog_scale, pc.decimal_comma ? 1 : 0);
        emit("# -------------------------------------------------------------------------\n");

        if (pc.prog_id == 0) {
            const char *entry = (target_ == MIPS_TARGET_MARS) ? "main" : "__start";
            emit("    .globl %s\n%s:\n", entry, entry);
        } else {
            emit("cob_sub_%s:\n", pc.san_name.c_str());
            emit("    la   $t0, p%d_ret_ra\n    sw   $ra, 0($t0)\n"
                 "    la   $t0, p%d_ret_sp\n    sw   $sp, 0($t0)\n", pc.prog_id, pc.prog_id);
            if (pc.uses_perform) {
                use_rt(RT_PERF);
                emit("    la   $t0, _rt_perf_top\n    lw   $t1, 0($t0)\n"
                     "    la   $t0, p%d_ret_pt\n    sw   $t1, 0($t0)\n", pc.prog_id);
            }
            for (size_t u = 0; u < pc.using_vars.size() && u < 4; u++) {
                emit("    la   $t0, p%d_lp%d\n    sw   $a%zu, 0($t0)\n",
                     pc.prog_id, pc.using_vars[u], u);
            }
        }

        for (size_t bi = 0; bi < pc.blocks.size(); bi++) {
            const ProcBlock &b = pc.blocks[bi];
            emit("\np%d_b%zu: # %s %s\n",
                 pc.prog_id, bi, b.is_section ? "SECTION" : "PARA", b.name.c_str());
            for (size_t si = 0; si < b.stmts.size(); si++) emit_stmt(b.stmts[si]);
            if (pc.uses_perform) {
                emit("    li   $a0, %d\n", b.global_id);
                call_rt("_rt_perf_chk", RT_PERF);
            }
        }

        emit("\np%d_exit:\n", pc.prog_id);
        if (pc.prog_id == 0) {
            emit("    li   $a0, 0\n");
            call_rt("_rt_exit", RT_EXIT);
        } else {
            if (pc.uses_perform) {
                emit("    la   $t0, p%d_ret_pt\n    lw   $t1, 0($t0)\n"
                     "    la   $t0, _rt_perf_top\n    sw   $t1, 0($t0)\n", pc.prog_id);
            }
            emit("    la   $t0, p%d_ret_sp\n    lw   $sp, 0($t0)\n"
                 "    la   $t0, p%d_ret_ra\n    lw   $ra, 0($t0)\n    jr   $ra\n",
                 pc.prog_id, pc.prog_id);
        }
    }

    void emit_all_data() const {
        emit("\n    .data\n");
        for (size_t pi = 0; pi < progs_.size(); pi++) {
            const ProgCtx &pc = *progs_[pi];
            emit("\n# --- Data for Program %d (%s) ---\n", pc.prog_id, pc.prog_name.c_str());
            if (pc.prog_id > 0) {
                emit("    .align 2\np%d_ret_ra: .word 0\np%d_ret_sp: .word 0\n", pc.prog_id, pc.prog_id);
                if (pc.uses_perform) emit("p%d_ret_pt: .word 0\n", pc.prog_id);
            }
            if (!pc.files.empty()) {
                emit("    .align 2\n");
                for (size_t fi = 0; fi < pc.files.size(); fi++) {
                    emit("p%d_fd%zu: .word -1 # %s\n", pc.prog_id, fi, pc.files[fi].name.c_str());
                }
            }
            for (size_t vi = 0; vi < pc.vars.size(); vi++) {
                const VarLayout &v = pc.vars[vi];
                if (v.kind == DATA_COND || v.parent_idx >= 0 || v.is_redefines || v.is_global_retcode)
                    continue;
                size_t sz = v.total_size > 0 ? (size_t)v.total_size : 1;
                std::vector<unsigned char> buf(sz, ' ');
                init_var_tree(pc, (int)vi, buf, 0);

                emit("p%d_d%zu: # %s (%zu bytes)\n", pc.prog_id, vi, v.name.c_str(), sz);
                emit_raw_bytes(out_, buf);
                if (v.section_kind == 3) {
                    emit("    .align 2\np%d_lp%zu: .word p%d_d%zu\n",
                         pc.prog_id, vi, pc.prog_id, vi);
                }
            }
        }

        const std::vector<StrPoolItem> &pool = strs_.items();
        if (!pool.empty()) {
            emit("\n# --- String & Picture Literal Pool ---\n");
            for (size_t i = 0; i < pool.size(); i++) {
                emit("cob_s%zu:\n", i);
                emit_string_data(out_, pool[i].bytes, pool[i].zterm);
            }
        }
        emit("    .align 2\n");
    }
};

bool codegen_mips(const ASTNode *root, const SymTab &symtab,
                  const CodegenOpts &opts, FILE *out) {
    (void)symtab;
    if (!root || !out) return false;
    MipsCodegen cg(out, opts);
    return cg.generate(root);
}
