#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

#include "read.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "sema.h"

#ifndef GIT_VERSION
#define GIT_VERSION "unknown"
#endif

#define MAX_COPY_DIRS 32

void print_help(const char *prog_name) {
    printf("Usage: %s [options] file...\n", prog_name);
    printf("A WIP COBOL very simple compiler\n\n");
    printf("Options:\n");
    printf("  -h, --help            Display this information.\n");
    printf("  -v, --version         Display compiler version information.\n");
    printf("  -o <file>, --output   Place the output into <file>.\n");
    printf("  -d, --debug           Add debugging lines in compilation.\n");
    printf("  -c, --comment         Display comments.\n");
    printf("  -l, --lex             Run lexical analysis only and print tokens.\n");
    printf("  -p, --parse           Stop after syntax analysis (skip semantic checks).\n");
    printf("  -a, --ast             Print the abstract syntax tree after parsing.\n");
    printf("  -s, --symtab          Print the symbol table after semantic analysis.\n");
    printf("  -I <dir>, --include   Add copybook search directory for COPY.\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    int opt;
    OPTS opts = {false, false};
    const char *output_file = NULL;
    bool lex_only = false;
    bool parse_only = false;
    bool dump_ast = false;
    bool dump_symtab = false;
    const char *copy_dirs[MAX_COPY_DIRS];
    int n_copy_dirs = 0;

    static struct option long_options[] = {
        {"help",    no_argument,        0, 'h'},
        {"version", no_argument,        0, 'v'},
        {"output",  required_argument,  0, 'o'},
        {"debug",   no_argument,        0, 'd'},
        {"comment", no_argument,        0, 'c'},
        {"lex",     no_argument,        0, 'l'},
        {"parse",   no_argument,        0, 'p'},
        {"ast",     no_argument,        0, 'a'},
        {"symtab",  no_argument,        0, 's'},
        {"include", required_argument,  0, 'I'},
        {0,         0,                  0,  0}
    };

    while ((opt = getopt_long(argc, argv, "hvo:dclpasI:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0]);
                exit(EXIT_SUCCESS);
            case 'v':
                printf("Version %s\n", GIT_VERSION);
                exit(EXIT_SUCCESS);
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
            case 'I':
                if (n_copy_dirs < MAX_COPY_DIRS) {
                    copy_dirs[n_copy_dirs++] = optarg;
                }
                break;
            default:
                print_help(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "[S] ERROR - No input file provided.\n");
        exit(EXIT_FAILURE);
    }

    FILE *out = stdout;
    if (output_file) {
        out = fopen(output_file, "w");
        if (!out) {
            fprintf(stderr, "[S] ERROR: Failed to open output file \"%s\".\n", output_file);
            exit(EXIT_FAILURE);
        }
    }

    int status = EXIT_SUCCESS;
    const int nfiles = argc - optind;

    for (int fi = optind; fi < argc; fi++) {
        const char *input_path = argv[fi];
        if (nfiles > 1 && (lex_only || dump_ast || dump_symtab)) {
            fprintf(out, "### %s\n", input_path);
        }

        TokenList tokens;
        token_list_init(&tokens);
        lexer_reset_errors();

        bool lex_ok = lex_cobol_file(
            input_path,
            &opts,
            lex_only ? out : NULL,
            &tokens,
            copy_dirs,
            n_copy_dirs,
            !lex_only
        );

        if (!lex_ok || lexer_error_count() > 0) {
            status = EXIT_FAILURE;
            token_list_free(&tokens);
            continue;
        }

        if (!lex_only) {
            int parse_errors = 0;
            ASTNode *root = parse_tokens(&tokens, input_path, &parse_errors);
            if (!root || parse_errors > 0) {
                status = EXIT_FAILURE;
            } else {
                if (dump_ast) {
                    ast_print(root, out, 0);
                }
                if (!parse_only) {
                    SymTab tab;
                    bool sema_ok = sema_analyze(root, input_path, &tab);
                    if (dump_symtab) {
                        symtab_print(&tab, out);
                    }
                    if (!sema_ok) {
                        status = EXIT_FAILURE;
                    }
                    symtab_free(&tab);
                }
            }
            ast_free(root);
        }

        token_list_free(&tokens);
    }

    if (out != stdout) fclose(out);
    return status;
}
