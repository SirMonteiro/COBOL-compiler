BIN := cbc

SRC_DIR := src
INC_DIR := include
GIT_VERSION := $(shell git describe --tags --always --dirty)

LEX_SRC  := $(SRC_DIR)/lexer.l
LEX_GEN  := $(SRC_DIR)/lex.yy.cpp
YACC_SRC := $(SRC_DIR)/parser.y
YACC_GEN := $(SRC_DIR)/parser.tab.cpp
YACC_HDR := $(SRC_DIR)/parser.tab.hpp

C_SRCS   := $(filter-out $(SRC_DIR)/lex.yy.c $(SRC_DIR)/parser.tab.c,$(wildcard $(SRC_DIR)/*.c))
CPP_SRCS := $(filter-out $(LEX_GEN) $(YACC_GEN),$(wildcard $(SRC_DIR)/*.cpp)) $(LEX_GEN) $(YACC_GEN)
OBJS     := $(C_SRCS:.c=.o) $(CPP_SRCS:.cpp=.o)

CXX  ?= g++
LEX  := flex
YACC := bison
RM   := rm
CXXFLAGS := -O3 -std=c++17 -Wall -Wformat -I$(INC_DIR) -I$(SRC_DIR) -DGIT_VERSION=\"$(GIT_VERSION)\"
LDLIBS :=

MIPS_AS := clang -target mipsel-unknown-linux-gnu -mabi=32 -march=mips32 -mno-abicalls -fno-pic -c
MIPS_LD := ld.lld -m elf32ltsmip -static -e __start

TEST_SRCS := $(sort $(wildcard cobol-exemplos/*.cob) $(wildcard exemplos/*.cbl))

.PHONY: all test test-mips test-sema test-parse test-lex clean help

all: $(BIN)

$(LEX_GEN): $(LEX_SRC)
	$(LEX) -o $(LEX_GEN) $(LEX_SRC)

$(YACC_GEN) $(YACC_HDR): $(YACC_SRC)
	$(YACC) -d -o $(YACC_GEN) $(YACC_SRC)

$(BIN): $(OBJS)
	$(CXX) $^ -o $@ $(LDLIBS)

$(SRC_DIR)/lex.yy.o: $(LEX_GEN) $(YACC_HDR)
	$(CXX) $(CXXFLAGS) -Wno-register -c $< -o $@

%.o: %.c $(YACC_HDR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cpp $(YACC_HDR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test-mips

test-mips: $(BIN)
	@pass=0; fail=0; tmp=$$(mktemp -d); \
	for f in $(TEST_SRCS); do \
		base=$$(basename "$$f"); \
		if ./$(BIN) "$$f" -o "$$tmp/$$base.s" 2>/dev/null && \
		   $(MIPS_AS) "$$tmp/$$base.s" -o "$$tmp/$$base.o" 2>/dev/null && \
		   $(MIPS_LD) "$$tmp/$$base.o" -o "$$tmp/$$base.elf" 2>/dev/null; then \
			printf "  [ OK ]  %s  (mipsel ELF)\n" "$$f"; pass=$$((pass+1)); \
		else \
			printf "  [FAIL]  %s\n" "$$f"; fail=$$((fail+1)); \
		fi; \
	done; \
	rm -rf "$$tmp"; \
	echo ""; echo "mips passed: $$pass  failed: $$fail"; \
	[ $$fail -eq 0 ]

test-sema: $(BIN)
	@pass=0; fail=0; \
	for f in $(TEST_SRCS); do \
		if ./$(BIN) -s "$$f" >/dev/null 2>&1; then \
			printf "  [ OK ]  %s\n" "$$f"; pass=$$((pass+1)); \
		else \
			printf "  [FAIL]  %s\n" "$$f"; fail=$$((fail+1)); \
		fi; \
	done; \
	echo ""; echo "sema passed: $$pass  failed: $$fail"; \
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
	$(RM) -f $(OBJS) $(BIN) $(LEX_GEN) $(YACC_GEN) $(YACC_HDR) \
	         $(SRC_DIR)/*.o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.tab.h

help:
	@echo "Available targets:"
	@echo "  all        - Build compiler (default)"
	@echo "  test       - Compile, assemble and link all test programs to MIPS32 ELF (alias: test-mips)"
	@echo "  test-mips  - Run full pipeline + clang/ld.lld MIPS32 ELF build on test programs"
	@echo "  test-sema  - Run lex + parse + semantic analysis (-s) on test programs"
	@echo "  test-parse - Run lex + parse only (-p) on test programs"
	@echo "  test-lex   - Run lexer only (-l) on test programs"
	@echo "  clean      - Remove build artifacts"
	@echo "  help       - Show this menu"
