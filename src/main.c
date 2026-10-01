#include <stdio.h>
#include <stdlib.h>

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

    fclose(cobolSrc);
    return EXIT_SUCCESS;
}
