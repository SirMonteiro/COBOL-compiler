BIN := cbc

SRC_DIR := src
INC_DIR := include
GIT_VERSION := $(shell git describe --tags --always --dirty)

LEX_SRC  := $(SRC_DIR)/lexer.l
LEX_GEN  := $(SRC_DIR)/lex.yy.c
SRCS := $(wildcard $(SRC_DIR)/*.c) $(LEX_GEN)
OBJS := $(SRCS:.c=.o)

CC  ?= gcc
LEX := flex
RM  := rm
CFLAGS := -O3 -Wall -Wformat -I\$(INC_DIR) -DGIT_VERSION=\"$(GIT_VERSION)\"
LDLIBS :=

# --- configuração de testes ---
TEST_DIR   := exemplos
TEST_SRCS  := $(sort $(wildcard $(TEST_DIR)/*.cob))
TEST_OUTS  := $(TEST_SRCS:.cob=.out)
# flags extras passadas para cbc
CBCFLAGS   ?=

.PHONY: all test test-clean clean help

all: $(BIN)

$(LEX_GEN): $(LEX_SRC)
	$(LEX) -o $(LEX_GEN) $(LEX_SRC)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Executa cbc em todos exemplos/*.cob e salva resultado para *.out
test: $(BIN)
	@pass=0; fail=0; failed=""; \
	for f in $(TEST_SRCS); do \
		out=$${f%.cob}.out; \
		{ echo "### ./$(BIN) $(CBCFLAGS) $$f"; \
		  echo "### date: $$(date '+%Y-%m-%d %H:%M:%S')"; \
		  echo "### version: $(GIT_VERSION)"; \
		  echo "-----"; } > $$out; \
		./$(BIN) $(CBCFLAGS) $$f >> $$out 2>&1; rc=$$?; \
		{ echo "-----"; echo "### exit code: $$rc"; } >> $$out; \
		if [ $$rc -eq 0 ]; then \
			printf "  [ OK ]  %s\n" $$f; pass=$$((pass+1)); \
		else \
			printf "  [FAIL]  %s (exit %s) -> %s\n" $$f $$rc $$out; \
			fail=$$((fail+1)); failed="$$failed $$f"; \
		fi; \
	done; \
	echo ""; echo "passed: $$pass  failed: $$fail"; \
	[ $$fail -eq 0 ]

# Testa um único exemplo: make exemplos/prog001_divisoes.out
$(TEST_DIR)/%.out: $(TEST_DIR)/%.cob $(BIN)
	@./$(BIN) $(CBCFLAGS) $< > $@ 2>&1; rc=$$?; \
	echo "### exit code: $$rc" >> $@; \
	echo "$< -> $@ (exit $$rc)"

test-clean:
	$(RM) -f $(TEST_OUTS)

clean: test-clean
	$(RM) -f $(OBJS) $(BIN) $(LEX_GEN)

help:
	@echo "Available targets:"
	@echo "  all      - Build compiler (default)"
	@echo "  test     - Run cbc on every exemplos/*.cob, saving results to exemplos/*.out"
	@echo "  test-clean - Remove exemplos/*.out"
	@echo "  clean    - Remove build artifacts and test outputs"
	@echo "  help     - Show this menu"
	@echo ""
