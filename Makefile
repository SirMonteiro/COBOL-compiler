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

# --- test configuration ---
TEST_DIR   := exemplos
TEST_SRCS  := $(sort $(wildcard $(TEST_DIR)/*.cob))
TEST_OUTS  := $(TEST_SRCS:.cob=.out)
# Extra flags passed to cbc
CBCFLAGS   ?=

# --- GnuCOBOL comparison (make compare) ---
COBC       ?= cobc
COBCFLAGS  ?= -fixed
CMP_PP     := $(TEST_SRCS:.cob=.pp)
CMP_TOK    := $(TEST_SRCS:.cob=.tok)
CMP_DIFF   := $(TEST_SRCS:.cob=.diff)
CMP_LOG    := $(TEST_SRCS:.cob=.cobc)

# awk filter applied to cbc's token lines before comparing. Prints just the lexeme of each token.
CMP_AWK    = /^\[SEQ/ { lex = $$0; sub(/^\[SEQ[^]]*\] TOKEN_[A-Z_]+ +: /, "", lex); \
  areaA = ($$0 ~ /\(AREA A\)/); \
  if (skip && !areaA) next; skip = 0; \
  u = toupper(lex); \
  if (areaA && (u=="AUTHOR" || u=="INSTALLATION" || u=="DATE-WRITTEN" || u=="DATE-COMPILED" || u=="SECURITY" || u=="REMARKS")) { skip = 1; next } \
  print lex }

.PHONY: all test compare test-clean clean help

all: $(BIN)

$(LEX_GEN): $(LEX_SRC)
	$(LEX) -o $(LEX_GEN) $(LEX_SRC)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Runs cbc for every exemplos/*.cob and saves the result to *.out
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

# Compare lexer against GnuCOBOL, per program in exemplos/:
#   NAME.pp    GnuCOBOL's preprocessed source (cobc -E: comments removed, continuation
#              lines joined, tabs expanded)
#   NAME.tok   our token stream (cbc stdout)
#   NAME.cobc  cobc -fsyntax-only diagnostics (is the program itself valid COBOL?)
#   NAME.diff  only when they differ: expected (cobc) vs actual (cbc), 80 chars/line
# Every lexeme cbc emitted, concatenated with all whitespace removed and
# upper-cased, must equal the cobc-preprocessed source with whitespace removed
# and upper-cased.
compare: $(BIN)
	@command -v $(COBC) >/dev/null 2>&1 || { \
		echo "error: '$(COBC)' not found. Install GnuCOBOL or run: make compare COBC=/path/to/cobc"; exit 2; }
	@match=0; differ=0; bad=0; skipped=0; \
	for f in $(TEST_SRCS); do \
		base=$${f%.cob}; \
		rm -f $$base.pp $$base.diff; \
		if ! $(COBC) $(COBCFLAGS) -E $$f -o $$base.pp >$$base.cobc 2>&1; then \
			printf "  [SKIP ]  %s   (cobc cannot preprocess it, see %s)\n" $$f $$base.cobc; \
			skipped=$$((skipped+1)); continue; \
		fi; \
		if $(COBC) $(COBCFLAGS) -fsyntax-only $$f >>$$base.cobc 2>&1; then \
			syn="cobc syntax: OK"; first=""; \
		else \
			syn="cobc syntax: ERRORS (see $$base.cobc)"; bad=$$((bad+1)); \
			first=$$(grep -m1 'error' $$base.cobc); \
		fi; \
		./$(BIN) $(CBCFLAGS) $$f > $$base.tok 2>/dev/null; \
		grep -v '^#line' $$base.pp | tr -d ' \t\r\n' | tr 'a-z' 'A-Z' | fold -w 80 > $$base.exp.tmp; echo >> $$base.exp.tmp; \
		grep '^\[SEQ' $$base.tok | awk '$(CMP_AWK)' \
			| tr -d ' \t\r\n' | tr 'a-z' 'A-Z' | fold -w 80 > $$base.act.tmp; echo >> $$base.act.tmp; \
		if diff -u --label "cobc (expected)" --label "cbc (actual)" $$base.exp.tmp $$base.act.tmp > $$base.diff; then \
			rm -f $$base.diff; printf "  [MATCH]  %s   (%s)\n" $$f "$$syn"; match=$$((match+1)); \
		else \
			printf "  [DIFF ]  %s   (%s) -> %s\n" $$f "$$syn" $$base.diff; differ=$$((differ+1)); \
		fi; \
		rm -f $$base.exp.tmp $$base.act.tmp; \
		if [ -n "$$first" ]; then printf "           first cobc error: %s\n" "$$first"; fi; \
	done; \
	echo ""; echo "match: $$match  differ: $$differ  skipped: $$skipped  (programs cobc itself rejects: $$bad)"; \
	[ $$differ -eq 0 ]

# Test a single program: make exemplos/prog001_divisoes.out
$(TEST_DIR)/%.out: $(TEST_DIR)/%.cob $(BIN)
	@./$(BIN) $(CBCFLAGS) $< > $@ 2>&1; rc=$$?; \
	echo "### exit code: $$rc" >> $@; \
	echo "$< -> $@ (exit $$rc)"

test-clean:
	$(RM) -f $(TEST_OUTS) $(CMP_PP) $(CMP_TOK) $(CMP_DIFF) $(CMP_LOG)

clean: test-clean
	$(RM) -f $(OBJS) $(BIN) $(LEX_GEN)

help:
	@echo "Available targets:"
	@echo "  all      - Build compiler (default)"
	@echo "  test     - Run cbc on every exemplos/*.cob, saving results to exemplos/*.out"
	@echo "  compare  - Compare cbc token text with GnuCOBOL (cobc -E) per program"
	@echo "  test-clean - Remove exemplos/*.out and compare artifacts"
	@echo "  clean    - Remove build artifacts and test outputs"
	@echo "  help     - Show this menu"
	@echo ""
