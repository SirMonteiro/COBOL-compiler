#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "ast.h"
#include "read.h"
#include "sema.h"

typedef enum {
    MIPS_TARGET_LINUX = 0, /* Generic MIPS32 o32 Linux ELF (mipsel/mips routers, PS2 Linux, qemu-mipsel) */
    MIPS_TARGET_MARS       /* MARS / SPIM simulator syscalls */
} MipsTarget;

typedef struct {
    MipsTarget target;
    const OPTS *lex_opts;
    const char *input_path;
    const char *const *copy_dirs;
    int n_copy_dirs;
} CodegenOpts;

/* Generate generic MIPS32 assembly from a semantically validated AST.
 * Returns true on success. */
bool codegen_mips(const ASTNode *root, const SymTab *symtab,
                  const CodegenOpts *opts, FILE *out);

/* Emit the self-contained MIPS32 runtime helpers for the selected target. */
void codegen_emit_runtime(FILE *out, MipsTarget target);

#endif /* CODEGEN_H */
