#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct __attribute__((packed)) {
    char seq_num[6];   // Columns 1-6   (offset 0)
    char indicator;    // Column 7      (offset 6)
    char area_a[4];    // Columns 8-11  (offset 7)
    char area_b[61];   // Columns 12-72 (offset 11)
    char comment[8];   // Columns 73-80 (offset 72)
} COBOL_COLUMNS;

typedef struct {
    int seq_num;
    short area_a_len;
    short area_a_offset;
    short area_b_len;
    short area_b_offset;
} LINE;

short extract(const char *src, short *len) {
    if (!src || *len == 0) {
        *len = 0;
        return 0;
    }

    // Find first non-space string index
    short i = 0;
    while (i < *len && isspace((unsigned char) src[i])) i++;

    if (i == *len) {
        *len = 0;
        return 0;
    };
    // Find last non-space string index
    short j = *len - 1;
    while (j > i && isspace((unsigned char) src[j])) j--;

    short area_len = j - i + 1;
    *len = area_len;
    return i;
}

LINE *parse_line(char *line) {
    if (!line) return NULL;
    short line_len = strlen(line);
    while (line_len > 0 && (line[line_len - 1] == '\n' || line[line_len - 1] == '\r')) {
        line[--line_len] = '\0';
    }
    if (line_len < 6) return NULL;

    COBOL_COLUMNS *separated_line = (COBOL_COLUMNS *)line;
    char seq[7];
    memcpy(seq, separated_line->seq_num, 6);
    seq[6] = '\0';
    int line_number = atoi(seq);

    LINE *res = calloc(1, sizeof(LINE));
    if (!res) {
        fprintf(stderr, "[ERROR] Allocate of line %d area offsets", line_number);
        exit(EXIT_FAILURE);
    }
    res->seq_num = line_number;

    if (line_len == 6) return res;

    if (separated_line->indicator != ' ') {
        switch (separated_line->indicator) {
            case '*':
            case '/':
                printf("[COMMENT] Line %d, %s", line_number, separated_line->area_a); // Read area_a until end of line
                return res;
                break;
            case 'D':
                printf("[DEBUG LINE %d] %s", line_number, separated_line->area_a); // Read area_a until end of line
                break;
            case '-':
                printf("[WIP] Continuação de literal string da linha anterior.");
                break;
            default:
                fprintf(stderr, "[READING ERROR] Unknown indicator on line %d", line_number);
                break;
        }
    }


    if (line_len > 7) {
        short len = line_len - 7;
        if (len > 4) len = 4;
        short offset = extract(separated_line->area_a, &len);
        res->area_a_offset = offset + 7;
        res->area_a_len = len;
    } else {
        res->area_a_offset = 0;
        res->area_a_len = 0;
        res->area_b_offset = 0;
        res->area_b_len = 0;
        return res;
    }

    if (line_len > 11) {
        short len = line_len - 11;
        if (len > 61) len = 61;
        short offset = extract(separated_line->area_b, &len);
        res->area_b_offset = offset + 11;
        res->area_b_len = len;
    } else {
        res->area_b_offset = 0;
        res->area_b_len = 0;
    }

    return res;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Uso: %s <arquivo_de_entrada>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE *cobolSrc = fopen(argv[1], "r");
    if (!cobolSrc) {
        fprintf(stderr, "[main] ERROR - Falha ao abrir arquivo em \"%s\".\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    char buffer[83];

    // Read the first line into buffer and ensure it succeeded
    while (fgets(buffer, sizeof(buffer), cobolSrc) != NULL) {
            LINE *parsed = parse_line(buffer);
            if (parsed) {
                printf("Line Seq : %06d\n", parsed->seq_num);
                printf("Area A   : \"%.*s\"\n", parsed->area_a_len, buffer + parsed->area_a_offset);
                printf("Area B   : \"%.*s\"\n\n", parsed->area_b_len, buffer + parsed->area_b_offset);
                free(parsed);
            }
        }

    fclose(cobolSrc);
    return EXIT_SUCCESS;
}
