BIN := cbc

SRC_DIR := src
INC_DIR := include
GIT_VERSION := $(shell git describe --tags --always --dirty)

LEX_SRC  := $(SRC_DIR)/lexer.l
LEX_GEN  := $(SRC_DIR)/lex.yy.c
YACC_SRC := $(SRC_DIR)/parser.y
YACC_GEN := $(SRC_DIR)/parser.tab.c
YACC_HDR := $(SRC_DIR)/parser.tab.h

SRCS := $(filter-out $(LEX_GEN) $(YACC_GEN),$(wildcard $(SRC_DIR)/*.c)) $(LEX_GEN) $(YACC_GEN)
OBJS := $(SRCS:.c=.o)

CC   ?= gcc
LEX  := flex
YACC := bison
RM   := rm
CFLAGS := -O3 -Wall -Wformat -I$(INC_DIR) -I$(SRC_DIR) -DGIT_VERSION=\"$(GIT_VERSION)\"
LDLIBS :=

TEST_SRCS := $(sort $(wildcard cobol-exemplos/*.cob) $(wildcard exemplos/*.cbl))

.PHONY: all test test-parse test-lex clean help

all: $(BIN)

$(LEX_GEN): $(LEX_SRC)
	$(LEX) -o $(LEX_GEN) $(LEX_SRC)

$(YACC_GEN) $(YACC_HDR): $(YACC_SRC)
	$(YACC) -d -o $(YACC_GEN) $(YACC_SRC)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

%.o: %.c $(YACC_HDR)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(BIN)
	@pass=0; fail=0; \
	for f in $(TEST_SRCS); do \
		if ./$(BIN) "$$f" >/dev/null 2>&1; then \
			printf "  [ OK ]  %s\n" "$$f"; pass=$$((pass+1)); \
		else \
			printf "  [FAIL]  %s\n" "$$f"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; echo "passed: $$pass  failed: $$fail"; \
	[ $$fail -eq 0 ]

test-parse: $(BIN)
	@pass=0; fail=0; \
	for f in $(TEST_SRCS); do \
		if ./$(BIN) -p "$$f" >/dev/null 2>&1; then \
			printf "  [ OK ]  %s\n" "$$f"; pass=$$((pass+1)); \
		else \
			printf "  [FAIL]  %s\n" "$$f"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; echo "parse passed: $$pass  failed: $$fail"; \
	[ $$fail -eq 0 ]

test-lex: $(BIN)
	@pass=0; fail=0; \
	for f in $(TEST_SRCS); do \
		if ./$(BIN) -l "$$f" >/dev/null 2>&1; then \
			printf "  [ OK ]  %s\n" "$$f"; pass=$$((pass+1)); \
		else \
			printf "  [FAIL]  %s\n" "$$f"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; echo "lex passed: $$pass  failed: $$fail"; \
	[ $$fail -eq 0 ]

clean:
	$(RM) -f $(OBJS) $(BIN) $(LEX_GEN) $(YACC_GEN) $(YACC_HDR)

help:
	@echo "Available targets:"
	@echo "  all        - Build compiler (default)"
	@echo "  test       - Run lex + parse + semantic analysis on cobol-exemplos/*.cob and exemplos/*.cbl"
	@echo "  test-parse - Run lex + parse only (-p) on test programs"
	@echo "  test-lex   - Run lexer only (-l) on test programs"
	@echo "  clean      - Remove build artifacts"
	@echo "  help       - Show this menu"
