#ifndef SEMA_H
#define SEMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "ast.h"

typedef enum {
    DATA_UNKNOWN = 0,
    DATA_GROUP,
    DATA_ALPHA,          /* PIC A */
    DATA_NUMERIC,        /* PIC 9 / S9 / V9 */
    DATA_NUMERIC_EDITED, /* PIC Z / -Z9.99 / ZZZ,ZZ */
    DATA_ALPHANUMERIC,   /* PIC X */
    DATA_INDEX,          /* INDEXED BY */
    DATA_COND            /* Level 88 */
} DataKind;

typedef struct {
    char name[64];
    int level;
    int parent_idx;      /* -1 for top-level (01 / 77 / INDEXED BY) */
    int fd_idx;          /* -1 unless declared under an FD/SD */
    DataKind kind;
    char pic[64];
    int seq_num;
    int col;
} DataSym;

typedef struct {
    char name[64];
    bool has_select;
    bool has_fd;
    int select_seq;
    int select_col;
    int fd_seq;
    int fd_col;
} FileSym;

typedef struct {
    char name[64];
    bool is_section;
    int seq_num;
    int col;
} ProcSym;

typedef struct {
    DataSym *data;
    size_t data_count;
    size_t data_cap;

    FileSym *files;
    size_t file_count;
    size_t file_cap;

    ProcSym *procs;
    size_t proc_count;
    size_t proc_cap;

    int errors;
    int warnings;
} SymTab;

const char *data_kind_to_string(DataKind kind);

void symtab_init(SymTab *tab);
void symtab_free(SymTab *tab);
void symtab_print(const SymTab *tab, FILE *out);

/* Run Section 4 semantic checks on the AST and populate SymTab.
 * Returns true if zero semantic errors occurred (warnings are non-fatal). */
bool sema_analyze(const ASTNode *root, const char *filename, SymTab *out_tab);

#endif /* SEMA_H */
