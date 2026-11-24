#ifndef CODEGEN_H
#define CODEGEN_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "ast.h"
#include "read.h"
#include "sema.h"

enum MipsTarget {
    MIPS_TARGET_LINUX = 0, /* Generic MIPS32 o32 Linux ELF (mipsel/mips routers, PS2 Linux, qemu-mipsel) */
    MIPS_TARGET_MARS       /* MARS / SPIM simulator syscalls */
};

/* Bitmask of MIPS32 runtime modules emitted on demand */
enum RtModule : uint32_t {
    RT_NONE       = 0,
    RT_EXIT       = 1u << 0,
    RT_WRITE_FD   = 1u << 1,
    RT_READ_FD    = 1u << 2,
    RT_OPEN_CLOSE = 1u << 3,
    RT_PRINT      = 1u << 4,
    RT_PRINT_NUM  = 1u << 5,
    RT_MEM        = 1u << 6,
    RT_CMP_ALNUM  = 1u << 7,
    RT_CMP_DIGITS = 1u << 8,
    RT_CLASS      = 1u << 9,
    RT_NUM        = 1u << 10,
    RT_EDITED     = 1u << 11,
    RT_ARITH      = 1u << 12,
    RT_LINE_IO    = 1u << 13,
    RT_ACCEPT     = 1u << 14,
    RT_BIG_DIGITS = 1u << 15,
    RT_STR_FUNCS  = 1u << 16,
    RT_INSPECT    = 1u << 17,
    RT_DATETIME   = 1u << 18,
    RT_PERF       = 1u << 19,
    RT_RETCODE    = 1u << 20,
    RT_RNG        = 1u << 21
};

struct CodegenOpts {
    MipsTarget target = MIPS_TARGET_LINUX;
    const OPTS *lex_opts = nullptr;
    std::string input_path;
    std::vector<std::string> copy_dirs;
};

/* Generate generic MIPS32 assembly from a semantically validated AST. */
bool codegen_mips(const ASTNode *root, const SymTab &symtab,
                  const CodegenOpts &opts, FILE *out);

/* Emit only the runtime routines (and their transitive deps) indicated by used_mask. */
void codegen_emit_runtime(FILE *out, MipsTarget target, uint32_t used_mask);

#endif /* CODEGEN_H */
