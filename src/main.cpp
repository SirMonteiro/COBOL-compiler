#include "read.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "sema.h"
#include "codegen.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <string>
#include <vector>

#ifndef GIT_VERSION
#define GIT_VERSION "unknown"
#endif

static void print_help(const char *prog_name) {
    std::printf("Usage: %s [options] file...\n", prog_name);
    std::printf("A COBOL compiler targeting generic MIPS32 assembly\n\n");
    std::printf("Options:\n");
    std::printf("  -h, --help            Display this information.\n");
    std::printf("  -v, --version         Display compiler version information.\n");
    std::printf("  -o <file>, --output   Place the output into <file>.\n");
    std::printf("  -d, --debug           Add debugging lines in compilation.\n");
    std::printf("  -c, --comment         Display comments.\n");
    std::printf("  -l, --lex             Run lexical analysis only and print tokens.\n");
    std::printf("  -p, --parse           Stop after syntax analysis (skip semantic/codegen).\n");
    std::printf("  -a, --ast             Print the abstract syntax tree after parsing.\n");
    std::printf("  -s, --symtab          Print the symbol table and stop after semantic analysis.\n");
    std::printf("  -S, --mips            Emit MIPS32 assembly (default when no analysis-only flag is set).\n");
    std::printf("  -t <abi>, --target    MIPS target ABI: linux (default, MIPS32 o32 ELF) or mars (MARS/SPIM).\n");
    std::printf("  -I <dir>, --include   Add copybook/subprogram search directory.\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    int opt;
    OPTS opts = {false, false};
    const char *output_file = nullptr;
    bool lex_only = false;
    bool parse_only = false;
    bool dump_ast = false;
    bool dump_symtab = false;
    bool force_mips = false;
    MipsTarget target = MIPS_TARGET_LINUX;
    std::vector<std::string> copy_dirs;

    static struct option long_options[] = {
        {"help",    no_argument,       nullptr, 'h'},
        {"version", no_argument,       nullptr, 'v'},
        {"output",  required_argument, nullptr, 'o'},
        {"debug",   no_argument,       nullptr, 'd'},
        {"comment", no_argument,       nullptr, 'c'},
        {"lex",     no_argument,       nullptr, 'l'},
        {"parse",   no_argument,       nullptr, 'p'},
        {"ast",     no_argument,       nullptr, 'a'},
        {"symtab",  no_argument,       nullptr, 's'},
        {"mips",    no_argument,       nullptr, 'S'},
        {"target",  required_argument, nullptr, 't'},
        {"include", required_argument, nullptr, 'I'},
        {nullptr,   0,                 nullptr,  0}
    };

    while ((opt = getopt_long(argc, argv, "hvo:dclpasSt:I:", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0]);
                return EXIT_SUCCESS;
            case 'v':
                std::printf("Version %s\n", GIT_VERSION);
                return EXIT_SUCCESS;
            case 'o':
                output_file = optarg;
                break;
            case 'd':
                opts.include_debug = true;
                break;
            case 'c':
                opts.display_comments = true;
                break;
            case 'l':
                lex_only = true;
                break;
            case 'p':
                parse_only = true;
                break;
            case 'a':
                dump_ast = true;
                break;
            case 's':
                dump_symtab = true;
                break;
            case 'S':
                force_mips = true;
                break;
            case 't':
                if (std::strcmp(optarg, "linux") == 0) {
                    target = MIPS_TARGET_LINUX;
                } else if (std::strcmp(optarg, "mars") == 0) {
                    target = MIPS_TARGET_MARS;
                } else {
                    std::fprintf(stderr, "[S] ERROR: Unknown MIPS target \"%s\" (use linux or mars).\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            case 'I':
                copy_dirs.push_back(optarg);
                break;
            default:
                print_help(argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        std::fprintf(stderr, "[S] ERROR - No input file provided.\n");
        return EXIT_FAILURE;
    }

    bool emit_mips = force_mips || (!lex_only && !parse_only && !dump_ast && !dump_symtab);

    FILE *out = stdout;
    if (output_file) {
        out = std::fopen(output_file, "w");
        if (!out) {
            std::fprintf(stderr, "[S] ERROR: Failed to open output file \"%s\".\n", output_file);
            return EXIT_FAILURE;
        }
    }

    int status = EXIT_SUCCESS;
    const int nfiles = argc - optind;

    for (int fi = optind; fi < argc; fi++) {
        std::string input_path = argv[fi];
        if (nfiles > 1 && (lex_only || dump_ast || dump_symtab)) {
            std::fprintf(out, "### %s\n", input_path.c_str());
        }

        TokenList tokens;
        lexer_reset_errors();

        bool lex_ok = lex_cobol_file(
            input_path, opts, lex_only ? out : nullptr,
            &tokens, copy_dirs, !lex_only);

        if (!lex_ok || lexer_error_count() > 0) {
            status = EXIT_FAILURE;
            continue;
        }
        if (lex_only) continue;

        int parse_errors = 0;
        ASTNode *root = parse_tokens(tokens, input_path, &parse_errors);
        if (!root || parse_errors > 0) {
            status = EXIT_FAILURE;
            ast_free(root);
            continue;
        }

        if (dump_ast) ast_print(root, out, 0);
        if (!parse_only) {
            SymTab tab;
            bool sema_ok = sema_analyze(root, input_path, tab);
            if (dump_symtab) symtab_print(tab, out);

            if (!sema_ok) {
                status = EXIT_FAILURE;
            } else if (emit_mips) {
                CodegenOpts cg;
                cg.target = target;
                cg.lex_opts = &opts;
                cg.input_path = input_path;
                cg.copy_dirs = copy_dirs;
                if (!codegen_mips(root, tab, cg, out)) {
                    status = EXIT_FAILURE;
                }
            }
        }
        ast_free(root);
    }

    if (out != stdout) std::fclose(out);
    return status;
}
