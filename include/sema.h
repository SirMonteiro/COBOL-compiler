#ifndef SEMA_H
#define SEMA_H

#include <cstdio>
#include <string>
#include <vector>

#include "ast.h"

enum DataKind {
    DATA_UNKNOWN = 0,
    DATA_GROUP,
    DATA_ALPHA,          /* PIC A */
    DATA_NUMERIC,        /* PIC 9 / S9 / V9 */
    DATA_NUMERIC_EDITED, /* PIC Z / -Z9.99 / ZZZ,ZZ */
    DATA_ALPHANUMERIC,   /* PIC X */
    DATA_INDEX,          /* INDEXED BY */
    DATA_COND            /* Level 88 */
};

struct DataSym {
    std::string name;
    int level = 0;
    int parent_idx = -1; /* -1 for top-level (01 / 77 / INDEXED BY) */
    int fd_idx = -1;     /* -1 unless declared under an FD/SD */
    DataKind kind = DATA_UNKNOWN;
    std::string pic;
    int seq_num = 0;
    int col = 0;
};

struct FileSym {
    std::string name;
    bool has_select = false;
    bool has_fd = false;
    int select_seq = 0;
    int select_col = 0;
    int fd_seq = 0;
    int fd_col = 0;
};

struct ProcSym {
    std::string name;
    bool is_section = false;
    int seq_num = 0;
    int col = 0;
};

struct SymTab {
    std::vector<DataSym> data;
    std::vector<FileSym> files;
    std::vector<ProcSym> procs;
    int errors = 0;
    int warnings = 0;

    int find_file(const std::string &name) const;
    int find_proc(const std::string &name) const;
};

const char *data_kind_to_string(DataKind kind);
void symtab_print(const SymTab &tab, FILE *out);

/* Run Section 4 semantic checks on the AST and populate SymTab.
 * Returns true if zero semantic errors occurred (warnings are non-fatal). */
bool sema_analyze(const ASTNode *root, const std::string &filename, SymTab &out_tab);

#endif /* SEMA_H */
