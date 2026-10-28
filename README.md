# Compilador COBOL (`cbc`) - ACH2087

Projeto desenvolvido para a disciplina [ACH2087 - Construção de Compiladores](https://uspdigital.usp.br/jupiterweb/obterDisciplina?sgldis=ACH2087&codcur=86200&codhab=204) (EACH-USP, 2º semestre de 2026), seguindo a especificação definida em [EspecificacaoCOBOL.md](EspecificacaoCOBOL.md).

Grupo:

- Adryelli Reis dos Santos (14714019)
- André Vieira Rocha (12681611)
- Gabriel Monteiro de Souza (14746450)

Repositório público: https://github.com/SirMonteiro/COBOL-compiler

## 2.1 Introdução

O COBOL (*Common Business-Oriented Language*) surgiu em 1959 a partir do comitê CODASYL, apoiado pelo Departamento de Defesa dos Estados Unidos, com o objetivo de criar uma linguagem voltada ao processamento comercial que pudesse ser lida quase como texto em inglês ([GnuCOBOL FAQ](https://gnucobol.sourceforge.io/faq/index.html#history)). Apesar da idade, a linguagem continua em produção em sistemas bancários, folhas de pagamento, seguradoras e órgãos públicos, principalmente pelo volume de código legado já validado e pelo custo e risco de reescrever regras de negócio que operam em lote há décadas.

Construir um analisador léxico para COBOL em formato fixo traz alguns problemas práticos que não aparecem em linguagens de formato livre:

1. Cada linha física divide-se em Área de Sequência (colunas 1 a 6), Área de Indicador (coluna 7, com espaço em branco, `*` ou `/` para comentário, `-` para continuação e `D` para depuração), Área A (colunas 8 a 11), Área B (colunas 12 a 72) e Área do Programa (colunas 73 a 80, ignorada pelo compilador). Arquivos editados com tabulações (`\t`) exigem expansão prévia para múltiplos de 8 colunas para que a fronteira entre Área A e Área B não se desloque.
2. A posição inicial do token define se um elemento pertence à Área A (cabeçalhos de divisão, seções, parágrafos e níveis `01` e `77`) ou à Área B (comandos, cláusulas e níveis subordinados). Por isso o lexer precisa registrar a coluna e a área em cada token emitido.
3. Literais alfanuméricos abertos e identificadores longos podem terminar na coluna 72 e continuar na linha seguinte marcada com `-` na coluna 7, como acontece com `TAB-TRABALHADOR` nos programas de teste e com strings maiores que uma linha.
4. O ponto final `.` encerra sentenças e parágrafos, mas também aparece dentro de literais numéricos (`-123.45`) e de especificações de formato (`PIC ZZ9.99.`). Máscaras como `PIC -ZZZZZZZZZ9` começam com `-` e seriam confundidas com identificadores inválidos se lidas fora do contexto de `PIC` ou `PICTURE`.
5. A linguagem reúne centenas de palavras reservadas insensíveis a maiúsculas e minúsculas e aceita hífens internos em identificadores de até 30 caracteres (`WRK-VALOR1`), proibindo hífen no início ou no fim.

## 2.2 Implementação

O analisador léxico (`cbc`) foi implementado em C com suporte do gerador léxico `flex`, compilado com `gcc` (`-O3 -Wall -Wformat`). Toda a leitura respeita o formato fixo especificado na Seção 2 de [EspecificacaoCOBOL.md](EspecificacaoCOBOL.md).

A base de código está organizada em quatro módulos principais:

- [include/read.h](include/read.h) e [src/read.c](src/read.c): implementam `read_cobol_line`, que expande tabulações (`expand_tabs` com largura 8), extrai a área de sequência (`parse_seq`), valida o indicador da coluna 7 (espaço, `*`, `/`, `-`, `D`/`d`), separa comentários quando `-c` está ativo, descarta linhas de depuração sem `-d` e recorta exatamente as colunas 8-72 (`code_slice`). Indicadores inválidos retornam erro severo `[S]`.
- [src/lexer.l](src/lexer.l): define as regras do Flex com `%option caseless`, comentários inline `*>`, palavras reservadas agrupadas (`KW_DIVISIONS`, `KW_VERBS`, `KW_ENDS`, `KW_CLAUSES`, `KW_MISC`, `KW_FIGURATIVE`), literais numéricos com sinal e ponto decimal interno, strings entre aspas simples ou duplas (aceitando aspas dobradas como escape), operadores aritméticos e relacionais (`+`, `-`, `*`, `/`, `**`, `=`, `<>`, `<`, `<=`, `>`, `>=`) e pontuação (`.`, `,`, `;`, `(`, `)`, `:`). Para cláusulas `PIC`/`PICTURE [IS]`, o scanner entra no estado exclusivo `%x PICSTR`, captura a máscara como um único `TOKEN_PICTURE` e devolve eventual `.` final ao fluxo via `yyless` quando ele encerra a sentença. Identificadores aceitam `A-Z`, `0-9`, `_` (compatibilidade com `PROGRAM-ID` encontrado nos exemplos) e `-` interno, validando o limite de 30 caracteres e rejeitando hífen inicial ou final.
- [include/lexer.h](include/lexer.h) e [src/lexer_support.c](src/lexer_support.c): implementam `emit_token` (normalizando palavras-chave e identificadores para maiúsculas e registrando sequência, coluna e área), `emit_lexical_error` (reportando erro `[E]` em `stderr` e emitindo `TOKEN_UNKNOWN` para não interromper a varredura) e um mecanismo de lookahead de uma linha (`lexer_begin_file`, `lexer_process_line`, `lexer_end_file`) que trata linhas de continuação `-` tanto para literais alfanuméricos abertos na coluna 72 quanto para identificadores divididos entre a coluna 72 e a linha seguinte.
- [src/main.c](src/main.c): processa argumentos via `getopt_long` (`-h`, `-v`, `-o`, `-d`, `-c`), aceita múltiplos arquivos de entrada, usa o número da linha física quando as colunas 1-6 estão em branco, descarta sobras de linhas maiores que o buffer de leitura e retorna código de saída diferente de zero quando ocorrem erros léxicos ou de indicador.

### Uso de ferramentas de IA

Durante o desenvolvimento desta primeira entrega, o grupo usou modelos de linguagem (Claude e Gemini via Antigravity) como apoio nas seguintes atividades:

- Revisão das regras léxicas em [src/lexer.l](src/lexer.l), incluindo o agrupamento de palavras reservadas além do subconjunto mínimo da especificação e o tratamento da condição inicial `PICSTR`.
- Verificação de casos de borda do formato fixo (expansão de tabulações, continuação na coluna 72 e comportamento diante de literais não fechados).
- Organização, verificação dos exemplos de saída e revisão textual deste relatório.

O desenho modular do leitor de colunas, a interface de linha de comando e a estrutura geral do compilador em C foram escritos manualmente pela equipe.

### Compilação e execução

```sh
make          # compila o binário ./cbc
./cbc cobol-exemplos/prog001_divisoes.cob
make test     # executa ./cbc em cobol-exemplos/*.cob e exemplos/*.cbl
```

Para gerar a versão em PDF do relatório a partir deste `README.md`:

```sh
make -C relatorio pdf
```

## 2.3 Resultados dos testes

O analisador léxico foi executado sobre os sete programas de teste do diretório [cobol-exemplos/](cobol-exemplos/). Os sete programas adicionais em [exemplos/](exemplos/) também passam com código de saída `0` em `make test`.

| Programa | Descrição | Tokens emitidos | Exit |
| ---------------------------- | ---------------------------------------------- | ---------------: | ---: |
| `prog001_divisoes.cob` | Divisões básicas, seções e `DISPLAY` simples | 76 | 0 |
| `prog002_operacoes.cob` | Operações aritméticas (`ADD`, `SUBTRACT`, `MULTIPLY`, `DIVIDE`, `MOVE`) | 320 | 0 |
| `prog003_calculadora.cob` | Calculadora interativa com `ACCEPT`, `IF`/`ELSE`, `COMPUTE` e máscara `PIC -ZZZZZZZZZ9` | 229 | 0 |
| `prog004_folha_pgto.cob` | Tabela (`OCCURS`), `PERFORM ... UNTIL` e continuação de identificador quebrado na coluna 72 | 221 | 0 |
| `prog005_strings.cob` | Manipulação de cadeias com `INSPECT`, `STRING` e `UNSTRING` | 236 | 0 |
| `prog006_files.cob` | Manipulação de arquivos sequenciais (`SELECT`, `FD`, `OPEN`, `WRITE`, `CLOSE`) | 225 | 0 |
| `prog007_loja.cob` | Cadastro de produtos com `EVALUATE`, leitura/escrita indexada e `INVALID KEY` | 566 | 0 |

Os fluxos completos de tokens gerados por `./cbc` para cada programa de `cobol-exemplos/` estão listados abaixo.

<details>
<summary>prog001_divisoes.cob (76 tokens, exit 0)</summary>

```text
$ ./cbc cobol-exemplos/prog001_divisoes.cob
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
[SEQ 000023 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : SECURITY
[SEQ 000023 | COL 16 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000023 | COL 18 (AREA B)] TOKEN_IDENTIFIER           : O
[SEQ 000023 | COL 20 (AREA B)] TOKEN_IDENTIFIER           : PROGRAMA
[SEQ 000023 | COL 29 (AREA B)] TOKEN_IDENTIFIER           : USA
[SEQ 000023 | COL 33 (AREA B)] TOKEN_IDENTIFIER           : CRIPTOGRAFIA
[SEQ 000023 | COL 46 (AREA B)] TOKEN_IDENTIFIER           : XPTO
[SEQ 000023 | COL 50 (AREA B)] TOKEN_PERIOD               : .
[SEQ 000024 | COL 08 (AREA A)] TOKEN_IDENTIFIER           : REMARKS
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
$ ./cbc cobol-exemplos/prog002_operacoes.cob
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
$ ./cbc cobol-exemplos/prog003_calculadora.cob
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
$ ./cbc cobol-exemplos/prog004_folha_pgto.cob
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
$ ./cbc cobol-exemplos/prog005_strings.cob
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
[SEQ 000032 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : FOR
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
[SEQ 000036 | COL 40 (AREA B)] TOKEN_IDENTIFIER           : FOR
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
[SEQ 000041 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : FOR
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
[SEQ 000046 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : FOR
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
[SEQ 000051 | COL 21 (AREA B)] TOKEN_IDENTIFIER           : FOR
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
$ ./cbc cobol-exemplos/prog006_files.cob
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
$ ./cbc cobol-exemplos/prog007_loja.cob
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


## 2.4 Resultados de erros

Quando encontra um indicador inválido na coluna 7, o leitor reporta um erro severo `[S]` em `stderr` e ignora a linha. Quando encontra um problema léxico nas colunas 8-72 (caractere inválido, identificador malformado ou acima de 30 caracteres, literal não terminado ou continuação incorreta), `emit_lexical_error` imprime o diagnóstico `[E]` em `stderr` com sequência e coluna, incrementa o contador de erros (fazendo o programa retornar código `1`) e emite `TOKEN_UNKNOWN` para que o restante do arquivo continue sendo analisado.

Abaixo estão os dez erros inseridos nos sete programas de teste e as respectivas saídas produzidas por `./cbc`.

### 1. Literal alfanumérico não terminado (`prog001_divisoes.cob`)

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

### 2. Indicador inválido na coluna 7 (`prog001_divisoes.cob`)

Uso do caractere `X` na coluna 7, fora do conjunto permitido (espaço, `*`, `/`, `-`, `D`, `d`). O erro severo `[S]` descarta a linha.

```cobol
      X     DISPLAY "INVALID INDICATOR".
```

Saída em `stderr`:

```text
[S] Indicator Error (cobol-exemplos/prog001_divisoes.cob, Seq 000055): Invalid indicator 'X' in Column 7.
```

### 3. Identificador terminado em hífen (`prog002_operacoes.cob`)

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

### 4. Caractere fora da linguagem (`prog002_operacoes.cob`)

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

### 5. Identificador acima de 30 caracteres (`prog003_calculadora.cob`)

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

### 6. Operador desconhecido (`prog003_calculadora.cob`)

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

### 7. Identificador iniciado com hífen (`prog004_folha_pgto.cob`)

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

### 8. Aspas incompatíveis no literal (`prog005_strings.cob`)

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

### 9. Continuação com aspa diferente da abertura (`prog006_files.cob`)

Literal aberto até a coluna 72 com `"` e continuado na linha `-` com `'`. O lexer reporta tanto o literal não terminado quanto o erro de continuação.

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

### 10. Literal continuado e nunca fechado (`prog007_loja.cob`)

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

