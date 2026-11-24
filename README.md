# Compilador COBOL (`cbc`) - ACH2087

Compilador COBOL para a disciplina [ACH2087 - Construção de Compiladores](https://uspdigital.usp.br/jupiterweb/obterDisciplina?sgldis=ACH2087&codcur=86200&codhab=204) (EACH-USP, 2º semestre de 2026), seguindo as especificações passadas em [EspecificacaoCOBOL.md](EspecificacaoCOBOL.md).

Grupo:

- Adryelli Reis dos Santos (14714019)
- André Vieira Rocha (12681611)
- Gabriel Monteiro de Souza (14746450)

Repositório público: [Em markdown](https://github.com/SirMonteiro/COBOL-compiler/blob/main/README.md), [em PDF](https://github.com/SirMonteiro/COBOL-compiler/blob/main/relatorio.pdf)

## Compilação e uso

```sh
make            # compila o binário ./cbc (Flex + Bison + GCC)
make help       # mostra o menu de ajuda de compilação/teste
./cbc           # mostra o menu de ajuda dos possíveis argumentos
./cbc programa.cbl -o prog.s    # pipeline completo (lex + parse + sema + MIPS)
./cbc -t mars programa.cbl -o prog.asm  # gera assembly MIPS para MARS/SPIM
./cbc -l programa.cbl           # executa apenas o lexer e imprime o fluxo de tokens
./cbc -p programa.cbl           # executa lexer + parser (sem análise semântica)
./cbc -a programa.cbl           # imprime a AST gerada pelo parser
./cbc -s programa.cbl           # imprime a tabela de símbolos e para após a semântica
./cbc -I dir/ programa.cbl      # adiciona diretório extra de busca para COPY e CALL
make test       # compila, monta e linka os 14 programas de teste para ELF MIPS32
make test-sema  # executa lex + parse + semântica (-s) nos 14 programas de teste
make test-parse # executa apenas lex + parse (-p) nos 14 programas de teste
make test-lex   # executa apenas o lexer (-l) nos 14 programas de teste
```

Exemplo para montar, linkar e executar um binário estático MIPS3 Linux (`mipsel`, sem `libc`):

```sh
./cbc exemplos/CALCMD01.cbl -o CALCMD01.s
clang -target mipsel-unknown-linux-gnu -mabi=32 -march=mips3 -mno-abicalls -fno-pic -c CALCMD01.s -o CALCMD01.o
ld.lld -m elf32ltsmip -static -e __start CALCMD01.o -o CALCMD01
qemu-mipsel ./CALCMD01
```

Para gerar a versão em PDF deste relatório na raiz do repositório:

```sh
make -C relatorio pdf
```

## 1. Introdução

O COBOL (*Common Business-Oriented Language*) surgiu em 1959 a partir do comitê CODASYL, apoiado pelo Departamento de Defesa dos Estados Unidos, para processamento comercial com leitura próxima ao inglês ([GnuCOBOL FAQ](https://gnucobol.sourceforge.io/faq/index.html#history)). A linguagem continua em produção em bancos, folhas de pagamento, seguradoras e órgãos públicos pelo volume de código legado já validado e pelo custo e risco de reescrever rotinas em lote que operam há décadas.

Compilar COBOL em formato fixo traz problemas práticos que não aparecem em linguagens de formato livre:

1. Cada linha física divide-se em Área de Sequência (colunas 1 a 6), Área de Indicador (coluna 7, com espaço em branco, `*` ou `/` para comentário, `-` para continuação e `D` para depuração), Área A (colunas 8 a 11), Área B (colunas 12 a 72) e Área do Programa (colunas 73 a 80, ignorada pelo compilador). Arquivos editados com tabulações (`\t`) exigem expansão prévia para múltiplos de 8 colunas para que a fronteira entre Área A e Área B não se desloque.
2. A posição inicial do token define se um elemento pertence à Área A (cabeçalhos de divisão, seções, parágrafos e níveis `01` e `77`) ou à Área B (comandos, cláusulas e níveis subordinados). Por isso o lexer registra a coluna e a área em cada token emitido.
3. Literais alfanuméricos abertos e identificadores longos podem terminar na coluna 72 e continuar na linha seguinte marcada com `-` na coluna 7, como acontece com `TAB-TRABALHADOR` nos programas de teste e com strings maiores que uma linha.
4. O ponto final `.` encerra sentenças e parágrafos, mas também aparece em literais numéricos (`-123.45`) e em máscaras de formato (`PIC ZZ9.99.`, `PIC ZZZ.ZZZ.ZZZ,ZZ.`). Máscaras como `PIC -ZZZZZZZZZ9.99` começam com `-` e seriam confundidas com identificadores inválidos se lidas fora do contexto de `PIC` ou `PICTURE`.
5. A linguagem reúne centenas de palavras reservadas insensíveis a maiúsculas e minúsculas, aceita hífens internos em identificadores de até 30 caracteres (`WRK-VALOR1`), permite delimitadores de bloco opcionais (como `IF` sem `END-IF`, fechado por `END-PERFORM` ou `.`) e aceita condições relacionais abreviadas (`IF WKS-OPCAO = "S" OR "U"`).

## 2. Parte 1: Análise Léxica

### 2.1 Implementação do lexer

O analisador léxico foi escrito em C com o gerador `flex`, compilado com `gcc` (`-O3 -Wall -Wformat`), seguindo a Seção 2 de [EspecificacaoCOBOL.md](EspecificacaoCOBOL.md).

Os arquivos responsáveis por essa etapa são:

- [include/read.h](include/read.h) e [src/read.c](src/read.c): implementam `read_cobol_line`, que expande tabulações (`expand_tabs` com largura 8), extrai a área de sequência (`parse_seq`), valida o indicador da coluna 7 (espaço, `*`, `/`, `-`, `D`/`d`), separa comentários quando `-c` está ativo, descarta linhas de depuração sem `-d` e recorta exatamente as colunas 8 a 72 (`code_slice`). Indicadores inválidos geram erro severo `[S]`.
- [src/lexer.l](src/lexer.l): define as regras do Flex com `%option caseless`, comentários inline `*>`, palavras reservadas agrupadas (`KW_DIVISIONS`, `KW_VERBS`, `KW_ENDS`, `KW_CLAUSES`, `KW_MISC`, `KW_FIGURATIVE`), literais numéricos com sinal e separador decimal interno (`.` ou `,` para `DECIMAL-POINT IS COMMA`), strings entre aspas simples ou duplas (com aspas dobradas como escape), operadores aritméticos e relacionais (`+`, `-`, `*`, `/`, `**`, `=`, `<>`, `<`, `<=`, `>`, `>=`) e pontuação (`.`, `,`, `;`, `(`, `)`, `:`). Após `PIC`/`PICTURE [IS]`, o scanner entra no estado exclusivo `%x PICSTR`, captura a máscara como `TOKEN_PICTURE` e devolve eventual `.` final ao fluxo via `yyless`. Identificadores aceitam `A-Z`, `0-9`, `_` (usado em `PROGRAM-ID` nos exemplos) e `-` interno, com limite de 30 caracteres e rejeição de hífen inicial ou final.
- [include/lexer.h](include/lexer.h) e [src/lexer_support.c](src/lexer_support.c): implementam `emit_token` (normalizando palavras-chave e identificadores para maiúsculas, gravando os tokens em `TokenList` e imprimindo o fluxo quando `-l` está ativo), `emit_lexical_error` (reportando erro `[E]` em `stderr` e emitindo `TOKEN_UNKNOWN` sem interromper a varredura), o lookahead de uma linha (`lexer_begin_file`, `lexer_process_line`, `lexer_end_file`) para continuações `-` de strings e identificadores na coluna 72, e `lex_cobol_file` com expansão recursiva de copybooks `COPY`.
- [src/main.c](src/main.c): processa argumentos via `getopt_long` (`-h`, `-v`, `-o`, `-d`, `-c`, `-l`, `-a`, `-I`), aceita múltiplos arquivos, usa o número da linha física quando as colunas 1 a 6 estão em branco, descarta sobras de linhas acima do buffer e retorna código de saída diferente de zero quando ocorrem erros.

Uso de ferramentas de IA na Parte 1: o grupo usou modelos de linguagem (Claude e Gemini via Antigravity) na revisão das regras léxicas de [src/lexer.l](src/lexer.l) (palavras reservadas estendidas e estado `PICSTR`), na checagem de casos de borda do formato fixo (tabulações, continuação na coluna 72 e literais não fechados) e na revisão textual deste relatório. O leitor de colunas, a CLI e a estrutura principal em C foram escritos manualmente.

### 2.2 Resultados dos testes léxicos

O lexer (`./cbc -l`) foi executado nos sete programas de [cobol-exemplos/](cobol-exemplos/), além dos sete programas de [exemplos/](exemplos/), todos com código de saída `0` via `make test-lex`.

| Programa | Descrição | Tokens emitidos | Exit |
| ---------------------------- | ---------------------------------------------- | ---------------: | ---: |
| `prog001_divisoes.cob` | Divisões básicas, seções e `DISPLAY` simples | 76 | 0 |
| `prog002_operacoes.cob` | Operações aritméticas (`ADD`, `SUBTRACT`, `MULTIPLY`, `DIVIDE`, `MOVE`) | 320 | 0 |
| `prog003_calculadora.cob` | Calculadora interativa com `ACCEPT`, `IF`/`ELSE`, `COMPUTE` e `PIC -ZZZZZZZZZ9.99` | 229 | 0 |
| `prog004_folha_pgto.cob` | Tabela (`OCCURS`), `PERFORM ... UNTIL` e continuação de identificador na coluna 72 | 221 | 0 |
| `prog005_strings.cob` | Manipulação de cadeias com `INSPECT`, `STRING` e `UNSTRING` | 236 | 0 |
| `prog006_files.cob` | Manipulação de arquivos sequenciais (`SELECT`, `FD`, `OPEN`, `WRITE`, `CLOSE`) | 225 | 0 |
| `prog007_loja.cob` | Cadastro de produtos com `EVALUATE`, leitura/escrita indexada e `INVALID KEY` | 566 | 0 |

Os fluxos completos de tokens gerados por `./cbc -l` para cada programa de `cobol-exemplos/` estão abaixo.

<details>
<summary>prog001_divisoes.cob (76 tokens, exit 0)</summary>

```text
$ ./cbc -l cobol-exemplos/prog001_divisoes.cob
[SEQ 000015 | COL 08 (AREA A)] TOKEN_KEYWORD              : IDENTIFICATION
[SEQ 000015 | COL 23 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000015 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000016 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROGRAM-ID
[SEQ 000016 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000016 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG0001
[SEQ 000016 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000019 | COL 08 (AREA A)] TOKEN_KEYWORD              : AUTHOR
[SEQ 000019 | COL 14 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000019 | COL 16 (AREA B)] TOKEN_IDENTIFIER           : ENIO
[SEQ 000019 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : BASSO
[SEQ 000019 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000020 | COL 08 (AREA A)] TOKEN_KEYWORD              : INSTALLATION
[SEQ 000020 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000020 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : EBASSO
[SEQ 000020 | COL 28 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000020 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : DOT
[SEQ 000020 | COL 32 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000020 | COL 33 (AREA B)] TOKEN_IDENTIFIER           : NET
[SEQ 000020 | COL 36 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000021 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATE-WRITTEN
[SEQ 000021 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000021 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : 2018-02-24
[SEQ 000021 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000022 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATE-COMPILED
[SEQ 000022 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000022 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : 2018-02-24
[SEQ 000022 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000023 | COL 08 (AREA A)] TOKEN_KEYWORD              : SECURITY
[SEQ 000023 | COL 16 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000023 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : O
[SEQ 000023 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROGRAMA
[SEQ 000023 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : USA
[SEQ 000023 | COL 33 (AREA B)] TOKEN_IDENTIFIER           : CRIPTOGRAFIA
[SEQ 000023 | COL 46 (AREA B)] TOKEN_IDENTIFIER           : XPTO
[SEQ 000023 | COL 50 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000024 | COL 08 (AREA A)] TOKEN_KEYWORD              : REMARKS
[SEQ 000024 | COL 15 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000024 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : O
[SEQ 000024 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : PROGRAMA
[SEQ 000024 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : RECEBE
[SEQ 000024 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : UM
[SEQ 000024 | COL 38 (AREA B)] TOKEN_IDENTIFIER           : VALOR
[SEQ 000024 | COL 44 (AREA B)] TOKEN_IDENTIFIER           : E
[SEQ 000024 | COL 46 (AREA B)] TOKEN_IDENTIFIER           : IMPRIME
[SEQ 000024 | COL 54 (AREA B)] TOKEN_IDENTIFIER           : NA
[SEQ 000024 | COL 57 (AREA B)] TOKEN_IDENTIFIER           : TELA
[SEQ 000024 | COL 61 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000027 | COL 08 (AREA A)] TOKEN_KEYWORD              : ENVIRONMENT
[SEQ 000027 | COL 20 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000027 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000029 | COL 08 (AREA A)] TOKEN_KEYWORD              : CONFIGURATION
[SEQ 000029 | COL 22 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000029 | COL 29 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000037 | COL 08 (AREA A)] TOKEN_KEYWORD              : INPUT-OUTPUT
[SEQ 000037 | COL 21 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000037 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000046 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATA
[SEQ 000046 | COL 13 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000046 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000053 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROCEDURE
[SEQ 000053 | COL 18 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000053 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000054 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : MAIN-PROCEDURE
[SEQ 000054 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000055 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000055 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "Hello world"
[SEQ 000056 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000056 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "Enio Basso - 2"
[SEQ 000057 | COL 13 (AREA B)] TOKEN_KEYWORD              : STOP
[SEQ 000057 | COL 18 (AREA B)] TOKEN_KEYWORD              : RUN
[SEQ 000057 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000058 | COL 08 (AREA A)] TOKEN_KEYWORD              : END
[SEQ 000058 | COL 12 (AREA B)] TOKEN_KEYWORD              : PROGRAM
[SEQ 000058 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG0001
[SEQ 000058 | COL 28 (AREA B)] TOKEN_PERIOD               : .
```

</details>

<details>
<summary>prog002_operacoes.cob (320 tokens, exit 0)</summary>

```text
$ ./cbc -l cobol-exemplos/prog002_operacoes.cob
[SEQ 000011 | COL 08 (AREA A)] TOKEN_KEYWORD              : IDENTIFICATION
[SEQ 000011 | COL 23 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000011 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROGRAM-ID
[SEQ 000012 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : YOUR-PROGRAM-NAME
[SEQ 000012 | COL 37 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000013 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATA
[SEQ 000013 | COL 13 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000013 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000014 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE
[SEQ 000014 | COL 13 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000014 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000015 | COL 08 (AREA A)] TOKEN_KEYWORD              : WORKING-STORAGE
[SEQ 000015 | COL 24 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000015 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000016 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000016 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR1
[SEQ 000016 | COL 32 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000016 | COL 36 (AREA B)] TOKEN_PICTURE              : 9(10)
[SEQ 000016 | COL 42 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000016 | COL 49 (AREA B)] TOKEN_LITERAL_NUMERIC      : 10
[SEQ 000016 | COL 51 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000017 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000017 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000017 | COL 32 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000017 | COL 36 (AREA B)] TOKEN_PICTURE              : 9(10)
[SEQ 000017 | COL 42 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000017 | COL 49 (AREA B)] TOKEN_KEYWORD              : ZEROS
[SEQ 000017 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000018 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000018 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000018 | COL 32 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000018 | COL 36 (AREA B)] TOKEN_PICTURE              : 9(2)
[SEQ 000018 | COL 41 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000018 | COL 48 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000018 | COL 49 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000019 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000019 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000019 | COL 32 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000019 | COL 36 (AREA B)] TOKEN_PICTURE              : 9(2)
[SEQ 000019 | COL 41 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000019 | COL 48 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000019 | COL 49 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000020 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000020 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WRK-C
[SEQ 000020 | COL 32 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000020 | COL 36 (AREA B)] TOKEN_PICTURE              : 9(2)
[SEQ 000020 | COL 41 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000020 | COL 48 (AREA B)] TOKEN_KEYWORD              : ZEROS
[SEQ 000020 | COL 53 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000022 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000022 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WRK-TOTAIS
[SEQ 000022 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000023 | COL 15 (AREA B)] TOKEN_LITERAL_NUMERIC      : 05
[SEQ 000023 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : WRK-TOTAL
[SEQ 000023 | COL 32 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000023 | COL 36 (AREA B)] TOKEN_PICTURE              : 9(10)
[SEQ 000023 | COL 42 (AREA B)] TOKEN_KEYWORD              : VALUE
[SEQ 000023 | COL 48 (AREA B)] TOKEN_LITERAL_NUMERIC      : 19
[SEQ 000023 | COL 50 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000025 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROCEDURE
[SEQ 000025 | COL 18 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000025 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000026 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : MAIN-PROCEDURE
[SEQ 000026 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000027 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000027 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "===== start ======="
[SEQ 000027 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000028 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000028 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 20
[SEQ 000028 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000028 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000028 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000029 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000029 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "MOVE 20 TO WRK-VALOR2 -->"
[SEQ 000029 | COL 48 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000029 | COL 58 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000031 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000031 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR1
[SEQ 000031 | COL 28 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000031 | COL 31 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000031 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000032 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000032 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "MOVE WRK-VALOR1 TO WRK-VALOR2 -->"
[SEQ 000032 | COL 56 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000032 | COL 66 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000034 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000034 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 70
[SEQ 000034 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000034 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR1
[SEQ 000034 | COL 34 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000034 | COL 44 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000035 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000035 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "MOVE 70 TO WRK-VALOR1 WRK-VALOR2 -->"
[SEQ 000035 | COL 59 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000035 | COL 69 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000037 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000037 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : WRK-TOTAL
[SEQ 000037 | COL 27 (AREA B)] TOKEN_KEYWORD              : OF
[SEQ 000037 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : WRK-TOTAIS
[SEQ 000037 | COL 41 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000037 | COL 44 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000037 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000038 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000038 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "MOVE WRK-TOTAL OF WRK-TOTAIS TO WRK-VALOR2. -->"
[SEQ 000039 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WRK-VALOR2
[SEQ 000039 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000041 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000041 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "--------"
[SEQ 000041 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000042 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000042 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A = 1, B = 1, C = 1"
[SEQ 000042 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000044 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000044 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000044 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000045 | COL 12 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000045 | COL 16 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000045 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000045 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000045 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000045 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000046 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000046 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "ADD B C TO A (A = B + C + A) -->"
[SEQ 000046 | COL 55 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000046 | COL 60 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000048 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000048 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000048 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000048 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000048 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000049 | COL 12 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000049 | COL 16 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000049 | COL 18 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000049 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000049 | COL 27 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000049 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000050 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000050 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000050 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000051 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000051 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "ADD 1 TO A B (A = A + 1; B = B + 1) -->"
[SEQ 000052 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000052 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= "
[SEQ 000052 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000052 | COL 32 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " , B= "
[SEQ 000052 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000052 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000054 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000054 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000054 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000055 | COL 12 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000055 | COL 16 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000055 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000055 | COL 20 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000055 | COL 27 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000055 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000056 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000056 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "ADD B C GIVING A (A = B + C) -->"
[SEQ 000056 | COL 55 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000056 | COL 60 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000058 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000058 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "============ SUBTRACT ============= "
[SEQ 000058 | COL 58 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000059 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000059 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 99
[SEQ 000059 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000059 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000059 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000060 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000060 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000060 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000060 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000060 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000061 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000061 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= 99, B= 0"
[SEQ 000061 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000062 | COL 12 (AREA B)] TOKEN_KEYWORD              : SUBTRACT
[SEQ 000062 | COL 21 (AREA B)] TOKEN_LITERAL_NUMERIC      : 30
[SEQ 000062 | COL 24 (AREA B)] TOKEN_LITERAL_NUMERIC      : 23
[SEQ 000062 | COL 27 (AREA B)] TOKEN_KEYWORD              : FROM
[SEQ 000062 | COL 32 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000063 | COL 27 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000063 | COL 34 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000063 | COL 39 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000064 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000064 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SUBTRACT 30 23 FROM A GIVING B.-->"
[SEQ 000065 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000065 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= "
[SEQ 000065 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000065 | COL 32 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " , B= "
[SEQ 000065 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000065 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000067 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000067 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "============ MULTIPLY ============= "
[SEQ 000067 | COL 58 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000068 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000068 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 40
[SEQ 000068 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000068 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000068 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000069 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000069 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000069 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000069 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000069 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000070 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000070 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= 40, B= 0"
[SEQ 000070 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000071 | COL 12 (AREA B)] TOKEN_KEYWORD              : MULTIPLY
[SEQ 000071 | COL 21 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000071 | COL 23 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000071 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000071 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000072 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000072 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "MULTIPLY A BY 2 -->"
[SEQ 000073 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000073 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= "
[SEQ 000073 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000073 | COL 32 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " , B= "
[SEQ 000073 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000073 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000075 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000075 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "============ MULTIPLY ============= "
[SEQ 000075 | COL 58 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000076 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000076 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 40
[SEQ 000076 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000076 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000076 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000077 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000077 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000077 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000077 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000077 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000078 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000078 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= 40, B= 0"
[SEQ 000078 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000079 | COL 12 (AREA B)] TOKEN_KEYWORD              : MULTIPLY
[SEQ 000079 | COL 21 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000079 | COL 23 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000079 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000079 | COL 32 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000079 | COL 39 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000079 | COL 44 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000080 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000080 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "MULTIPLY A BY 2 GIVING B.-->"
[SEQ 000081 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000081 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= "
[SEQ 000081 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000081 | COL 32 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " , B= "
[SEQ 000081 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000081 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000083 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000083 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "============ DIVIDE ============="
[SEQ 000083 | COL 55 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000084 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000084 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 81
[SEQ 000084 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000084 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000084 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000085 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000085 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000085 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000085 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000085 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000086 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000086 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000086 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000086 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WRK-C
[SEQ 000086 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000087 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000087 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= 80, B= 0"
[SEQ 000087 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000088 | COL 12 (AREA B)] TOKEN_KEYWORD              : DIVIDE
[SEQ 000088 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000088 | COL 25 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000088 | COL 28 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000088 | COL 30 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000088 | COL 37 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000088 | COL 43 (AREA B)] TOKEN_KEYWORD              : REMAINDER
[SEQ 000088 | COL 53 (AREA B)] TOKEN_IDENTIFIER           : WRK-C
[SEQ 000088 | COL 58 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000089 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000089 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "DIVIDE A BY 2 GIVING B REMAINDER C .-->"
[SEQ 000090 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000090 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A= "
[SEQ 000090 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000090 | COL 32 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " , B= "
[SEQ 000090 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WRK-B
[SEQ 000090 | COL 47 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " , C= "
[SEQ 000090 | COL 56 (AREA B)] TOKEN_IDENTIFIER           : WRK-C
[SEQ 000090 | COL 61 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000093 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000093 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000093 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000094 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000094 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 99
[SEQ 000094 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000094 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000094 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000095 | COL 12 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000095 | COL 16 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000095 | COL 18 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000095 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000096 | COL 12 (AREA B)] TOKEN_KEYWORD              : ON
[SEQ 000096 | COL 15 (AREA B)] TOKEN_KEYWORD              : SIZE
[SEQ 000096 | COL 20 (AREA B)] TOKEN_KEYWORD              : ERROR
[SEQ 000097 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000097 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "ON SIZE ERROR -->"
[SEQ 000097 | COL 43 (AREA B)] TOKEN_IDENTIFIER           : WRK-A
[SEQ 000097 | COL 48 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000099 | COL 13 (AREA B)] TOKEN_KEYWORD              : STOP
[SEQ 000099 | COL 18 (AREA B)] TOKEN_KEYWORD              : RUN
[SEQ 000099 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000100 | COL 08 (AREA A)] TOKEN_KEYWORD              : END
[SEQ 000100 | COL 12 (AREA B)] TOKEN_KEYWORD              : PROGRAM
[SEQ 000100 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : YOUR-PROGRAM-NAME
[SEQ 000100 | COL 37 (AREA B)] TOKEN_PERIOD               : .
```

</details>

<details>
<summary>prog003_calculadora.cob (229 tokens, exit 0)</summary>

```text
$ ./cbc -l cobol-exemplos/prog003_calculadora.cob
[SEQ 000011 | COL 08 (AREA A)] TOKEN_KEYWORD              : IDENTIFICATION
[SEQ 000011 | COL 23 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000011 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROGRAM-ID
[SEQ 000012 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : CALCULADORA
[SEQ 000012 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000013 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATA
[SEQ 000013 | COL 13 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000013 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000014 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE
[SEQ 000014 | COL 13 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000014 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000015 | COL 08 (AREA A)] TOKEN_KEYWORD              : WORKING-STORAGE
[SEQ 000015 | COL 24 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000015 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000016 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000016 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-1
[SEQ 000016 | COL 35 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000016 | COL 39 (AREA B)] TOKEN_PICTURE              : S9(05)V99
[SEQ 000016 | COL 52 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000016 | COL 59 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000016 | COL 60 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000017 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000017 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-2
[SEQ 000017 | COL 35 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000017 | COL 39 (AREA B)] TOKEN_PICTURE              : S9(05)V9(02)
[SEQ 000017 | COL 52 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000017 | COL 59 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000017 | COL 60 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000018 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000018 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULTADO
[SEQ 000018 | COL 35 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000018 | COL 39 (AREA B)] TOKEN_PICTURE              : S9(10)V99
[SEQ 000018 | COL 52 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000018 | COL 59 (AREA B)] TOKEN_KEYWORD              : ZEROS
[SEQ 000018 | COL 64 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000019 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000019 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESTO
[SEQ 000019 | COL 35 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000019 | COL 39 (AREA B)] TOKEN_PICTURE              : 9(2)
[SEQ 000019 | COL 52 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000019 | COL 59 (AREA B)] TOKEN_KEYWORD              : ZEROS
[SEQ 000019 | COL 64 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000020 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000020 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000020 | COL 35 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000020 | COL 39 (AREA B)] TOKEN_PICTURE              : A(01)
[SEQ 000020 | COL 44 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000021 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000021 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-SAIR
[SEQ 000021 | COL 35 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000021 | COL 39 (AREA B)] TOKEN_PICTURE              : 9(1)
[SEQ 000021 | COL 52 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000021 | COL 59 (AREA B)] TOKEN_KEYWORD              : ZEROS
[SEQ 000021 | COL 64 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000022 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000022 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULT-FORMAT
[SEQ 000022 | COL 35 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000022 | COL 39 (AREA B)] TOKEN_PICTURE              : -ZZZZZZZZZ9.99
[SEQ 000022 | COL 53 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000024 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROCEDURE
[SEQ 000024 | COL 18 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000024 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000025 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : MAIN-PROCEDURE
[SEQ 000025 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000027 | COL 12 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000027 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : 00000-CALCULADORA
[SEQ 000027 | COL 38 (AREA B)] TOKEN_KEYWORD              : UNTIL
[SEQ 000027 | COL 44 (AREA B)] TOKEN_IDENTIFIER           : WKS-SAIR
[SEQ 000027 | COL 53 (AREA B)] TOKEN_EQ                   : =
[SEQ 000027 | COL 55 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000027 | COL 56 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000029 | COL 12 (AREA B)] TOKEN_KEYWORD              : STOP
[SEQ 000029 | COL 17 (AREA B)] TOKEN_KEYWORD              : RUN
[SEQ 000029 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000031 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00000-CALCULADORA
[SEQ 000031 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000032 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000032 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "============ CALCULADORA ============= "
[SEQ 000032 | COL 61 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000033 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000033 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "1-. USAR CALCULADORA, 2-. SAIR"
[SEQ 000033 | COL 52 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000034 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000034 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WKS-SAIR
[SEQ 000034 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000036 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000036 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-SAIR
[SEQ 000036 | COL 24 (AREA B)] TOKEN_EQ                   : =
[SEQ 000036 | COL 26 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000037 | COL 15 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000037 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : 00001-PEDIR-DADOS
[SEQ 000038 | COL 15 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000038 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : 00002-EXECUTAR-OPERACOES
[SEQ 000039 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000039 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000041 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00001-PEDIR-DADOS
[SEQ 000041 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000042 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000042 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "OPERACAO: "
[SEQ 000042 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000043 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000043 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "(S)oma, s(U)btracao, (M)ultiplicacao e (D)ivisao"
[SEQ 000043 | COL 70 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000044 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000044 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "(P)otencia"
[SEQ 000044 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000045 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000045 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "==>"
[SEQ 000045 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000046 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000046 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000046 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000048 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000048 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "VALOR 1:"
[SEQ 000048 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000049 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000049 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-1
[SEQ 000049 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000051 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000051 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "VALOR 2:"
[SEQ 000051 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000052 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000052 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-2
[SEQ 000052 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000054 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00002-EXECUTAR-OPERACOES
[SEQ 000054 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000055 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000055 | COL 17 (AREA B)] TOKEN_KEYWORD              : FUNCTION
[SEQ 000055 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : UPPER-CASE
[SEQ 000055 | COL 36 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000055 | COL 37 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000055 | COL 46 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000055 | COL 48 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000055 | COL 51 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000055 | COL 60 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000056 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000056 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000056 | COL 25 (AREA B)] TOKEN_EQ                   : =
[SEQ 000056 | COL 27 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "S"
[SEQ 000056 | COL 31 (AREA B)] TOKEN_KEYWORD              : OR
[SEQ 000056 | COL 34 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "s"
[SEQ 000057 | COL 15 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000057 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-1
[SEQ 000057 | COL 29 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000057 | COL 32 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-2
[SEQ 000057 | COL 42 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000057 | COL 49 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULTADO
[SEQ 000058 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000059 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000059 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000059 | COL 25 (AREA B)] TOKEN_EQ                   : =
[SEQ 000059 | COL 27 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "U"
[SEQ 000060 | COL 15 (AREA B)] TOKEN_KEYWORD              : SUBTRACT
[SEQ 000060 | COL 24 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-1
[SEQ 000060 | COL 34 (AREA B)] TOKEN_KEYWORD              : FROM
[SEQ 000060 | COL 39 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-2
[SEQ 000060 | COL 49 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000060 | COL 56 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULTADO
[SEQ 000061 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000062 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000062 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000062 | COL 25 (AREA B)] TOKEN_EQ                   : =
[SEQ 000062 | COL 27 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "M"
[SEQ 000063 | COL 15 (AREA B)] TOKEN_KEYWORD              : MULTIPLY
[SEQ 000063 | COL 24 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-1
[SEQ 000063 | COL 34 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000063 | COL 37 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-2
[SEQ 000063 | COL 47 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000063 | COL 54 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULTADO
[SEQ 000064 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000065 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000065 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000065 | COL 25 (AREA B)] TOKEN_EQ                   : =
[SEQ 000065 | COL 27 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "D"
[SEQ 000066 | COL 15 (AREA B)] TOKEN_KEYWORD              : DIVIDE
[SEQ 000066 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-1
[SEQ 000066 | COL 32 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000066 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-2
[SEQ 000066 | COL 45 (AREA B)] TOKEN_KEYWORD              : GIVING
[SEQ 000066 | COL 52 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULTADO
[SEQ 000067 | COL 42 (AREA B)] TOKEN_KEYWORD              : REMAINDER
[SEQ 000067 | COL 52 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESTO
[SEQ 000068 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000069 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000069 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000069 | COL 25 (AREA B)] TOKEN_EQ                   : =
[SEQ 000069 | COL 27 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "P"
[SEQ 000070 | COL 15 (AREA B)] TOKEN_KEYWORD              : COMPUTE
[SEQ 000070 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULTADO
[SEQ 000070 | COL 37 (AREA B)] TOKEN_EQ                   : =
[SEQ 000070 | COL 39 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-1
[SEQ 000070 | COL 49 (AREA B)] TOKEN_POWER                : **
[SEQ 000070 | COL 52 (AREA B)] TOKEN_IDENTIFIER           : WKS-NUM-2
[SEQ 000071 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000072 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000072 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "OPCAO INVALIDA"
[SEQ 000073 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000074 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000075 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000076 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000077 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000077 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000078 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000078 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WKS-OPCAO
[SEQ 000078 | COL 25 (AREA B)] TOKEN_EQ                   : =
[SEQ 000078 | COL 27 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "S"
[SEQ 000078 | COL 31 (AREA B)] TOKEN_KEYWORD              : OR
[SEQ 000078 | COL 34 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "U"
[SEQ 000078 | COL 38 (AREA B)] TOKEN_KEYWORD              : OR
[SEQ 000078 | COL 41 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "M"
[SEQ 000078 | COL 45 (AREA B)] TOKEN_KEYWORD              : OR
[SEQ 000078 | COL 48 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "D"
[SEQ 000078 | COL 52 (AREA B)] TOKEN_KEYWORD              : OR
[SEQ 000078 | COL 55 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "P"
[SEQ 000079 | COL 16 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000079 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULTADO
[SEQ 000079 | COL 35 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000079 | COL 38 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULT-FORMAT
[SEQ 000080 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000080 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "Resultado: "
[SEQ 000080 | COL 37 (AREA B)] TOKEN_IDENTIFIER           : WKS-RESULT-FORMAT
[SEQ 000081 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000081 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000083 | COL 08 (AREA A)] TOKEN_KEYWORD              : END
[SEQ 000083 | COL 12 (AREA B)] TOKEN_KEYWORD              : PROGRAM
[SEQ 000083 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : CALCULADORA
[SEQ 000083 | COL 31 (AREA B)] TOKEN_PERIOD               : .
```

</details>

<details>
<summary>prog004_folha_pgto.cob (221 tokens, exit 0)</summary>

```text
$ ./cbc -l cobol-exemplos/prog004_folha_pgto.cob
[SEQ 000011 | COL 08 (AREA A)] TOKEN_KEYWORD              : IDENTIFICATION
[SEQ 000011 | COL 23 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000011 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROGRAM-ID
[SEQ 000012 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : FOLHA-PGTO
[SEQ 000012 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000013 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATA
[SEQ 000013 | COL 13 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000013 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000014 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE
[SEQ 000014 | COL 13 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000014 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000015 | COL 08 (AREA A)] TOKEN_KEYWORD              : WORKING-STORAGE
[SEQ 000015 | COL 24 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000015 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000017 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000017 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : TAB-TRABALHADOR
[SEQ 000017 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000018 | COL 11 (AREA A)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000018 | COL 14 (AREA B)] TOKEN_IDENTIFIER           : TRABALHADOR
[SEQ 000018 | COL 26 (AREA B)] TOKEN_KEYWORD              : OCCURS
[SEQ 000018 | COL 33 (AREA B)] TOKEN_LITERAL_NUMERIC      : 3
[SEQ 000018 | COL 35 (AREA B)] TOKEN_KEYWORD              : TIMES
[SEQ 000018 | COL 41 (AREA B)] TOKEN_KEYWORD              : INDEXED
[SEQ 000018 | COL 49 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000018 | COL 52 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000018 | COL 55 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000019 | COL 14 (AREA B)] TOKEN_LITERAL_NUMERIC      : 03
[SEQ 000019 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : NOME
[SEQ 000019 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000019 | COL 37 (AREA B)] TOKEN_PICTURE              : X(5)
[SEQ 000019 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000020 | COL 14 (AREA B)] TOKEN_LITERAL_NUMERIC      : 03
[SEQ 000020 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : SALARIO
[SEQ 000020 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000020 | COL 37 (AREA B)] TOKEN_PICTURE              : 9(5)V99
[SEQ 000020 | COL 44 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000021 | COL 14 (AREA B)] TOKEN_LITERAL_NUMERIC      : 03
[SEQ 000021 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : DPTO
[SEQ 000021 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000021 | COL 37 (AREA B)] TOKEN_PICTURE              : X(2)
[SEQ 000021 | COL 42 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000021 | COL 49 (AREA B)] TOKEN_KEYWORD              : SPACES
[SEQ 000021 | COL 56 (AREA B)] TOKEN_KEYWORD              : OCCURS
[SEQ 000021 | COL 63 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000021 | COL 65 (AREA B)] TOKEN_KEYWORD              : TIMES
[SEQ 000021 | COL 70 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000023 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000023 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : IDX
[SEQ 000023 | COL 15 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000023 | COL 19 (AREA B)] TOKEN_PICTURE              : 9(1)
[SEQ 000023 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000025 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROCEDURE
[SEQ 000025 | COL 18 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000025 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000026 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : MAIN-PROCEDURE
[SEQ 000026 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000027 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000027 | COL 18 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "JOSEA1111111A1A4MARIA2222222A2  FABIO3333333A2A3"
[SEQ 000027 | COL 69 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000027 | COL 72 (AREA B)] TOKEN_IDENTIFIER           : TAB-TRABALHADOR
[SEQ 000028 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000029 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000029 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000029 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000030 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000030 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "====TAB-TRABALHADOR===="
[SEQ 000030 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000031 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000031 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "INITIAL VALUE"
[SEQ 000031 | COL 36 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000032 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000032 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : TAB-TRABALHADOR
[SEQ 000032 | COL 36 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000034 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000034 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000034 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000034 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000034 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000035 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000035 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000035 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000036 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000036 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "====00001-DISPLAY-TRABALHADOR===="
[SEQ 000036 | COL 56 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000037 | COL 13 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000037 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : 00001-DISPLAY-TRABALHADOR
[SEQ 000037 | COL 47 (AREA B)] TOKEN_KEYWORD              : UNTIL
[SEQ 000037 | COL 53 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000037 | COL 57 (AREA B)] TOKEN_GT                   : >
[SEQ 000037 | COL 59 (AREA B)] TOKEN_LITERAL_NUMERIC      : 3
[SEQ 000037 | COL 60 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000039 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000039 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000039 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000040 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000040 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "====00002-DISPLAY-BY-SEARCH===="
[SEQ 000040 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000041 | COL 13 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000041 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : 00002-DISPLAY-BY-SEARCH
[SEQ 000041 | COL 44 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000043 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000043 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000043 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000044 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000044 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "====00003-REMOVE-TRABALHADOR===="
[SEQ 000044 | COL 55 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000045 | COL 13 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000045 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : 00003-REMOVE-TRABALHADOR
[SEQ 000045 | COL 45 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000047 | COL 12 (AREA B)] TOKEN_KEYWORD              : STOP
[SEQ 000047 | COL 17 (AREA B)] TOKEN_KEYWORD              : RUN
[SEQ 000047 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000049 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00001-DISPLAY-TRABALHADOR
[SEQ 000049 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000050 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000050 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000050 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000052 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000052 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "IDX="
[SEQ 000052 | COL 27 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000052 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000053 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000053 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "TRABALHADOR(IDX)="
[SEQ 000053 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : TRABALHADOR
[SEQ 000053 | COL 51 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000053 | COL 52 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000053 | COL 55 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000053 | COL 56 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000054 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000054 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "NOME(IDX)="
[SEQ 000054 | COL 33 (AREA B)] TOKEN_IDENTIFIER           : NOME
[SEQ 000054 | COL 37 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000054 | COL 38 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000054 | COL 41 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000054 | COL 42 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000055 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000055 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SALARIO(IDX)="
[SEQ 000055 | COL 36 (AREA B)] TOKEN_IDENTIFIER           : SALARIO
[SEQ 000055 | COL 43 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000055 | COL 44 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000055 | COL 47 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000055 | COL 48 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000056 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000056 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "DPTO(IDX,1)="
[SEQ 000056 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : DPTO
[SEQ 000056 | COL 39 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000056 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000056 | COL 43 (AREA B)] TOKEN_COMMA                : ,
[SEQ 000056 | COL 44 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000056 | COL 45 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000056 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000057 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000057 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "DPTO(IDX,2)="
[SEQ 000057 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : DPTO
[SEQ 000057 | COL 39 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000057 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000057 | COL 43 (AREA B)] TOKEN_COMMA                : ,
[SEQ 000057 | COL 44 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000057 | COL 45 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000057 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000058 | COL 12 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000058 | COL 16 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000058 | COL 18 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000058 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000058 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000060 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00002-DISPLAY-BY-SEARCH
[SEQ 000060 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000062 | COL 12 (AREA B)] TOKEN_KEYWORD              : SET
[SEQ 000062 | COL 16 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000062 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000062 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000062 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000063 | COL 12 (AREA B)] TOKEN_KEYWORD              : SEARCH
[SEQ 000063 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : TRABALHADOR
[SEQ 000064 | COL 15 (AREA B)] TOKEN_KEYWORD              : AT
[SEQ 000064 | COL 18 (AREA B)] TOKEN_KEYWORD              : END
[SEQ 000064 | COL 22 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000064 | COL 30 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "TRABALHADOR NOT FOUND"
[SEQ 000065 | COL 15 (AREA B)] TOKEN_KEYWORD              : WHEN
[SEQ 000065 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : NOME
[SEQ 000065 | COL 24 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000065 | COL 25 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000065 | COL 28 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000065 | COL 30 (AREA B)] TOKEN_EQ                   : =
[SEQ 000065 | COL 32 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "MARIA"
[SEQ 000066 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000066 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "'WHEN NOME(TID) = MARIA'"
[SEQ 000067 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000067 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "TID="
[SEQ 000067 | COL 33 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000068 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000068 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "TRABALHADOR(TID)="
[SEQ 000068 | COL 46 (AREA B)] TOKEN_IDENTIFIER           : TRABALHADOR
[SEQ 000068 | COL 57 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000068 | COL 58 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000068 | COL 61 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000069 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-SEARCH
[SEQ 000069 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000072 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00003-REMOVE-TRABALHADOR
[SEQ 000072 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000073 | COL 12 (AREA B)] TOKEN_KEYWORD              : SET
[SEQ 000073 | COL 16 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000073 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000073 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000073 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000074 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000074 | COL 17 (AREA B)] TOKEN_KEYWORD              : LOW-VALUES
[SEQ 000074 | COL 28 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000074 | COL 31 (AREA B)] TOKEN_IDENTIFIER           : TRABALHADOR
[SEQ 000074 | COL 42 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000074 | COL 43 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000074 | COL 46 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000075 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000075 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : TAB-TRABALHADOR
[SEQ 000075 | COL 35 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000077 | COL 08 (AREA A)] TOKEN_KEYWORD              : END
[SEQ 000077 | COL 12 (AREA B)] TOKEN_KEYWORD              : PROGRAM
[SEQ 000077 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : FOLHA-PGTO
[SEQ 000077 | COL 30 (AREA B)] TOKEN_PERIOD               : .
```

</details>

<details>
<summary>prog005_strings.cob (236 tokens, exit 0)</summary>

```text
$ ./cbc -l cobol-exemplos/prog005_strings.cob
[SEQ 000011 | COL 08 (AREA A)] TOKEN_KEYWORD              : IDENTIFICATION
[SEQ 000011 | COL 23 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000011 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROGRAM-ID
[SEQ 000012 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG008_STRING
[SEQ 000012 | COL 34 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000013 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATA
[SEQ 000013 | COL 13 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000013 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000014 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE
[SEQ 000014 | COL 13 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000014 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000015 | COL 08 (AREA A)] TOKEN_KEYWORD              : WORKING-STORAGE
[SEQ 000015 | COL 24 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000015 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000017 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000017 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000017 | COL 27 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000017 | COL 31 (AREA B)] TOKEN_PICTURE              : X(15)
[SEQ 000017 | COL 36 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000018 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000018 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : CONT
[SEQ 000018 | COL 27 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000018 | COL 31 (AREA B)] TOKEN_PICTURE              : 9(2)
[SEQ 000018 | COL 35 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000019 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000019 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : YESNO
[SEQ 000019 | COL 27 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000019 | COL 31 (AREA B)] TOKEN_PICTURE              : 9
[SEQ 000019 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000021 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000021 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : IDX
[SEQ 000021 | COL 15 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000021 | COL 19 (AREA B)] TOKEN_PICTURE              : 9(1)
[SEQ 000021 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000023 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROCEDURE
[SEQ 000023 | COL 18 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000023 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000024 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : MAIN-PROCEDURE
[SEQ 000024 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000025 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000025 | COL 18 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "HELLO WORLD 123!!"
[SEQ 000025 | COL 38 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000025 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000025 | COL 45 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000027 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000027 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000027 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000028 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000028 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "==== EXEMPLO 1 ===="
[SEQ 000028 | COL 42 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000029 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000029 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SAUX='"
[SEQ 000029 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000029 | COL 35 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "'"
[SEQ 000029 | COL 38 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000031 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000031 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000031 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000031 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000031 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000032 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000032 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000032 | COL 26 (AREA B)] TOKEN_KEYWORD              : TALLYING
[SEQ 000032 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000032 | COL 40 (AREA B)] TOKEN_KEYWORD              : FOR
[SEQ 000032 | COL 44 (AREA B)] TOKEN_KEYWORD              : CHARACTERS
[SEQ 000032 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000033 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000033 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "CONT="
[SEQ 000033 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000033 | COL 34 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " - FOR CHARACTERS"
[SEQ 000033 | COL 53 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000035 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000035 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000035 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000035 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000035 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000036 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000036 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000036 | COL 26 (AREA B)] TOKEN_KEYWORD              : TALLYING
[SEQ 000036 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000036 | COL 40 (AREA B)] TOKEN_KEYWORD              : FOR
[SEQ 000036 | COL 44 (AREA B)] TOKEN_KEYWORD              : ALL
[SEQ 000036 | COL 48 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "L"
[SEQ 000036 | COL 51 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000037 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000037 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "CONT="
[SEQ 000037 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000037 | COL 34 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " - FOR ALL 'L'"
[SEQ 000037 | COL 50 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000039 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000039 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000039 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000039 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000039 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000040 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000040 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000040 | COL 26 (AREA B)] TOKEN_KEYWORD              : TALLYING
[SEQ 000040 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000041 | COL 21 (AREA B)] TOKEN_KEYWORD              : FOR
[SEQ 000041 | COL 25 (AREA B)] TOKEN_KEYWORD              : CHARACTERS
[SEQ 000041 | COL 36 (AREA B)] TOKEN_KEYWORD              : BEFORE
[SEQ 000041 | COL 43 (AREA B)] TOKEN_KEYWORD              : INITIAL
[SEQ 000041 | COL 51 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000041 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000042 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000042 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "CONT="
[SEQ 000042 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000042 | COL 34 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " - BEFORE INITIAL ' '"
[SEQ 000042 | COL 57 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000044 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000044 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000044 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000044 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000044 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000045 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000045 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000045 | COL 26 (AREA B)] TOKEN_KEYWORD              : TALLYING
[SEQ 000045 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000046 | COL 21 (AREA B)] TOKEN_KEYWORD              : FOR
[SEQ 000046 | COL 25 (AREA B)] TOKEN_KEYWORD              : CHARACTERS
[SEQ 000046 | COL 36 (AREA B)] TOKEN_KEYWORD              : AFTER
[SEQ 000046 | COL 42 (AREA B)] TOKEN_KEYWORD              : INITIAL
[SEQ 000046 | COL 50 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000046 | COL 53 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000047 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000047 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "CONT="
[SEQ 000047 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : CONT
[SEQ 000047 | COL 34 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " - AFTER INITIAL ' '"
[SEQ 000047 | COL 56 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000049 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000049 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000049 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000049 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : YESNO
[SEQ 000049 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000050 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000050 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000050 | COL 26 (AREA B)] TOKEN_KEYWORD              : TALLYING
[SEQ 000050 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : YESNO
[SEQ 000051 | COL 21 (AREA B)] TOKEN_KEYWORD              : FOR
[SEQ 000051 | COL 25 (AREA B)] TOKEN_KEYWORD              : LEADING
[SEQ 000051 | COL 33 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "H"
[SEQ 000051 | COL 36 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000052 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000052 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "YESNO="
[SEQ 000052 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : YESNO
[SEQ 000052 | COL 36 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " - FOR LEADING 'H' 0=N / 1=Y"
[SEQ 000052 | COL 66 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000054 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000054 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000054 | COL 26 (AREA B)] TOKEN_KEYWORD              : REPLACING
[SEQ 000054 | COL 36 (AREA B)] TOKEN_KEYWORD              : FIRST
[SEQ 000054 | COL 42 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "E"
[SEQ 000054 | COL 46 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000054 | COL 49 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "A"
[SEQ 000054 | COL 52 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000055 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000055 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SAUX='"
[SEQ 000055 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000055 | COL 35 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "' - REPLACING FIRST 'E' BY 'A'"
[SEQ 000055 | COL 67 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000057 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000057 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000057 | COL 26 (AREA B)] TOKEN_KEYWORD              : REPLACING
[SEQ 000057 | COL 36 (AREA B)] TOKEN_KEYWORD              : ALL
[SEQ 000057 | COL 40 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "LLO"
[SEQ 000057 | COL 46 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000057 | COL 49 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "PPY"
[SEQ 000057 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000058 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000058 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SAUX='"
[SEQ 000058 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000058 | COL 35 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "' - REPLACING ALL 'LLO' BY 'PPY'"
[SEQ 000058 | COL 69 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000060 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000060 | COL 18 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " HOLA-DAY"
[SEQ 000060 | COL 30 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000060 | COL 33 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000060 | COL 37 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000061 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000061 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000061 | COL 26 (AREA B)] TOKEN_KEYWORD              : REPLACING
[SEQ 000061 | COL 36 (AREA B)] TOKEN_KEYWORD              : ALL
[SEQ 000061 | COL 40 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "HOLA"
[SEQ 000061 | COL 47 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000061 | COL 50 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "GOOD"
[SEQ 000062 | COL 21 (AREA B)] TOKEN_KEYWORD              : AFTER
[SEQ 000062 | COL 27 (AREA B)] TOKEN_KEYWORD              : INITIAL
[SEQ 000062 | COL 35 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000062 | COL 39 (AREA B)] TOKEN_KEYWORD              : BEFORE
[SEQ 000062 | COL 46 (AREA B)] TOKEN_KEYWORD              : INITIAL
[SEQ 000062 | COL 54 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-"
[SEQ 000062 | COL 57 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000063 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000063 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SAUX='"
[SEQ 000063 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000063 | COL 35 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "' - HOLA-DAY TO GOOD-DAY'"
[SEQ 000063 | COL 62 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000065 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000065 | COL 18 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "HELLO WORLD"
[SEQ 000065 | COL 32 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000065 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000065 | COL 39 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000066 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000066 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000066 | COL 26 (AREA B)] TOKEN_KEYWORD              : CONVERTING
[SEQ 000066 | COL 37 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "ABCDEFGHIJKLMNOPQRSTUVWXZ"
[SEQ 000067 | COL 17 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000067 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "abcdefghijklmnopqrstuvwxz"
[SEQ 000067 | COL 47 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000068 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000068 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SAUX='"
[SEQ 000068 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000068 | COL 35 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "' - CONVERTING'"
[SEQ 000068 | COL 52 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000070 | COL 13 (AREA B)] TOKEN_KEYWORD              : INSPECT
[SEQ 000070 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000070 | COL 26 (AREA B)] TOKEN_KEYWORD              : REPLACING
[SEQ 000070 | COL 36 (AREA B)] TOKEN_KEYWORD              : CHARACTERS
[SEQ 000070 | COL 47 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000070 | COL 50 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "F"
[SEQ 000070 | COL 53 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000071 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000071 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "SAUX='"
[SEQ 000071 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000071 | COL 35 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "' - REPLACING "
[SEQ 000071 | COL 51 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000073 | COL 12 (AREA B)] TOKEN_KEYWORD              : STOP
[SEQ 000073 | COL 17 (AREA B)] TOKEN_KEYWORD              : RUN
[SEQ 000073 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000075 | COL 08 (AREA A)] TOKEN_KEYWORD              : END
[SEQ 000075 | COL 12 (AREA B)] TOKEN_KEYWORD              : PROGRAM
[SEQ 000075 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG008_STRING
[SEQ 000075 | COL 34 (AREA B)] TOKEN_PERIOD               : .
```

</details>

<details>
<summary>prog006_files.cob (225 tokens, exit 0)</summary>

```text
$ ./cbc -l cobol-exemplos/prog006_files.cob
[SEQ 000011 | COL 08 (AREA A)] TOKEN_KEYWORD              : IDENTIFICATION
[SEQ 000011 | COL 23 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000011 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROGRAM-ID
[SEQ 000012 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG006_FILES
[SEQ 000012 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000013 | COL 08 (AREA A)] TOKEN_KEYWORD              : ENVIRONMENT
[SEQ 000013 | COL 20 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000013 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000014 | COL 08 (AREA A)] TOKEN_KEYWORD              : INPUT-OUTPUT
[SEQ 000014 | COL 21 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000014 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000016 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE-CONTROL
[SEQ 000016 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000017 | COL 12 (AREA B)] TOKEN_KEYWORD              : SELECT
[SEQ 000017 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : MY-FILE
[SEQ 000017 | COL 27 (AREA B)] TOKEN_KEYWORD              : ASSIGN
[SEQ 000017 | COL 34 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000017 | COL 37 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "DADOS.TXT"
[SEQ 000017 | COL 48 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000019 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATA
[SEQ 000019 | COL 13 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000019 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000020 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE
[SEQ 000020 | COL 13 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000020 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000023 | COL 08 (AREA A)] TOKEN_KEYWORD              : FD
[SEQ 000023 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : MY-FILE
[SEQ 000023 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000024 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000024 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : RC-TRABALHADOR
[SEQ 000024 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000025 | COL 11 (AREA A)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000025 | COL 14 (AREA B)] TOKEN_IDENTIFIER           : RC-TID
[SEQ 000025 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000025 | COL 37 (AREA B)] TOKEN_PICTURE              : 9(5)
[SEQ 000025 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000026 | COL 11 (AREA A)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000026 | COL 14 (AREA B)] TOKEN_IDENTIFIER           : RC-NOME
[SEQ 000026 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000026 | COL 37 (AREA B)] TOKEN_PICTURE              : X(10)
[SEQ 000026 | COL 42 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000027 | COL 11 (AREA A)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000027 | COL 14 (AREA B)] TOKEN_IDENTIFIER           : RC-SALARIO
[SEQ 000027 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000027 | COL 37 (AREA B)] TOKEN_PICTURE              : 9(10)V99
[SEQ 000027 | COL 45 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000028 | COL 11 (AREA A)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000028 | COL 14 (AREA B)] TOKEN_IDENTIFIER           : RC-DPTO-1
[SEQ 000028 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000028 | COL 37 (AREA B)] TOKEN_PICTURE              : X(2)
[SEQ 000028 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000029 | COL 11 (AREA A)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000029 | COL 14 (AREA B)] TOKEN_IDENTIFIER           : RC-DPTO-2
[SEQ 000029 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000029 | COL 37 (AREA B)] TOKEN_PICTURE              : X(2)
[SEQ 000029 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000031 | COL 08 (AREA A)] TOKEN_KEYWORD              : WORKING-STORAGE
[SEQ 000031 | COL 24 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000031 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000033 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000033 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : TAB-TRABALHADOR
[SEQ 000033 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000034 | COL 11 (AREA A)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000034 | COL 14 (AREA B)] TOKEN_IDENTIFIER           : TRABALHADOR
[SEQ 000034 | COL 26 (AREA B)] TOKEN_KEYWORD              : OCCURS
[SEQ 000034 | COL 33 (AREA B)] TOKEN_LITERAL_NUMERIC      : 3
[SEQ 000034 | COL 35 (AREA B)] TOKEN_KEYWORD              : TIMES
[SEQ 000034 | COL 41 (AREA B)] TOKEN_KEYWORD              : INDEXED
[SEQ 000034 | COL 49 (AREA B)] TOKEN_KEYWORD              : BY
[SEQ 000034 | COL 52 (AREA B)] TOKEN_IDENTIFIER           : TID
[SEQ 000034 | COL 55 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000035 | COL 14 (AREA B)] TOKEN_LITERAL_NUMERIC      : 03
[SEQ 000035 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : NOME
[SEQ 000035 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000035 | COL 37 (AREA B)] TOKEN_PICTURE              : X(5)
[SEQ 000035 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000036 | COL 14 (AREA B)] TOKEN_LITERAL_NUMERIC      : 03
[SEQ 000036 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : SALARIO
[SEQ 000036 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000036 | COL 37 (AREA B)] TOKEN_PICTURE              : 9(5)V99
[SEQ 000036 | COL 44 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000037 | COL 14 (AREA B)] TOKEN_LITERAL_NUMERIC      : 03
[SEQ 000037 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : DPTO
[SEQ 000037 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000037 | COL 37 (AREA B)] TOKEN_PICTURE              : X(2)
[SEQ 000037 | COL 42 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000037 | COL 49 (AREA B)] TOKEN_KEYWORD              : SPACES
[SEQ 000037 | COL 56 (AREA B)] TOKEN_KEYWORD              : OCCURS
[SEQ 000037 | COL 63 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000037 | COL 65 (AREA B)] TOKEN_KEYWORD              : TIMES
[SEQ 000037 | COL 70 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000039 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000039 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : SAUX
[SEQ 000039 | COL 27 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000039 | COL 31 (AREA B)] TOKEN_PICTURE              : X(15)
[SEQ 000039 | COL 36 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000040 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000040 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : CONT
[SEQ 000040 | COL 27 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000040 | COL 31 (AREA B)] TOKEN_PICTURE              : 9(2)
[SEQ 000040 | COL 35 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000041 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000041 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : YESNO
[SEQ 000041 | COL 27 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000041 | COL 31 (AREA B)] TOKEN_PICTURE              : 9
[SEQ 000041 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000043 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000043 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : IDX
[SEQ 000043 | COL 15 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000043 | COL 19 (AREA B)] TOKEN_PICTURE              : 9(1)
[SEQ 000043 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000045 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROCEDURE
[SEQ 000045 | COL 18 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000045 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000046 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : MAIN-PROCEDURE
[SEQ 000046 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000048 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000048 | COL 18 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "JOSEA1111111A1A4MARIA2222222A2  FABIO3333333A2A3"
[SEQ 000048 | COL 69 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000048 | COL 72 (AREA B)] TOKEN_IDENTIFIER           : TAB-TRABALHADOR
[SEQ 000049 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000051 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000051 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000051 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000052 | COL 13 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000052 | COL 21 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "====00001-DISPLAY-TRABALHADOR===="
[SEQ 000052 | COL 56 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000053 | COL 13 (AREA B)] TOKEN_KEYWORD              : OPEN
[SEQ 000053 | COL 18 (AREA B)] TOKEN_KEYWORD              : OUTPUT
[SEQ 000053 | COL 25 (AREA B)] TOKEN_IDENTIFIER           : MY-FILE
[SEQ 000053 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000055 | COL 13 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000055 | COL 18 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000055 | COL 20 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000055 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000055 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000056 | COL 13 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000056 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : 00001-WRITE-TO-FILE
[SEQ 000056 | COL 41 (AREA B)] TOKEN_KEYWORD              : UNTIL
[SEQ 000056 | COL 47 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000056 | COL 51 (AREA B)] TOKEN_GT                   : >
[SEQ 000056 | COL 53 (AREA B)] TOKEN_LITERAL_NUMERIC      : 3
[SEQ 000056 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000058 | COL 13 (AREA B)] TOKEN_KEYWORD              : CLOSE
[SEQ 000058 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : MY-FILE
[SEQ 000058 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000060 | COL 12 (AREA B)] TOKEN_KEYWORD              : STOP
[SEQ 000060 | COL 17 (AREA B)] TOKEN_KEYWORD              : RUN
[SEQ 000060 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000062 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00001-WRITE-TO-FILE
[SEQ 000062 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000063 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000063 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000063 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000065 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000065 | COL 17 (AREA B)] TOKEN_KEYWORD              : LOW-VALUES
[SEQ 000065 | COL 28 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000065 | COL 31 (AREA B)] TOKEN_IDENTIFIER           : RC-TRABALHADOR
[SEQ 000065 | COL 45 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000066 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000066 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "IDX="
[SEQ 000066 | COL 27 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000066 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000067 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000067 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "TRABALHADOR(IDX)="
[SEQ 000067 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : TRABALHADOR
[SEQ 000067 | COL 51 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000067 | COL 52 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000067 | COL 55 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000067 | COL 56 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000068 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000068 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000068 | COL 21 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000068 | COL 24 (AREA B)] TOKEN_IDENTIFIER           : RC-TID
[SEQ 000068 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000069 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000069 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : NOME
[SEQ 000069 | COL 21 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000069 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000069 | COL 25 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000069 | COL 27 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000069 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : RC-NOME
[SEQ 000069 | COL 37 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000070 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000070 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : SALARIO
[SEQ 000070 | COL 24 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000070 | COL 25 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000070 | COL 28 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000070 | COL 30 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000070 | COL 33 (AREA B)] TOKEN_IDENTIFIER           : RC-SALARIO
[SEQ 000070 | COL 43 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000071 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000071 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : DPTO
[SEQ 000071 | COL 21 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000071 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000071 | COL 25 (AREA B)] TOKEN_COMMA                : ,
[SEQ 000071 | COL 26 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000071 | COL 27 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000071 | COL 29 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000071 | COL 32 (AREA B)] TOKEN_IDENTIFIER           : RC-DPTO-1
[SEQ 000071 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000072 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000072 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : DPTO
[SEQ 000072 | COL 21 (AREA B)] TOKEN_LPAREN               : (
[SEQ 000072 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000072 | COL 25 (AREA B)] TOKEN_COMMA                : ,
[SEQ 000072 | COL 26 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000072 | COL 27 (AREA B)] TOKEN_RPAREN               : )
[SEQ 000072 | COL 29 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000072 | COL 32 (AREA B)] TOKEN_IDENTIFIER           : RC-DPTO-2
[SEQ 000072 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000073 | COL 12 (AREA B)] TOKEN_KEYWORD              : WRITE
[SEQ 000073 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : RC-TRABALHADOR
[SEQ 000073 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000074 | COL 12 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000074 | COL 16 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000074 | COL 18 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000074 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : IDX
[SEQ 000074 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000076 | COL 08 (AREA A)] TOKEN_KEYWORD              : END
[SEQ 000076 | COL 12 (AREA B)] TOKEN_KEYWORD              : PROGRAM
[SEQ 000076 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG006_FILES
[SEQ 000076 | COL 33 (AREA B)] TOKEN_PERIOD               : .
```

</details>

<details>
<summary>prog007_loja.cob (566 tokens, exit 0)</summary>

```text
$ ./cbc -l cobol-exemplos/prog007_loja.cob
[SEQ 000011 | COL 08 (AREA A)] TOKEN_KEYWORD              : IDENTIFICATION
[SEQ 000011 | COL 23 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000011 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROGRAM-ID
[SEQ 000012 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000012 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG007_LOJA
[SEQ 000012 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000014 | COL 08 (AREA A)] TOKEN_KEYWORD              : ENVIRONMENT
[SEQ 000014 | COL 20 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000014 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000015 | COL 08 (AREA A)] TOKEN_KEYWORD              : INPUT-OUTPUT
[SEQ 000015 | COL 21 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000015 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000017 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE-CONTROL
[SEQ 000017 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000018 | COL 12 (AREA B)] TOKEN_KEYWORD              : SELECT
[SEQ 000018 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS
[SEQ 000018 | COL 31 (AREA B)] TOKEN_KEYWORD              : ASSIGN
[SEQ 000018 | COL 38 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000018 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WS-PROD-FILENAME
[SEQ 000019 | COL 31 (AREA B)] TOKEN_KEYWORD              : ORGANIZATION
[SEQ 000019 | COL 44 (AREA B)] TOKEN_KEYWORD              : INDEXED
[SEQ 000020 | COL 31 (AREA B)] TOKEN_KEYWORD              : ACCESS
[SEQ 000020 | COL 38 (AREA B)] TOKEN_KEYWORD              : MODE
[SEQ 000020 | COL 43 (AREA B)] TOKEN_KEYWORD              : DYNAMIC
[SEQ 000021 | COL 31 (AREA B)] TOKEN_KEYWORD              : RECORD
[SEQ 000021 | COL 38 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000021 | COL 42 (AREA B)] TOKEN_KEYWORD              : IS
[SEQ 000021 | COL 45 (AREA B)] TOKEN_IDENTIFIER           : RP-PID
[SEQ 000022 | COL 31 (AREA B)] TOKEN_KEYWORD              : ALTERNATE
[SEQ 000022 | COL 41 (AREA B)] TOKEN_KEYWORD              : RECORD
[SEQ 000022 | COL 48 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000022 | COL 52 (AREA B)] TOKEN_KEYWORD              : IS
[SEQ 000022 | COL 55 (AREA B)] TOKEN_IDENTIFIER           : RP-NOME
[SEQ 000023 | COL 31 (AREA B)] TOKEN_KEYWORD              : FILE
[SEQ 000023 | COL 36 (AREA B)] TOKEN_KEYWORD              : STATUS
[SEQ 000023 | COL 43 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000023 | COL 62 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000026 | COL 12 (AREA B)] TOKEN_KEYWORD              : SELECT
[SEQ 000026 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS-KEY
[SEQ 000026 | COL 35 (AREA B)] TOKEN_KEYWORD              : ASSIGN
[SEQ 000026 | COL 42 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000026 | COL 45 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "PROG10-001.KEY"
[SEQ 000027 | COL 31 (AREA B)] TOKEN_KEYWORD              : FILE
[SEQ 000027 | COL 36 (AREA B)] TOKEN_KEYWORD              : STATUS
[SEQ 000027 | COL 43 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000027 | COL 66 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000029 | COL 08 (AREA A)] TOKEN_KEYWORD              : DATA
[SEQ 000029 | COL 13 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000029 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000030 | COL 08 (AREA A)] TOKEN_KEYWORD              : FILE
[SEQ 000030 | COL 13 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000030 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000033 | COL 08 (AREA A)] TOKEN_KEYWORD              : FD
[SEQ 000033 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : FD-PRODUTOS
[SEQ 000034 | COL 11 (AREA A)] TOKEN_KEYWORD              : BLOCK
[SEQ 000034 | COL 17 (AREA B)] TOKEN_KEYWORD              : CONTAINS
[SEQ 000034 | COL 26 (AREA B)] TOKEN_LITERAL_NUMERIC      : 874
[SEQ 000034 | COL 30 (AREA B)] TOKEN_KEYWORD              : RECORDS
[SEQ 000035 | COL 11 (AREA A)] TOKEN_KEYWORD              : RECORD
[SEQ 000035 | COL 18 (AREA B)] TOKEN_KEYWORD              : CONTAINS
[SEQ 000035 | COL 27 (AREA B)] TOKEN_LITERAL_NUMERIC      : 64
[SEQ 000035 | COL 30 (AREA B)] TOKEN_KEYWORD              : CHARACTERS
[SEQ 000035 | COL 40 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000046 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000046 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : REG-PRODUTO
[SEQ 000046 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000047 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000047 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : RP-PID
[SEQ 000047 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000047 | COL 38 (AREA B)] TOKEN_PICTURE              : 9(10)
[SEQ 000047 | COL 43 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000048 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000048 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : RP-NOME
[SEQ 000048 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000048 | COL 38 (AREA B)] TOKEN_PICTURE              : X(10)
[SEQ 000048 | COL 43 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000049 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000049 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : RP-VALOR
[SEQ 000049 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000049 | COL 38 (AREA B)] TOKEN_PICTURE              : 9(10)V9(02)
[SEQ 000049 | COL 49 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000050 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000050 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : RP-QTD
[SEQ 000050 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000050 | COL 38 (AREA B)] TOKEN_PICTURE              : 9(10)V9(02)
[SEQ 000050 | COL 49 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000051 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000051 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : RP-DESCRICAO
[SEQ 000051 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000051 | COL 38 (AREA B)] TOKEN_PICTURE              : X(20)
[SEQ 000051 | COL 43 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000054 | COL 08 (AREA A)] TOKEN_KEYWORD              : FD
[SEQ 000054 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : FD-PRODUTOS-KEY
[SEQ 000054 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000055 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000055 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : REGISTRO-CHAVE
[SEQ 000055 | COL 33 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000055 | COL 37 (AREA B)] TOKEN_PICTURE              : 9(10)
[SEQ 000055 | COL 42 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000059 | COL 08 (AREA A)] TOKEN_KEYWORD              : WORKING-STORAGE
[SEQ 000059 | COL 24 (AREA B)] TOKEN_KEYWORD              : SECTION
[SEQ 000059 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000062 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000062 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : WS-PROD-FILENAME
[SEQ 000062 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000063 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000063 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : NAME
[SEQ 000063 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000063 | COL 38 (AREA B)] TOKEN_PICTURE              : X(7)
[SEQ 000063 | COL 43 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000063 | COL 50 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "PROG10-"
[SEQ 000063 | COL 59 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000064 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000064 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : NUM
[SEQ 000064 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000064 | COL 38 (AREA B)] TOKEN_PICTURE              : 9(3)
[SEQ 000064 | COL 43 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000064 | COL 50 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000064 | COL 51 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000065 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000065 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : EXT
[SEQ 000065 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000065 | COL 38 (AREA B)] TOKEN_PICTURE              : X(4)
[SEQ 000065 | COL 43 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000065 | COL 50 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : ".DAT"
[SEQ 000065 | COL 56 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000067 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000067 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000067 | COL 37 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000067 | COL 41 (AREA B)] TOKEN_PICTURE              : 9(02)
[SEQ 000067 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000068 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000068 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000068 | COL 37 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000068 | COL 41 (AREA B)] TOKEN_PICTURE              : 9(02)
[SEQ 000068 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000070 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 01
[SEQ 000070 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : WS-PRODUTO
[SEQ 000070 | COL 21 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000071 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000071 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-PID
[SEQ 000071 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000071 | COL 38 (AREA B)] TOKEN_PICTURE              : 9(10)
[SEQ 000071 | COL 43 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000072 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000072 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-NOME
[SEQ 000072 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000072 | COL 38 (AREA B)] TOKEN_PICTURE              : X(10)
[SEQ 000072 | COL 43 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000073 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000073 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-VALOR
[SEQ 000073 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000073 | COL 38 (AREA B)] TOKEN_PICTURE              : 9(10)V9(02)
[SEQ 000073 | COL 49 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000074 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000074 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-QTD
[SEQ 000074 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000074 | COL 38 (AREA B)] TOKEN_PICTURE              : 9(10)V9(02)
[SEQ 000074 | COL 49 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000075 | COL 12 (AREA B)] TOKEN_LITERAL_NUMERIC      : 02
[SEQ 000075 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-DESCRICAO
[SEQ 000075 | COL 34 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000075 | COL 38 (AREA B)] TOKEN_PICTURE              : X(20)
[SEQ 000075 | COL 43 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000078 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000078 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : WS-OPCAO
[SEQ 000078 | COL 43 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000078 | COL 47 (AREA B)] TOKEN_PICTURE              : 9(01)
[SEQ 000078 | COL 52 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000079 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000079 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000079 | COL 43 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000079 | COL 47 (AREA B)] TOKEN_PICTURE              : 9(10)
[SEQ 000079 | COL 52 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000080 | COL 08 (AREA A)] TOKEN_LITERAL_NUMERIC      : 77
[SEQ 000080 | COL 11 (AREA A)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000080 | COL 43 (AREA B)] TOKEN_KEYWORD              : PIC
[SEQ 000080 | COL 47 (AREA B)] TOKEN_PICTURE              : 9(01)
[SEQ 000080 | COL 53 (AREA B)] TOKEN_KEYWORD              : VALUES
[SEQ 000080 | COL 60 (AREA B)] TOKEN_KEYWORD              : ZERO
[SEQ 000080 | COL 64 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000084 | COL 08 (AREA A)] TOKEN_KEYWORD              : PROCEDURE
[SEQ 000084 | COL 18 (AREA B)] TOKEN_KEYWORD              : DIVISION
[SEQ 000084 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000085 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : MAIN-PROCEDURE
[SEQ 000085 | COL 22 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000087 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000087 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "PERFORM 00000-OPEN-FILE-PRODUTOS-KEY"
[SEQ 000087 | COL 58 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000088 | COL 12 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000088 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : 00000-OPEN-FILE-PRODUTOS-KEY
[SEQ 000088 | COL 48 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000090 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000090 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000090 | COL 23 (AREA B)] TOKEN_EQ                   : =
[SEQ 000090 | COL 25 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000091 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000091 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "PERFORM 00000-OPEN-FILE-PRODUTOS"
[SEQ 000092 | COL 15 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000092 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : 00000-OPEN-FILE-PRODUTOS
[SEQ 000093 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-IF
[SEQ 000093 | COL 18 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000095 | COL 12 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000095 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : 00001-MENU-OPCOES
[SEQ 000095 | COL 38 (AREA B)] TOKEN_KEYWORD              : UNTIL
[SEQ 000095 | COL 44 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000095 | COL 52 (AREA B)] TOKEN_EQ                   : =
[SEQ 000095 | COL 54 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000095 | COL 55 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000097 | COL 12 (AREA B)] TOKEN_KEYWORD              : WRITE
[SEQ 000097 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : REGISTRO-CHAVE
[SEQ 000097 | COL 33 (AREA B)] TOKEN_KEYWORD              : FROM
[SEQ 000097 | COL 38 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000097 | COL 51 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000098 | COL 12 (AREA B)] TOKEN_KEYWORD              : CLOSE
[SEQ 000098 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS-KEY
[SEQ 000098 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000099 | COL 12 (AREA B)] TOKEN_KEYWORD              : CLOSE
[SEQ 000099 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS
[SEQ 000099 | COL 29 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000100 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000100 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "BYE"
[SEQ 000101 | COL 12 (AREA B)] TOKEN_KEYWORD              : STOP
[SEQ 000101 | COL 17 (AREA B)] TOKEN_KEYWORD              : RUN
[SEQ 000101 | COL 20 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000103 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00000-OPEN-FILE-PRODUTOS-KEY
[SEQ 000103 | COL 36 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000105 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000105 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000105 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000105 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000105 | COL 45 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000106 | COL 12 (AREA B)] TOKEN_KEYWORD              : OPEN
[SEQ 000106 | COL 17 (AREA B)] TOKEN_KEYWORD              : INPUT
[SEQ 000106 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS-KEY
[SEQ 000106 | COL 38 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000107 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000107 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000107 | COL 39 (AREA B)] TOKEN_EQ                   : =
[SEQ 000107 | COL 41 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000108 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000108 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- PROG10-001.KEY FOUND"
[SEQ 000109 | COL 15 (AREA B)] TOKEN_KEYWORD              : READ
[SEQ 000109 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS-KEY
[SEQ 000109 | COL 36 (AREA B)] TOKEN_KEYWORD              : INTO
[SEQ 000109 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000110 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000111 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000111 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000111 | COL 39 (AREA B)] TOKEN_EQ                   : =
[SEQ 000111 | COL 41 (AREA B)] TOKEN_LITERAL_NUMERIC      : 35
[SEQ 000112 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000112 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- PROG10-001.KEY NOT FOUND - CREATING NEW"
[SEQ 000113 | COL 15 (AREA B)] TOKEN_KEYWORD              : OPEN
[SEQ 000113 | COL 20 (AREA B)] TOKEN_KEYWORD              : OUTPUT
[SEQ 000113 | COL 27 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS-KEY
[SEQ 000114 | COL 15 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000114 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000114 | COL 42 (AREA B)] TOKEN_EQ                   : =
[SEQ 000114 | COL 44 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000115 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000115 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000115 | COL 25 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000115 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000116 | COL 15 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000117 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000117 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- ERRO AO CRIAR ARQUIVO DE CHAVES -->"
[SEQ 000118 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000119 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000119 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000119 | COL 25 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000119 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000120 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000121 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000121 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- ERRO AO ABRIR ARQUIVO DE CHAVES -->"
[SEQ 000122 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD-KEY
[SEQ 000123 | COL 15 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000123 | COL 20 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000123 | COL 22 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000123 | COL 25 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000123 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000125 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00000-OPEN-FILE-PRODUTOS
[SEQ 000125 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000126 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000126 | COL 17 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000126 | COL 19 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000126 | COL 22 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000126 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000127 | COL 12 (AREA B)] TOKEN_KEYWORD              : OPEN
[SEQ 000127 | COL 17 (AREA B)] TOKEN_KEYWORD              : I-O
[SEQ 000127 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS
[SEQ 000127 | COL 32 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000129 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000129 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000129 | COL 35 (AREA B)] TOKEN_EQ                   : =
[SEQ 000129 | COL 37 (AREA B)] TOKEN_LITERAL_NUMERIC      : 35
[SEQ 000130 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000130 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- PROG10-001.DAT NOT FOUND - CREATING NEW"
[SEQ 000131 | COL 15 (AREA B)] TOKEN_KEYWORD              : OPEN
[SEQ 000131 | COL 20 (AREA B)] TOKEN_KEYWORD              : OUTPUT
[SEQ 000131 | COL 27 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS
[SEQ 000132 | COL 15 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000132 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000132 | COL 38 (AREA B)] TOKEN_EQ                   : =
[SEQ 000132 | COL 40 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000133 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000133 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000133 | COL 25 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000133 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000134 | COL 15 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000135 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000135 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- ERRO AO CRIAR ARQUIVO DE DADOS -->"
[SEQ 000136 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000137 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000137 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000137 | COL 25 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000137 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000138 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000139 | COL 12 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000139 | COL 15 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000139 | COL 35 (AREA B)] TOKEN_EQ                   : =
[SEQ 000139 | COL 37 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000140 | COL 15 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000140 | COL 23 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- PROG10-001.DAT FOUND"
[SEQ 000141 | COL 15 (AREA B)] TOKEN_KEYWORD              : IF
[SEQ 000141 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000141 | COL 32 (AREA B)] TOKEN_EQ                   : =
[SEQ 000141 | COL 34 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000142 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000142 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 0
[SEQ 000142 | COL 25 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000142 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000143 | COL 15 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000144 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000144 | COL 23 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000144 | COL 37 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000144 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : RP-PID
[SEQ 000146 | COL 18 (AREA B)] TOKEN_KEYWORD              : START
[SEQ 000146 | COL 24 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS
[SEQ 000146 | COL 36 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000146 | COL 40 (AREA B)] TOKEN_KEYWORD              : EQUAL
[SEQ 000146 | COL 46 (AREA B)] TOKEN_IDENTIFIER           : RP-PID
[SEQ 000147 | COL 22 (AREA B)] TOKEN_KEYWORD              : INVALID
[SEQ 000147 | COL 30 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000148 | COL 25 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000148 | COL 33 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "ERRO AO REGISTRO"
[SEQ 000149 | COL 12 (AREA B)] TOKEN_KEYWORD              : ELSE
[SEQ 000150 | COL 14 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000150 | COL 22 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "-- ERRO AO ABRIR ARQUIVO DE PRODUTOS"
[SEQ 000151 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : WS-FILE-STATUS-PROD
[SEQ 000152 | COL 14 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000152 | COL 19 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000152 | COL 21 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000152 | COL 24 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000152 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000155 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00001-MENU-OPCOES
[SEQ 000155 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000156 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000156 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000156 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000157 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000157 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "====00001-MENU-OPCOES===="
[SEQ 000157 | COL 47 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000158 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000158 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "OPCOES:"
[SEQ 000158 | COL 29 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000159 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000159 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "1) ADICIONA PRODUTO"
[SEQ 000159 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000160 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000160 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "2) REMOVE PRODUTO - NOT IMPLEMENTED"
[SEQ 000160 | COL 57 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000161 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000161 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "3) ATUALIZA PRODUTO - NOT IMPLEMENTED"
[SEQ 000161 | COL 59 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000162 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000162 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "4) CONSULTA PRODUTO"
[SEQ 000162 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000163 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000163 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000163 | COL 23 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000164 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000164 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "5) SAIR DO PROGRAMA"
[SEQ 000164 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000165 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000165 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : WS-OPCAO
[SEQ 000165 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000167 | COL 12 (AREA B)] TOKEN_KEYWORD              : EVALUATE
[SEQ 000167 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : WS-OPCAO
[SEQ 000168 | COL 15 (AREA B)] TOKEN_KEYWORD              : WHEN
[SEQ 000168 | COL 20 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000168 | COL 22 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000168 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : 00002-CREATE-PRODUTO
[SEQ 000169 | COL 15 (AREA B)] TOKEN_KEYWORD              : WHEN
[SEQ 000169 | COL 20 (AREA B)] TOKEN_LITERAL_NUMERIC      : 2
[SEQ 000170 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000170 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "DELETE"
[SEQ 000171 | COL 15 (AREA B)] TOKEN_KEYWORD              : WHEN
[SEQ 000171 | COL 20 (AREA B)] TOKEN_LITERAL_NUMERIC      : 3
[SEQ 000172 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000172 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "UPDATE"
[SEQ 000173 | COL 15 (AREA B)] TOKEN_KEYWORD              : WHEN
[SEQ 000173 | COL 20 (AREA B)] TOKEN_LITERAL_NUMERIC      : 4
[SEQ 000173 | COL 22 (AREA B)] TOKEN_KEYWORD              : PERFORM
[SEQ 000173 | COL 30 (AREA B)] TOKEN_IDENTIFIER           : 00002-SELECT-PRODUTO
[SEQ 000174 | COL 15 (AREA B)] TOKEN_KEYWORD              : WHEN
[SEQ 000174 | COL 20 (AREA B)] TOKEN_LITERAL_NUMERIC      : 5
[SEQ 000175 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000175 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000175 | COL 25 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000175 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000176 | COL 15 (AREA B)] TOKEN_KEYWORD              : WHEN
[SEQ 000176 | COL 20 (AREA B)] TOKEN_KEYWORD              : OTHER
[SEQ 000177 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000177 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "OPCAO INVALIDA."
[SEQ 000178 | COL 12 (AREA B)] TOKEN_KEYWORD              : END-EVALUATE
[SEQ 000178 | COL 24 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000180 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00002-CREATE-PRODUTO
[SEQ 000180 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000181 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000181 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "CRIA UM NOVO PRODUTO"
[SEQ 000181 | COL 42 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000182 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000182 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "NOME: "
[SEQ 000182 | COL 29 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000182 | COL 34 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000182 | COL 37 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000182 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000183 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000183 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WS-NOME
[SEQ 000183 | COL 26 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000184 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000184 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "VALOR: "
[SEQ 000184 | COL 30 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000184 | COL 35 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000184 | COL 38 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000184 | COL 47 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000185 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000185 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WS-VALOR
[SEQ 000185 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000186 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000186 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "QUANTIDADE: "
[SEQ 000186 | COL 35 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000186 | COL 40 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000186 | COL 43 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000186 | COL 52 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000187 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000187 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WS-QTD
[SEQ 000187 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000188 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000188 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "DESCRICAO: "
[SEQ 000188 | COL 34 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000188 | COL 39 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000188 | COL 42 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000188 | COL 51 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000189 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000189 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WS-DESCRICAO
[SEQ 000189 | COL 31 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000191 | COL 12 (AREA B)] TOKEN_KEYWORD              : ADD
[SEQ 000191 | COL 16 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000191 | COL 18 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000191 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000191 | COL 34 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000192 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000192 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000192 | COL 31 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000192 | COL 34 (AREA B)] TOKEN_IDENTIFIER           : WS-PID
[SEQ 000192 | COL 40 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000194 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000194 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "--------------"
[SEQ 000195 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000195 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : WS-PRODUTO
[SEQ 000195 | COL 30 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000196 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000196 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "--------------"
[SEQ 000198 | COL 12 (AREA B)] TOKEN_KEYWORD              : WRITE
[SEQ 000198 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : REG-PRODUTO
[SEQ 000198 | COL 30 (AREA B)] TOKEN_KEYWORD              : FROM
[SEQ 000198 | COL 35 (AREA B)] TOKEN_IDENTIFIER           : WS-PRODUTO
[SEQ 000199 | COL 15 (AREA B)] TOKEN_KEYWORD              : INVALID
[SEQ 000199 | COL 23 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000200 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000200 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "CODIGO DE PRODUTO JAH EXISTE"
[SEQ 000200 | COL 57 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000201 | COL 18 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000201 | COL 23 (AREA B)] TOKEN_LITERAL_NUMERIC      : 1
[SEQ 000201 | COL 25 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000201 | COL 28 (AREA B)] TOKEN_IDENTIFIER           : WS-EXIT
[SEQ 000202 | COL 15 (AREA B)] TOKEN_KEYWORD              : NOT
[SEQ 000202 | COL 19 (AREA B)] TOKEN_KEYWORD              : INVALID
[SEQ 000202 | COL 27 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000203 | COL 18 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000203 | COL 26 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "PRODUTO ARMAZENADO"
[SEQ 000203 | COL 46 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000205 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : 00002-SELECT-PRODUTO
[SEQ 000205 | COL 28 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000206 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000206 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "CONSULTA PRODUTO"
[SEQ 000206 | COL 38 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000207 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000207 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "WS-ULTIMO-PID --> "
[SEQ 000207 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WS-ULTIMO-PID
[SEQ 000207 | COL 54 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000208 | COL 12 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000208 | COL 20 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "PID: "
[SEQ 000208 | COL 27 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000209 | COL 12 (AREA B)] TOKEN_KEYWORD              : ACCEPT
[SEQ 000209 | COL 19 (AREA B)] TOKEN_IDENTIFIER           : WS-PID
[SEQ 000209 | COL 25 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000211 | COL 12 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000211 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : WS-PID
[SEQ 000211 | COL 24 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000211 | COL 27 (AREA B)] TOKEN_IDENTIFIER           : RP-PID
[SEQ 000211 | COL 33 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000213 | COL 12 (AREA B)] TOKEN_KEYWORD              : READ
[SEQ 000213 | COL 17 (AREA B)] TOKEN_IDENTIFIER           : FD-PRODUTOS
[SEQ 000213 | COL 29 (AREA B)] TOKEN_KEYWORD              : RECORD
[SEQ 000213 | COL 36 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000213 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : RP-PID
[SEQ 000214 | COL 17 (AREA B)] TOKEN_KEYWORD              : INVALID
[SEQ 000214 | COL 25 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000215 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000215 | COL 29 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "ERRO AO LOCALIZAR REGISTRO"
[SEQ 000216 | COL 17 (AREA B)] TOKEN_KEYWORD              : NOT
[SEQ 000216 | COL 21 (AREA B)] TOKEN_KEYWORD              : INVALID
[SEQ 000216 | COL 29 (AREA B)] TOKEN_KEYWORD              : KEY
[SEQ 000217 | COL 21 (AREA B)] TOKEN_KEYWORD              : MOVE
[SEQ 000217 | COL 26 (AREA B)] TOKEN_IDENTIFIER           : REG-PRODUTO
[SEQ 000217 | COL 38 (AREA B)] TOKEN_KEYWORD              : TO
[SEQ 000217 | COL 41 (AREA B)] TOKEN_IDENTIFIER           : WS-PRODUTO
[SEQ 000218 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000218 | COL 29 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : " "
[SEQ 000219 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000219 | COL 29 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "NOME: "
[SEQ 000219 | COL 38 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000219 | COL 43 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000219 | COL 46 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000220 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000220 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : WS-NOME
[SEQ 000221 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000221 | COL 29 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "VALOR: "
[SEQ 000221 | COL 39 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000221 | COL 44 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000221 | COL 47 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000222 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000222 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : WS-VALOR
[SEQ 000223 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000223 | COL 29 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "QUANTIDADE: "
[SEQ 000223 | COL 44 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000223 | COL 49 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000223 | COL 52 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000224 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000224 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : WS-QTD
[SEQ 000225 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000225 | COL 29 (AREA B)] TOKEN_LITERAL_ALPHANUMERIC : "DESCRICAO: "
[SEQ 000225 | COL 43 (AREA B)] TOKEN_KEYWORD              : WITH
[SEQ 000225 | COL 48 (AREA B)] TOKEN_KEYWORD              : NO
[SEQ 000225 | COL 51 (AREA B)] TOKEN_KEYWORD              : ADVANCING
[SEQ 000226 | COL 21 (AREA B)] TOKEN_KEYWORD              : DISPLAY
[SEQ 000226 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : WS-DESCRICAO
[SEQ 000226 | COL 41 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000228 | COL 08 (AREA A)] TOKEN_KEYWORD              : END
[SEQ 000228 | COL 12 (AREA B)] TOKEN_KEYWORD              : PROGRAM
[SEQ 000228 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROG007_LOJA
[SEQ 000228 | COL 32 (AREA B)] TOKEN_PERIOD               : .
```

</details>


### 2.3 Resultados de erros léxicos

Quando encontra um indicador inválido na coluna 7, o leitor reporta erro severo `[S]` em `stderr` e ignora a linha. Quando encontra um erro léxico nas colunas 8 a 72, `emit_lexical_error` imprime o diagnóstico `[E]` em `stderr` com sequência e coluna, incrementa o contador de erros (fazendo `./cbc` retornar `1`) e emite `TOKEN_UNKNOWN` para continuar analisando o arquivo.

Abaixo estão os dez erros inseridos nos sete programas de teste e as saídas obtidas com `./cbc -l`.

#### 1. Literal alfanumérico não terminado (`prog001_divisoes.cob`)

Omissão das aspas de fechamento na instrução `DISPLAY`.

```cobol
            DISPLAY "Hello world
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000055, Col 21): Unterminated alphanumeric literal -> ""Hello world"
```

Token emitido em `stdout`:

```text
[SEQ 000055 | COL 21 (AREA B)] TOKEN_UNKNOWN              : "Hello world
```

#### 2. Indicador inválido na coluna 7 (`prog001_divisoes.cob`)

Uso do caractere `X` na coluna 7, fora do conjunto permitido (espaço, `*`, `/`, `-`, `D`, `d`). O erro severo `[S]` descarta a linha.

```cobol
      X     DISPLAY "INVALID INDICATOR".
```

Saída em `stderr`:

```text
[S] Indicator Error (cobol-exemplos/prog001_divisoes.cob, Seq 000055): Invalid indicator 'X' in Column 7.
```

#### 3. Identificador terminado em hífen (`prog002_operacoes.cob`)

Nome de variável encerrado com `-`, violando a regra de formação de identificadores.

```cobol
           MOVE 20 TO WRK-VALOR2-.
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000028, Col 23): Identifier cannot start or end with a hyphen -> "WRK-VALOR2-"
```

Token emitido em `stdout`:

```text
[SEQ 000028 | COL 23 (AREA B)] TOKEN_UNKNOWN              : WRK-VALOR2-
```

#### 4. Caractere fora da linguagem (`prog002_operacoes.cob`)

Inserção do símbolo `@` como operando de `MOVE`.

```cobol
           MOVE @ TO WRK-A.
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000048, Col 17): Unrecognized character -> "@"
```

Token emitido em `stdout`:

```text
[SEQ 000048 | COL 17 (AREA B)] TOKEN_UNKNOWN              : @
```

#### 5. Identificador acima de 30 caracteres (`prog003_calculadora.cob`)

Declaração de variável com 35 caracteres (`WKS-RESULTADO-FORMATADO-COM-MASCARA`).

```cobol
       77 WKS-RESULTADO-FORMATADO-COM-MASCARA PIC X(01) VALUE SPACES.
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000020, Col 15): Identifier exceeds maximum length of 30 characters -> "WKS-RESULTADO-FORMATADO-COM-MASCARA"
```

Token emitido em `stdout`:

```text
[SEQ 000020 | COL 15 (AREA B)] TOKEN_UNKNOWN              : WKS-RESULTADO-FORMATADO-COM-MASCARA
```

#### 6. Operador desconhecido (`prog003_calculadora.cob`)

Uso de `^` em vez de `**` para exponenciação dentro de `COMPUTE`.

```cobol
             COMPUTE WKS-RESULT = WKS-N1 ^ WKS-N2
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000072, Col 43): Unrecognized character -> "^"
```

Token emitido em `stdout`:

```text
[SEQ 000072 | COL 43 (AREA B)] TOKEN_UNKNOWN              : ^
```

#### 7. Identificador iniciado com hífen (`prog004_folha_pgto.cob`)

Identificador `-IDX` começando com hífen.

```cobol
            MOVE 1 TO -IDX.
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000034, Col 23): Identifier cannot start or end with a hyphen -> "-IDX"
```

Token emitido em `stdout`:

```text
[SEQ 000034 | COL 23 (AREA B)] TOKEN_UNKNOWN              : -IDX
```

#### 8. Aspas incompatíveis no literal (`prog005_strings.cob`)

Literal aberto com aspas duplas e encerrado com aspa simples.

```cobol
           DISPLAY "CONT=" CONT " - FOR CHARACTERS'.
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000033, Col 34): Unterminated alphanumeric literal -> "" - FOR CHARACTERS'."
```

Token emitido em `stdout`:

```text
[SEQ 000033 | COL 34 (AREA B)] TOKEN_UNKNOWN              : " - FOR CHARACTERS'.
```

#### 9. Continuação com aspa diferente da abertura (`prog006_files.cob`)

Literal aberto até a coluna 72 com `"` e continuado na linha `-` com `'`. O lexer reporta o literal não terminado e o erro de continuação.

```cobol
           DISPLAY "GRAVANDO OS REGISTROS DO TRABALHADOR NO DISCO NO ARQ
      -    'UIVO DE SAIDA'.
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000052, Col 20): Unterminated alphanumeric literal -> ""GRAVANDO OS REGISTROS DO TRABALHADOR NO DISCO NO ARQ"
[E] Lexical Error (Seq 000053, Col 7): Continuation line must start with the literal's quote character -> "-"
```

Token emitido em `stdout`:

```text
[SEQ 000052 | COL 20 (AREA B)] TOKEN_UNKNOWN              : "GRAVANDO OS REGISTROS DO TRABALHADOR NO DISCO NO ARQ
[SEQ 000053 | COL 07 (UNK   )] TOKEN_UNKNOWN              : -
```

#### 10. Literal continuado e nunca fechado (`prog007_loja.cob`)

A continuação reabre com a aspa correta, mas não fecha o literal antes da linha normal seguinte.

```cobol
           DISPLAY "PID JAH EXISTE E GRAVADO NO ARQUIVO DE ARMAZE
      -        "NAMENTO - PID: WS-ULTIMO-PID
```

Saída em `stderr`:

```text
[E] Lexical Error (Seq 000203, Col 20): Unterminated alphanumeric literal -> ""PID JAH EXISTE E GRAVADO NO ARQUIVO DE ARMAZE       NAMENTO - PID: WS-ULTIMO-PID"
```

Token emitido em `stdout`:

```text
[SEQ 000203 | COL 20 (AREA B)] TOKEN_UNKNOWN              : "PID JAH EXISTE E GRAVADO NO ARQUIVO DE ARMAZE       NAMENTO - PID: WS-ULTIMO-PID
```


## 3. Parte 2: Análise Sintática e AST

### 3.1 Arquitetura do parser, copybooks e AST

A análise sintática segue a Seção 3 de [EspecificacaoCOBOL.md](EspecificacaoCOBOL.md) e foi construída com GNU Bison (`bison -d`) sobre o fluxo de tokens coletado pelo lexer:

- [include/parser.h](include/parser.h) e [src/parser.y](src/parser.y): definem a gramática em Bison com `%define api.prefix {cob_}` (evitando conflito de símbolos com o `yylex()` gerado pelo Flex) e `%define parse.error verbose` para mensagens detalhadas de erro com arquivo, linha/sequência, coluna e lexema. O adaptador `cob_lex()` lê sequencialmente o `TokenList`, converte palavras-chave para os tokens do Bison e agrupa comment-entries livres da `IDENTIFICATION DIVISION` (`AUTHOR.`, `INSTALLATION.`, `DATE-WRITTEN.`, `DATE-COMPILED.`, `SECURITY.`, `REMARKS.`) em um par `T_ID_COMMENT_HDR` / `T_COMMENT_TEXT`.
- Hierarquia das divisões: o símbolo inicial `program` exige `IDENTIFICATION DIVISION.` seguida de `PROGRAM-ID.`, aceita `ENVIRONMENT DIVISION.` (`CONFIGURATION SECTION.`, `SPECIAL-NAMES.` com `DECIMAL-POINT IS COMMA` e `CRT STATUS`, `INPUT-OUTPUT SECTION.` e `FILE-CONTROL.`/`SELECT`) e `DATA DIVISION.` (`FILE SECTION.` com `FD`/`SD`, `WORKING-STORAGE`, `LOCAL-STORAGE`, `LINKAGE` e `SCREEN SECTION`, com cláusulas de item de dados em ordem livre), exige `PROCEDURE DIVISION [USING ...] [RETURNING ...].` (com seções, parágrafos vazios ou com sentenças, e instruções terminadas por `.`) e aceita o encerramento opcional `END PROGRAM <nome>.`.
- Comandos e expressões suportados: `DISPLAY` (incluindo `AT LINE/COLUMN`, `UPON` e `WITH NO ADVANCING`), `ACCEPT` (`FROM DATE/TIME/...`), `MOVE`, `ADD`, `SUBTRACT`, `MULTIPLY`, `DIVIDE`, `COMPUTE`, `IF`/`ELSE`/`END-IF` (incluindo `IF` sem `END-IF` encerrado por `END-PERFORM`, `END-EVALUATE` ou `.`), `EVALUATE`/`WHEN`/`WHEN OTHER`/`END-EVALUATE`, `PERFORM` inline e out-of-line (`TIMES`, `UNTIL`, `VARYING ... FROM ... BY ... UNTIL ... AFTER ...`), `OPEN` (múltiplos modos), `CLOSE`, `READ`, `WRITE`, `REWRITE`, `DELETE`, `START`, `GO TO`, `ALTER`, `CALL`, `CANCEL`, `STOP RUN`, `GOBACK`, `EXIT`, `CONTINUE`, `NEXT SENTENCE`, `INITIALIZE`, `INSPECT`, `STRING`, `UNSTRING`, `SET`, `SEARCH`, `SORT`, `MERGE`, `RELEASE` e `RETURN`. Nas expressões e condições, a gramática trata qualificação (`OF`/`IN`), subscritos multidimensionais (`DPTO(IDX, 1)`), modificação de referência (`WRK-NOME(WRK-IND2:1)`), funções intrínsecas (`FUNCTION REVERSE`, `FUNCTION UPPER-CASE`, `FUNCTION RANDOM`), `LENGTH OF`, testes de classe (`IS NOT NUMERIC`), operadores relacionais e condições combinadas abreviadas (`IF WKS-OPCAO = "S" OR "U"`). A gramática possui zero conflitos reduce/reduce no Bison; os conflitos shift/reduce remanescentes correspondem ao fechamento opcional de blocos COBOL sem terminador explícito (`IF` sem `END-IF`, cláusulas `AT END` / `INVALID KEY` / `ON SIZE ERROR`), resolvidos pelo comportamento padrão de *shift* do Bison (associando ao bloco mais interno).
- Expansão de copybooks (`COPY`): em `lex_cobol_file`, quando o modo parser está ativo, toda diretiva `COPY <nome>.` busca o arquivo (`.cpy`, `.cbl`, `.cob`) nos diretórios informados via `-I`, no diretório do arquivo fonte e em `codigo_fonte_cobol/`, expandindo recursivamente seu conteúdo (até profundidade 16) no fluxo de tokens. Caso o arquivo não exista, o compilador emite aviso `[W]` e preserva um nó `AST_COPY` para que a análise continue.
- [include/ast.h](include/ast.h) e [src/ast.c](src/ast.c): implementam a árvore sintática abstrata (`ASTNode`) com tipo do nó (`ASTKind`), posição original (`seq_num`, `col`), textos/atributos e lista ordenada de filhos, além das rotinas de impressão (`./cbc -a`) e liberação de memória (`ast_free`). A árvore já preserva níveis de dados, cláusulas `PIC`/`VALUE`/`OCCURS`, descritores `SELECT`/`FD`, seções, parágrafos e árvores de expressão para a análise semântica (Parte 3) e geração de código MIPS (Parte 5).

Uso de ferramentas de IA na Parte 2: seguindo o fluxo do projeto, o arquivo de especificação Yacc/Bison [src/parser.y](src/parser.y) foi elaborado com apoio de IA (Gemini via Antigravity) para cobrir o subconjunto completo exigido pelos programas da disciplina e eliminar conflitos reduce/reduce, além da atualização deste relatório. A integração com o `TokenList`, a expansão de `COPY`, a estrutura da AST e a CLI seguem a arquitetura em C do repositório.

### 3.2 Resultados dos testes sintáticos

Todos os 14 programas de teste de `cobol-exemplos/` e `exemplos/` passam na análise léxica e sintática (`make test`), incluindo a expansão automática de `COPY002A.cpy` em `CAD0002A.cbl` e de `COD001A.cpy` em `PROG024A.cbl`.

| Programa | Recursos exercitados | Nós AST | Exit |
| --------------------------------------- | --------------------------------------------- | ------: | ---: |
| `cobol-exemplos/prog001_divisoes.cob` | Quatro divisões, comment-entries (`AUTHOR`, `SECURITY`, `REMARKS`), `END PROGRAM` | 19 | 0 |
| `cobol-exemplos/prog002_operacoes.cob` | Qualificação `OF`, aritmética múltipla, `ON SIZE ERROR` | 260 | 0 |
| `cobol-exemplos/prog003_calculadora.cob` | `FUNCTION UPPER-CASE`, condições abreviadas (`OR "U" OR "M"`), `COMPUTE **` | 170 | 0 |
| `cobol-exemplos/prog004_folha_pgto.cob` | `OCCURS ... INDEXED BY`, `SET`, `SEARCH`, continuação de identificador | 154 | 0 |
| `cobol-exemplos/prog005_strings.cob` | `INSPECT` (`TALLYING`, `REPLACING`, `CONVERTING`) | 152 | 0 |
| `cobol-exemplos/prog006_files.cob` | `FILE-CONTROL`, `FD`, `OPEN`/`WRITE`/`CLOSE`, subscritos bidimensionais | 130 | 0 |
| `cobol-exemplos/prog007_loja.cob` | Arquivo `INDEXED`/`DYNAMIC`, `START`, `READ`, `EVALUATE`, `INVALID KEY` | 371 | 0 |
| `exemplos/BUSCA01A.cbl` | `SPECIAL-NAMES` (`DECIMAL-POINT IS COMMA`), seções, `READ ... AT END / NOT AT END` | 115 | 0 |
| `exemplos/CAD0002A.cbl` | `LINKAGE SECTION`, `COPY COPY002A.`, `USING`, `LENGTH OF`, refmod `(i:1)`, `FUNCTION REVERSE` | 643 | 0 |
| `exemplos/CAD0004A.cbl` | Parágrafos vazios (`0001-END.`), `PERFORM ... UNTIL`, escrita sequencial | 74 | 0 |
| `exemplos/CAD0005A.cbl` | `PERFORM VARYING` inline com `IF` fechado por `END-PERFORM`, continuação de string | 356 | 0 |
| `exemplos/CAIXAREG.cbl` | Máscara `PIC ZZZ.ZZZ.ZZZ,ZZ`, múltiplas seções, `COMPUTE` | 285 | 0 |
| `exemplos/CALCMD01.cbl` | `IF` aninhados, `AND` múltiplo, `COMPUTE` com parênteses | 207 | 0 |
| `exemplos/PROG024A.cbl` | `COPY COD001A.`, `OPEN INPUT ... EXTEND ...`, `CALL 'PROGDATA' USING`, `EVALUATE` | 1002 | 0 |

Além dos 14 programas acima, o compilador foi testado sobre os 83 arquivos `.cbl`/`.cob` do submódulo [codigo_fonte_cobol/](codigo_fonte_cobol/): todos os 71 programas COBOL padrão em formato fixo válidos passaram com código de saída `0`. Os 12 arquivos restantes falharam por razões externas à gramática COBOL padrão: 6 contêm marcadores de conflito de merge do Git no repositório de origem (`CAD0005A.cbl`, `PROG002A.cbl`, `PROG010A.cbl`, `PROG023C.cbl`, `SORT001A.cbl`, `VALIDA1B.cbl`), 2 usam SQL embutido `EXEC SQL` (`PROG006A.cbl`, `clientes-sql.cbl`), 1 é uma definição de mapa BMS/CICS (`TELABMS.cbl`), 1 está em formato livre (`CBREAKEX.cbl`), 1 está totalmente comentado sem divisões (`CLIESQL01.cbl`) e 1 possui um identificador de 31 caracteres que viola o limite de 30 caracteres da especificação (`PROG042A.cbl`).

<details>
<summary>Trecho da AST gerada para exemplos/BUSCA01A.cbl (./cbc -a)</summary>

```text
$ ./cbc -a exemplos/BUSCA01A.cbl
PROGRAM [BUSCA01A] @1:8
  IDENTIFICATION_DIVISION [BUSCA01A] @10:8
  ENVIRONMENT_DIVISION @20:8
    CONFIGURATION_SECTION @15:8
      SPECIAL_NAMES @15:8
        CLAUSE [DECIMAL-POINT-IS-COMMA] @13:34
    FILE_CONTROL @20:8
      SELECT [ARQUIVO-ENTRADA] @18:47
        CLAUSE [ASSIGN] @17:45
          LITERAL_STR ['CADASTRO2.dat'] @17:45
        CLAUSE [ORGANIZATION] @18:37
  DATA_DIVISION @36:8
    DATA_SECTION [FILE] @28:8
      FD [ARQUIVO-ENTRADA] @28:8
        DATA_ITEM (level=01) [REGISTRO-ENTRADA] @23:27
        DATA_ITEM (level=05) [NOME] @24:62
          CLAUSE [PIC] <X(30)> @24:44
          CLAUSE [VALUE] @24:62
            FIGURATIVE [SPACES] @24:56
        DATA_ITEM (level=05) [IDADE] @25:61
          CLAUSE [PIC] <9(02)> @25:44
          CLAUSE [VALUE] @25:61
            FIGURATIVE [ZEROS] @25:56
        DATA_ITEM (level=05) [CPF] @26:61
          CLAUSE [PIC] <9(11)> @26:44
          CLAUSE [VALUE] @26:61
            FIGURATIVE [ZEROS] @26:56
    DATA_SECTION [WORKING-STORAGE] @36:8
      DATA_ITEM (level=01) [WRK-FIM-ARQUIVO] @29:59
        CLAUSE [PIC] <X(01)> @29:44
        CLAUSE [VALUE] @29:59
          LITERAL_STR ['N'] @29:56
      DATA_ITEM (level=01) [WRK-CPF-PROCURADO] @30:61
        CLAUSE [PIC] <9(11)> @30:44
        CLAUSE [VALUE] @30:61
          FIGURATIVE [ZEROS] @30:56
      DATA_ITEM (level=01) [WRK-CPF-LISTADO] @31:61
        CLAUSE [PIC] <9(11)> @31:44
        CLAUSE [VALUE] @31:61
          FIGURATIVE [ZEROS] @31:56
      DATA_ITEM (level=01) [WRK-CPF-ENCONTRADO] @32:59
        CLAUSE [PIC] <X(01)> @32:44
        CLAUSE [VALUE] @32:59
          LITERAL_STR ['N'] @32:56
      DATA_ITEM (level=01) [WRK-IND1] @33:61
```

</details>

<details>
<summary>Expansão de COPY COPY002A. na AST de exemplos/CAD0002A.cbl (./cbc -a)</summary>

```text
    DATA_SECTION [LINKAGE] @80:8
      DATA_ITEM (level=01) [COPY002A-HEADER] @19:27
      DATA_ITEM (level=05) [COPY002A-COD-BOOK] @20:67
        CLAUSE [PIC] <X(08)> @20:45
        CLAUSE [VALUE] @20:67
          LITERAL_STR ['COPY002A'] @20:57
      DATA_ITEM (level=05) [COPY002A-TAM-BOOK] @21:62
        CLAUSE [PIC] <9(05)> @21:45
        CLAUSE [VALUE] @21:62
          LITERAL_NUM [02125] @21:57
      DATA_ITEM (level=01) [COPY002A-REGISTRO] @22:29
      DATA_ITEM (level=05) [COPY002A-QUANT-REG] @23:50
        CLAUSE [PIC] <9(02)> @23:45
      DATA_ITEM (level=05) [COPY002A-CADUSUAR] @24:46
        CLAUSE (50) [OCCURS] <50> @24:46
      DATA_ITEM (level=10) [COPY002A-COD] @25:50
        CLAUSE [PIC] <9(02)> @25:45
      DATA_ITEM (level=10) [COPY002A-NOME] @26:50
        CLAUSE [PIC] <X(30)> @26:45
      DATA_ITEM (level=10) [COPY002A-IDADE] @27:50
        CLAUSE [PIC] <9(02)> @27:45
      DATA_ITEM (level=10) [COPY002A-DATA-NASC] @28:50
```

</details>

### 3.3 Resultados de erros sintáticos

Quando a entrada viola a gramática da Seção 3, `cob_error` reporta `[E] Syntax Error` em `stderr` com arquivo, sequência/linha, coluna, descrição do token inesperado/esperado e lexema próximo, liberando a AST parcial e encerrando com código `1`.

Abaixo estão oito testes de erro sintático injetados nos programas de teste e as saídas reais de `./cbc`.

#### 1. Ausência de `PROGRAM-ID` (`cobol-exemplos/prog001_divisoes.cob`)

Remoção da linha `PROGRAM-ID. PROG0001.` logo após `IDENTIFICATION DIVISION.`.

```cobol
       IDENTIFICATION DIVISION.
       AUTHOR. ENIO BASSO.
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (cobol-exemplos/prog001_divisoes.cob, Seq 000018, Col 8): syntax error, unexpected T_ID_COMMENT_HDR, expecting KW_PROGRAM_ID (near "AUTHOR")
```

#### 2. Ordem incorreta de divisões (`exemplos/BUSCA01A.cbl`)

Substituição de `ENVIRONMENT DIVISION.` por `DATA DIVISION.` antes de `CONFIGURATION SECTION.`.

```cobol
       DATA DIVISION.
       CONFIGURATION SECTION.
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (exemplos/BUSCA01A.cbl, Seq 000011, Col 8): syntax error, unexpected KW_CONFIGURATION, expecting KW_PROCEDURE (near "CONFIGURATION")
```

#### 3. Ausência de `PROCEDURE DIVISION` (`exemplos/CAD0004A.cbl`)

Remoção do cabeçalho obrigatório `PROCEDURE DIVISION.` antes do primeiro parágrafo.

```cobol
       01  WRK-CPF    PIC X(14).
       0001-PROCESSAR.
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (exemplos/CAD0004A.cbl, Seq 000031, Col 8): syntax error, unexpected T_IDENTIFIER, expecting KW_PROCEDURE (near "0001-PROCESSAR")
```

#### 4. Ponto final omitido em item de dados (`cobol-exemplos/prog002_operacoes.cob`)

Remoção do ponto final em `77 WRK-VALOR1 PIC 9(10) VALUES 10`.

```cobol
           77 WRK-VALOR1      PIC 9(10) VALUES 10
           77 WRK-VALOR2      PIC 9(10) VALUES ZEROS.
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (cobol-exemplos/prog002_operacoes.cob, Seq 000017, Col 15): syntax error, unexpected T_IDENTIFIER, expecting KW_COMP (near "WRK-VALOR2")
```

#### 5. `MOVE` sem palavra-chave `TO` (`exemplos/CAD0004A.cbl`)

Omissão de `TO` entre origem e destino na instrução `MOVE`.

```cobol
           MOVE WRK-NOME NOME
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (exemplos/CAD0004A.cbl, Seq 000054, Col 26): syntax error, unexpected T_IDENTIFIER, expecting KW_TO (near "NOME")
```

#### 6. `PERFORM` inline sem `END-PERFORM` (`exemplos/CAD0005A.cbl`)

Remoção de `END-PERFORM` ao final de um laço `PERFORM VARYING` inline.

```cobol
           PERFORM VARYING WRK-IND1 FROM 1 BY 1 UNTIL WRK-FIM-ARQ EQUAL 'S'
               PERFORM 0001-LER-ARQUIVO-ENT
               ...
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (exemplos/CAD0005A.cbl, Seq 000166, Col 12): syntax error, unexpected T_PERIOD (near ".")
```

#### 7. Expressão incompleta em `COMPUTE` (`exemplos/CALCMD01.cbl`)

Remoção do operando direito do operador `/` em `COMPUTE`.

```cobol
              COMPUTE WRK-VALOR-MEDIO =  WRK-VALOR-MEDIO /
              DISPLAY '-------------'
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (exemplos/CALCMD01.cbl, Seq 000136, Col 15): syntax error, unexpected KW_DISPLAY (near "DISPLAY")
```

#### 8. Ponto final omitido em `SELECT` (`exemplos/BUSCA01A.cbl`)

Remoção do ponto final ao término da entrada `SELECT` na `FILE-CONTROL`.

```cobol
           SELECT ARQUIVO-ENTRADA ASSIGN TO 'CADASTRO2.dat'
               ORGANIZATION IS LINE SEQUENTIAL
```

Saída em `stderr` (exit `1`):

```text
[E] Syntax Error (exemplos/BUSCA01A.cbl, Seq 000020, Col 8): syntax error, unexpected KW_DATA, expecting KW_STATUS (near "DATA")
```


## 4. Parte 3: Análise Semântica e Tabela de Símbolos

### 4.1 Implementação do analisador semântico

A análise semântica implementa as quatro verificações da Seção 4 de [EspecificacaoCOBOL.md](EspecificacaoCOBOL.md) em [include/sema.h](include/sema.h) e [src/sema.c](src/sema.c), operando em duas passagens sobre a AST produzida pelo parser:

1. Primeira passagem (coleta de declarações): percorre `ENVIRONMENT DIVISION` (`SELECT`), `DATA DIVISION` (`FD`/`SD`, itens de dados `01` a `49`, `77`, `88` e índices de `OCCURS ... INDEXED BY`) e `PROCEDURE DIVISION` (seções e parágrafos). Uma pilha de níveis associa cada item subordinado ao seu grupo pai (`parent_idx`) e ao descritor `FD` de origem (`fd_idx`). Cada cláusula `PIC` é classificada em `PIC_A`, `PIC_9`, `PIC_9_ED` (numérico editado) ou `PIC_X`, e itens sem `PIC` recebem categoria `GROUP` (ou `COND_88` para nível `88`). Declarações duplicadas com o mesmo nome dentro do mesmo escopo pai (ignorando `FILLER`), `SELECT`/`FD` repetidos e nomes duplicados de seção ou parágrafo geram `[E] Semantic Error`.
2. Segunda passagem (validação de uso): verifica o pareamento `SELECT`/`FD` e percorre as instruções da `PROCEDURE DIVISION` aplicando as regras da especificação:
   - Compatibilidade de `MOVE` (Regra 1): impede (`[E]`) movimentações de dados puramente alfabéticos (`PIC A`) ou literais alfanuméricos não numéricos para variáveis numéricas (`PIC 9` ou numéricas editadas). Conversões permissivas do COBOL, como `PIC X` para `PIC 9` ou numérico para `PIC A`, emitem aviso `[W]` sem abortar a compilação. Itens com modificação de referência (`VAR(pos:len)`) são tratados como alfanuméricos.
   - Unicidade e qualificação de identificadores (Regra 2): resolve referências simples e qualificadas (`A OF B`), reportando identificadores não declarados (aceitando registradores especiais padrão como `RETURN-CODE` e `TALLY`) e referências ambíguas quando o mesmo nome existe em grupos diferentes sem qualificação `OF`/`IN`. Isso aceita estruturas hierárquicas reais como `01 TAB-ENT-LINHA` contendo `05 TAB-ENT-LINHA OCCURS 10 TIMES` em `CAD0005A.cbl` e `PROG024A.cbl`.
   - Destinos de controle (Regra 3): confirma que todo alvo de `PERFORM`, `PERFORM ... THRU`, `GO TO` e `ALTER` corresponde a uma seção ou parágrafo declarado na `PROCEDURE DIVISION`.
   - Associação de arquivos (Regra 4): exige que todo arquivo usado em `OPEN`, `READ`, `DELETE`, `START`, `CLOSE` ou `SORT` possua tanto `SELECT` em `FILE-CONTROL` quanto `FD`/`SD` na `DATA DIVISION`, e que todo registro usado em `WRITE` ou `REWRITE` pertença a um `FD` com `SELECT` correspondente.

A flag `-s`/`--symtab` imprime a tabela de símbolos (`FILES`, `DATA ITEMS` e `PROCEDURES`), enquanto `-p`/`--parse` permite parar após a análise sintática. O parser ([src/parser.y](src/parser.y)) também passou a preservar na AST as cláusulas `INDEXED BY`, `THRU`, `GIVING`, `REMAINDER`, `INTO`, `FROM`, `CALL USING` e manipuladores `NOT AT END`/`NOT INVALID KEY`/`NOT ON SIZE ERROR` para que todas as referências sejam verificadas.

### 4.2 Resultados dos testes semânticos

Os 14 programas de `cobol-exemplos/` e `exemplos/` passam na análise completa (`make test`) com zero erros e zero avisos.

| Programa | Arquivos | Itens de dados | Procedimentos | Exit |
| --------------------------------------- | -------: | -------------: | ------------: | ---: |
| `cobol-exemplos/prog001_divisoes.cob` | 0 | 0 | 1 | 0 |
| `cobol-exemplos/prog002_operacoes.cob` | 0 | 7 | 1 | 0 |
| `cobol-exemplos/prog003_calculadora.cob` | 0 | 7 | 4 | 0 |
| `cobol-exemplos/prog004_folha_pgto.cob` | 0 | 7 | 4 | 0 |
| `cobol-exemplos/prog005_strings.cob` | 0 | 4 | 1 | 0 |
| `cobol-exemplos/prog006_files.cob` | 1 | 16 | 2 | 0 |
| `cobol-exemplos/prog007_loja.cob` | 2 | 22 | 6 | 0 |
| `exemplos/BUSCA01A.cbl` | 1 | 9 | 8 | 0 |
| `exemplos/CAD0002A.cbl` | 0 | 47 | 18 | 0 |
| `exemplos/CAD0004A.cbl` | 1 | 8 | 4 | 0 |
| `exemplos/CAD0005A.cbl` | 1 | 15 | 18 | 0 |
| `exemplos/CAIXAREG.cbl` | 0 | 10 | 20 | 0 |
| `exemplos/CALCMD01.cbl` | 0 | 8 | 12 | 0 |
| `exemplos/PROG024A.cbl` | 2 | 65 | 36 | 0 |

Sobre os 71 programas COBOL válidos do submódulo [codigo_fonte_cobol/](codigo_fonte_cobol/) que passam pelo parser, 70 passaram sem erros na análise semântica e 1 (`ASCII001.cbl`) foi corretamente rejeitado por usar a variável `WS-CODIGO` na linha 10 sem declará-la na `WORKING-STORAGE SECTION`.

<details>
<summary>Tabela de símbolos gerada para exemplos/BUSCA01A.cbl (./cbc -s)</summary>

```text
$ ./cbc -s exemplos/BUSCA01A.cbl
=== FILES (1) ===
  ARQUIVO-ENTRADA          SELECT=yes  FD=yes
=== DATA ITEMS (9) ===
  01 REGISTRO-ENTRADA             GROUP    PIC=-                PARENT=-                    @23:27
  05 NOME                         PIC_X    PIC=X(30)            PARENT=REGISTRO-ENTRADA     @24:62
  05 IDADE                        PIC_9    PIC=9(02)            PARENT=REGISTRO-ENTRADA     @25:61
  05 CPF                          PIC_9    PIC=9(11)            PARENT=REGISTRO-ENTRADA     @26:61
  01 WRK-FIM-ARQUIVO              PIC_X    PIC=X(01)            PARENT=-                    @29:59
  01 WRK-CPF-PROCURADO            PIC_9    PIC=9(11)            PARENT=-                    @30:61
  01 WRK-CPF-LISTADO              PIC_9    PIC=9(11)            PARENT=-                    @31:61
  01 WRK-CPF-ENCONTRADO           PIC_X    PIC=X(01)            PARENT=-                    @32:59
  01 WRK-IND1                     PIC_9    PIC=9(02)            PARENT=-                    @33:61
=== PROCEDURES (8) ===
  0000-PROCESSAR               SECTION   @43:8
  0000-END                     PARAGRAPH @61:8
  0001-LER-ARQUIVO             SECTION   @67:8
  0001-END                     PARAGRAPH @81:8
  0002-BUSCAR-CPF              SECTION   @88:8
  0002-END                     PARAGRAPH @100:8
  9999-FINALIZAR               SECTION   @106:8
  9999-END                     PARAGRAPH @114:8
```

</details>

### 4.3 Resultados de erros e avisos semânticos

Abaixo estão dez testes cobrindo as quatro regras semânticas da Seção 4 (além de variáveis não declaradas e avisos `[W]`):

#### 1. `MOVE` incompatível de `PIC A` para `PIC 9` (`cobol-exemplos/prog003_calculadora.cob`)

Movimentação de `WKS-OPCAO` (`PIC A(01)`) para `WKS-SAIR` (`PIC 9(1)`), violando a Regra 1 da Seção 4.

```cobol
           MOVE WKS-OPCAO TO WKS-SAIR.
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (cobol-exemplos/prog003_calculadora.cob, Seq 000029, Col 30): Incompatible MOVE from alphabetic (PIC A) "WKS-OPCAO" to numeric "WKS-SAIR" (9(1)).
```

#### 2. `MOVE` de literal não numérico para `PIC 9` (`exemplos/BUSCA01A.cbl`)

Atribuição da string `'ABC'` à variável numérica `WRK-IND1` (`PIC 9(02)`).

```cobol
                 MOVE 'ABC'       TO WRK-IND1
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000076, Col 38): Incompatible MOVE from non-numeric literal 'ABC' to numeric "WRK-IND1" (9(02)).
```

#### 3. Aviso `[W]` em `MOVE` de `PIC X` para `PIC 9` (`exemplos/BUSCA01A.cbl`)

Movimentação de `WRK-FIM-ARQUIVO` (`PIC X(01)`) para `WRK-IND1` (`PIC 9(02)`). Gera aviso sem interromper a compilação.

```cobol
                 MOVE WRK-FIM-ARQUIVO TO WRK-IND1
```

Saída em `stderr` (exit `0`):

```text
[W] Semantic Warning (exemplos/BUSCA01A.cbl, Seq 000076, Col 42): Moving alphanumeric "WRK-FIM-ARQUIVO" to numeric "WRK-IND1" (9(02)).
```

#### 4. Identificador duplicado no mesmo escopo (`exemplos/BUSCA01A.cbl`)

Segunda declaração de `01 WRK-IND1` na `WORKING-STORAGE SECTION` (Regra 2).

```cobol
       01 WRK-IND1                     PIC 9(02) VALUE ZEROS.
       01 WRK-IND1                     PIC 9(02) VALUE ZEROS.
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000034, Col 61): Duplicate identifier "WRK-IND1" in the same scope (previous at Seq 000033).
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000077, Col 38): Ambiguous identifier "WRK-IND1" (2 declarations); qualify with OF/IN.
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000110, Col 54): Ambiguous identifier "WRK-IND1" (2 declarations); qualify with OF/IN.
```

#### 5. Referência ambígua sem qualificação `OF` (`exemplos/CAD0004A.cbl`)

Declaração de `05 NOME` em dois grupos distintos (`REGISTRO-CADASTRO` e `OUTRO-REG`) seguida de `MOVE WRK-NOME TO NOME` sem `OF`. Ao qualificar como `NOME OF REGISTRO-CADASTRO`, o programa passa sem erros.

```cobol
       01  OUTRO-REG.
         05 NOME     PIC X(30).
       ...
           MOVE WRK-NOME  TO NOME
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/CAD0004A.cbl, Seq 000056, Col 30): Ambiguous identifier "NOME" (2 declarations); qualify with OF/IN.
```

#### 6. Alvo inexistente em `PERFORM` (`exemplos/BUSCA01A.cbl`)

Chamada a um parágrafo/seção não declarado (`9999-INEXISTENTE`), violando a Regra 3.

```cobol
           PERFORM 9999-INEXISTENTE
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000058, Col 12): Undeclared PERFORM target "9999-INEXISTENTE".
```

#### 7. Alvo inexistente em `GO TO` (`exemplos/CAD0002A.cbl`)

Desvio `GO TO 9999-INEXISTENTE` para rótulo não declarado na `PROCEDURE DIVISION`.

```cobol
                   GO TO 9999-INEXISTENTE
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/CAD0002A.cbl, Seq 000265, Col 26): Undeclared GO TO target "9999-INEXISTENTE".
```

#### 8. Arquivo usado sem `SELECT` na `FILE-CONTROL` (`exemplos/BUSCA01A.cbl`)

Remoção do `SELECT ARQUIVO-ENTRADA` mantendo o `FD` e os comandos `OPEN`/`READ`/`CLOSE` (Regra 4).

```cobol
       FILE-CONTROL.
      * SELECT ARQUIVO-ENTRADA removido
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000026, Col 8): FD "ARQUIVO-ENTRADA" has no matching SELECT in FILE-CONTROL.
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000044, Col 23): File "ARQUIVO-ENTRADA" used in OPEN has no SELECT declaration in FILE-CONTROL.
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000053, Col 18): File "ARQUIVO-ENTRADA" used in CLOSE has no SELECT declaration in FILE-CONTROL.
[E] Semantic Error (exemplos/BUSCA01A.cbl, Seq 000068, Col 17): File "ARQUIVO-ENTRADA" used in READ has no SELECT declaration in FILE-CONTROL.
```

#### 9. `WRITE` em variável que não pertence a um `FD` (`exemplos/CAD0004A.cbl`)

Uso de `WRITE WRK-NOME`, onde `WRK-NOME` está na `WORKING-STORAGE` e não sob um descritor `FD` (Regra 4).

```cobol
           WRITE WRK-NOME
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/CAD0004A.cbl, Seq 000058, Col 18): Record "WRK-NOME" used in WRITE is not declared under an FD in FILE SECTION.
```

#### 10. Identificador não declarado (`exemplos/CALCMD01.cbl`)

Leitura via `ACCEPT WRK-NAO-DECL` de uma variável ausente na `DATA DIVISION`.

```cobol
              ACCEPT WRK-NAO-DECL
```

Saída em `stderr` (exit `1`):

```text
[E] Semantic Error (exemplos/CALCMD01.cbl, Seq 000125, Col 22): Undeclared identifier "WRK-NAO-DECL".
```

## 5. Parte 5: Geração de Código MIPS32

### 5.1 Estratégia e arquitetura do gerador

O backend gera assembly MIPS32 genérico diretamente a partir da AST validada pela análise semântica, usando apenas instruções inteiras padrão de 32 bits (`addiu`, `addu`, `subu`, `mult`, `div`, `mflo`, `mfhi`, `lw`, `sw`, `lb`, `lbu`, `sb`, `beq`, `bne`, `blt`, `bgt`, `ble`, `bge`, `j`, `jal`, `jr`, `sll`, `srl`, `and`, `or`, `xori`, `syscall`). Sem instruções específicas de versões recentes ou ponto flutuante em hardware, o mesmo arquivo `.s` monta para binários ELF estáticos MIPS32 little-endian (`mipsel`, compatível com `qemu-mipsel`, roteadores MediaTek/OpenWrt e PlayStation 2 Linux) e big-endian (`mips`, roteadores Atheros/Broadcom), além de oferecer o modo `-t mars` para o simulador MARS/SPIM.

Os arquivos adicionados nessa etapa são:

- [include/codegen.h](include/codegen.h): define os alvos (`MIPS_TARGET_LINUX` e `MIPS_TARGET_MARS`), a estrutura `CodegenOpts` e as funções `codegen_mips` e `codegen_emit_runtime`.
- [src/codegen.c](src/codegen.c): constrói o layout contíguo de memória de cada programa a partir da `DATA DIVISION`, calcula deslocamentos de grupos, `REDEFINES` e `OCCURS`, descobre e compila automaticamente subprogramas chamados via `CALL`, e emite código MIPS32 para todos os comandos da `PROCEDURE DIVISION`.
- [src/codegen_rt.c](src/codegen_rt.c): fornece a biblioteca de runtime embutida em MIPS32 (sem dependência de `libc`), separando a camada de chamadas de sistema (Linux MIPS o32: `4001` exit, `4003` read, `4004` write, `4005` open, `4006` close, `4013` time; MARS: `10`, `11`, `8`, `13`, `14`, `15`, `16`) das rotinas COBOL independentes de sistema operacional.
- [src/parser.y](src/parser.y) e [src/main.c](src/main.c): o parser passou a preservar na AST cláusulas necessárias à geração (`WITH NO ADVANCING`, `ACCEPT ... FROM DATE/TIME/DAY/DAY-OF-WEEK`, `DIVIDE ... INTO/BY`, `ROUNDED`, `INITIALIZE ... REPLACING`, `INSPECT` completo, `STRING`/`UNSTRING`), e `main.c` passou a acionar o backend MIPS32 por padrão (com flags `-S`/`--mips` e `-t linux|mars`).

Principais decisões técnicas adotadas no gerador:

1. Layout contíguo de bytes na `.data`: cada item raiz (`01`, `77` e índices `INDEXED BY`) vira um buffer contínuo de bytes pré-inicializado com seus `VALUE` (incluindo `FILLER VALUE '/'`, `ALL '-'`, `SPACES`, `ZEROS` e replicação de `OCCURS`). Campos subordinados acessam esse buffer por deslocamento exato, o que garante que grupos compostos (`WRK-DATA-NASC-AUX`, `WRK-MASC-DATA`, `REGISTRO-CADASTRO`, `FD-ARQ-ENT`) e `REDEFINES` compartilhem memória exatamente como no COBOL. Todas as palavras `.word` (descritores de arquivo, ponteiros da `LINKAGE` e tabelas do runtime) recebem `.align 2` para evitar falhas de alinhamento (`AdEL`/`AdES`) em hardware MIPS real.
2. Aritmética decimal em ponto fixo sem overflow: o gerador detecta a maior precisão decimal `V` usada no programa (até 4 casas, `prog_scale = 10^prog_frac`, por exemplo `10000` em `CALCMD01.cbl` e `100` em `CAIXAREG.cbl`). As rotinas `_rt_mul_scaled` e `_rt_div_scaled` decompõem quociente e resto antes de escalar, evitando estouro de 32 bits em produtos intermediários. Campos numéricos sem sinal com 10 ou mais dígitos (como `CPF PIC 9(11)` em `BUSCA01A.cbl`) usam rotinas de cadeia de dígitos (`_rt_accept_digits`, `_rt_move_digits`, `_rt_cmp_digits`) para não depender de inteiros de 32 bits. Máscaras editadas (`PIC ZZZ.ZZZ.ZZZ,ZZ`, `PIC -ZZZZZZZZZ9.99`) são formatadas em tempo de execução por `_rt_format_edited`, respeitando `DECIMAL-POINT IS COMMA`.
3. Controle de fluxo fiel ao COBOL via pilha de `PERFORM`: cada seção e parágrafo é emitido em sequência com um identificador global. Ao entrar em um `PERFORM` (simples, `THRU`, `TIMES`, `UNTIL` ou `VARYING`), `_rt_perf_push` empilha o bloco final esperado e o endereço de retorno; ao fim de cada bloco, `_rt_perf_chk` verifica o topo da pilha e retorna se houver correspondência ou segue para o bloco seguinte. Isso resolve de forma uniforme queda sequencial entre parágrafos, seções com múltiplos parágrafos, `PERFORM` recursivo (como em `CALCMD01.cbl`) e desvios `GO TO` internos ou recursivos dentro de uma seção (como `0014-END`, `0015-VALIDAR-CARGO` e `0016-VALIDAR-EMAIL` em `CAD0002A.cbl`) sem vazar espaço na pilha `$sp`.
4. Subprogramas (`CALL`) e `LINKAGE SECTION`: antes da emissão, o gerador varre a AST atrás de chamadas literais `CALL 'NOME'` (como `CALL 'PROGDATA' USING COD001A-REGISTRO` em `PROG024A.cbl`), localiza `PROGDATA.cbl` nos diretórios `-I`, no diretório do arquivo fonte ou em `codigo_fonte_cobol/`, executa lex, parse e análise semântica e emite o subprograma como sub-rotina `cob_sub_PROGDATA` no mesmo `.s`. Cada item `01` da `LINKAGE SECTION` usa um ponteiro indireto `p<id>_lp<v>` pré-inicializado para um buffer padrão, permitindo tanto receber endereços via `$a0`..`$a3` em `CALL ... USING` quanto executar programas de `LINKAGE` de forma independente (caso de `CAD0002A.cbl`).

Uso de ferramentas de IA na Parte 5: o grupo utilizou modelos de linguagem (Gemini via Antigravity) no apoio à escrita das rotinas em assembly MIPS32 de [src/codegen_rt.c](src/codegen_rt.c) e do emissor em [src/codegen.c](src/codegen.c), além da depuração de casos de alinhamento de memória, aritmética de ponto fixo em 32 bits e testes automatizados de execução.

### 5.2 Resultados de compilação, montagem e linkagem MIPS32

O alvo `make test` (equivalente a `make test-mips`) compila todos os 14 programas principais (`cobol-exemplos/*.cob` e `exemplos/*.cbl`) com `./cbc`, monta com `clang -target mipsel-unknown-linux-gnu -mabi=32 -march=mips32 -mno-abicalls -fno-pic` e gera executáveis ELF estáticos MIPS32 com `ld.lld -m elf32ltsmip -static -e __start`. Todos os 14 passam com zero erros de compilação, montagem ou linkagem.

| Programa | Linhas `.s` | ELF `mipsel` (bytes) | Módulos | Resultado |
| ------------------------- | ----------: | -------------------: | ------: | --------- |
| `prog001_divisoes.cob` | 1278 | 13684 | 1 | OK |
| `prog002_operacoes.cob` | 2095 | 18788 | 1 | OK |
| `prog003_calculadora.cob` | 1983 | 18844 | 1 | OK |
| `prog004_folha_pgto.cob` | 1904 | 18012 | 1 | OK |
| `prog005_strings.cob` | 1892 | 17540 | 1 | OK |
| `prog006_files.cob` | 1685 | 16404 | 1 | OK |
| `prog007_loja.cob` | 2606 | 23880 | 1 | OK |
| `BUSCA01A.cbl` | 1556 | 15876 | 1 | OK |
| `CAD0002A.cbl` | 4515 | 41480 | 1 | OK |
| `CAD0004A.cbl` | 1446 | 15004 | 1 | OK |
| `CAD0005A.cbl` | 2650 | 24216 | 1 | OK |
| `CAIXAREG.cbl` | 2215 | 20376 | 1 | OK |
| `CALCMD01.cbl` | 1950 | 18968 | 1 | OK |
| `PROG024A.cbl` | 6748 | 53268 | 2 (`+PROGDATA`) | OK |

Na varredura completa do repositório `codigo_fonte_cobol/` (83 arquivos `.cbl`/`.cob`), todos os **70/70** programas válidos na análise semântica foram compilados, montados e linkados com sucesso tanto para `mipsel` (little-endian) quanto para `mips` (big-endian), com zero falhas de geração ou linkagem. Os 13 arquivos restantes são os mesmos já identificados nas partes anteriores (6 com marcadores de conflito de merge do Git, 2 com `EXEC SQL`, 1 mapa BMS/CICS, 1 em formato livre, 1 totalmente comentado, 1 com identificador acima de 30 caracteres e 1 com variável não declarada).

Trecho do assembly MIPS32 gerado para `cobol-exemplos/prog001_divisoes.cob`:

```mips
# Program : PROG0001
# Target  : Linux MIPS32 o32 ELF (static, no libc)
# Modules : 1
    .set reorder
    .text
    .globl __start
__start:
    j    p0_b0

p0_b0: # PARA MAIN-PROCEDURE
    la   $v0, cob_s0
    li   $v1, 11
    move $a0, $v0
    move $a1, $v1
    jal  _rt_print_bytes
    jal  _rt_print_nl
    la   $v0, cob_s1
    li   $v1, 14
    move $a0, $v0
    move $a1, $v1
    jal  _rt_print_bytes
    jal  _rt_print_nl
    li   $a0, 0
    jal  _rt_exit
```

Inspeção do executável gerado (`file CALCMD01`):

```text
CALCMD01: ELF 32-bit LSB executable, MIPS, MIPS32 version 1 (SYSV), statically linked, not stripped
```

### 5.3 Verificação de execução dos 7 programas avaliados

Todos os sete programas de `exemplos/` foram executados a partir dos binários ELF MIPS32 compilados (simulando entradas de teclado e arquivos temporários), confirmando saída, cálculos decimais, máscaras editadas, validação de campos, manipulação de arquivos sequenciais e chamada de subprograma (`CALL 'PROGDATA'`).

1. `CALCMD01.cbl` (entradas `8.5`, `7.5`, `10`, divisor `5`): somou corretamente em `PIC 9(8)V9999` (`00000026,0000`) e calculou a média `00000005,2000`.
2. `CAIXAREG.cbl` (itens `20.00`, `30.50`, `49.50`, desconto `S` de `10`): exibiu o recibo formatado com `PIC ZZZ.ZZZ.ZZZ,ZZ` (`100,00`, desconto `-10,00`, total final `90,00`).
3. `CAD0004A.cbl` (dois cadastros): gravou os registros de largura fixa no arquivo `CADASTRO.dat` (`Gabriel Monteiro              025123.456.789-01` e `Maria Silva                   030987.654.321-00`).
4. `BUSCA01A.cbl` (busca do CPF de 11 dígitos `98765432100` em `CADASTRO2.dat`): localizou o segundo registro sem estouro de 32 bits (`CPF ENCONTRADO!`, `QUANTIDADE DE REGISTROS LIDOS: 02`).
5. `CAD0002A.cbl` (execução independente com `LINKAGE SECTION` do copybook `COPY002A`): validou nome sem dígitos, idade maior ou igual a 18 via `EVALUATE`, data de nascimento, cargo (`GO TO` interno), e-mail (`INSPECT FUNCTION REVERSE`) e finalizou em `FIM` com `DADOS CADASTRADOS COM SUCESSO!`.
6. `CAD0005A.cbl` (dois cadastros seguidos de listagem): persistiu os contatos em `CADAGENDA.dat`, recarregou na tabela `OCCURS 10` e listou ambos com `QTDE. REGISTROS: 02`.
7. `PROG024A.cbl` (cadastro, empréstimo, listagem, busca por código e devolução): invocou automaticamente o subprograma `PROGDATA.cbl` via `CALL 'PROGDATA' USING COD001A-REGISTRO`, obteve a data do sistema via syscall `4013`, atualizou a tabela `OCCURS 50` e gravou tanto `BIBLIOTECA.dat` quanto o histórico completo em `BIBLIOTECA.txt`:

```text
00101Compiladores                            Aho Ullman          S20/11/2026
CADASTRAR |20/11/2026|00101|Compiladores                            |Aho Ullman          |S|
EMPRESTAR |20/11/2026|00101|Compiladores                            |Aho Ullman          |N|20/11/2026|
DEVOLVER  |20/11/2026|00101|Compiladores                            |Aho Ullman          |S|20/11/2026|20/11/2026
```
