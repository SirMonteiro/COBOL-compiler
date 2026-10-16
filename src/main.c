#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

#include "read.h"
#include "lexer.h"

#ifndef GIT_VERSION
#define GIT_VERSION "unknown"
#endif

void print_help(const char *prog_name) {
    printf("Usage: %s [options] file...\n", prog_name);
    printf("A WIP COBOL very simple compiler\n\n");
    printf("Options:\n");
    printf("  -h, --help            Display this information.\n");
    printf("  -v, --version         Display compiler version information.\n");
    printf("  -o <file>, --output   Place the output into <file>.\n");
    printf("  -d, --debug           Add debugging lines in compilation.\n");
    printf("  -c, --comment         Display comments.\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    int opt;
    OPTS opts = {false, false};
    const char *output_file = NULL;

    static struct option long_options[] = {
        {"help",    no_argument,        0, 'h'},
        {"version", no_argument,        0, 'v'},
        {"output",  required_argument,  0, 'o'},
        {"debug",   no_argument,        0, 'd'},
        {"comment", no_argument,        0, 'c'},
        {0,         0,                  0,  0}
    };

    while ((opt = getopt_long(argc, argv, "hvo:dc", long_options, NULL)) != -1) {
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

    const char *input_path = argv[optind];
    FILE *cobolSrc = fopen(input_path, "r");
    if (!cobolSrc) {
        fprintf(stderr, "[S] ERROR: Failed to open input file \"%s\".\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    char raw_buffer[256];

    while (fgets(raw_buffer, sizeof(raw_buffer), cobolSrc) != NULL) {
        COBOL_LINE parsed_line;

        if (!read_cobol_line(&opts, raw_buffer, &parsed_line)) {
            fprintf(stderr,
                    "[S] Indicator Error (Seq %06d): Invalid indicator '%c' in Column 7.\n",
                    parsed_line.seq_num, parsed_line.indicator);
            continue;
        }

        switch (parsed_line.status) {
            case LINE_STATUS_CONTINUATION:
                /* String continuation ('-') handling: currently scans continuation slice */
                fprintf(out, "[W] Continuation line detected at Seq %06d\n",
                        parsed_line.seq_num);
            case LINE_STATUS_CODE:
                lexer_scan_cobol_line(parsed_line.code_slice, parsed_line.seq_num, out);
                break;
            case LINE_STATUS_COMMENT:
                if (opts.display_comments) {
                    fprintf(out, "[I] Comment (Seq %06d): %s\n",
                            parsed_line.seq_num, &raw_buffer[7]);
                }
                break;
            case LINE_STATUS_EMPTY:
            case LINE_STATUS_DEBUG_SKIPPED:
                break;
            default:
                break;
        }
    }

    fclose(cobolSrc);
    if (out != stdout) fclose(out);
    return EXIT_SUCCESS;
}
