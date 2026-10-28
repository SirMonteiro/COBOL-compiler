BIN := cbc

SRC_DIR := src
INC_DIR := include
GIT_VERSION := $(shell git describe --tags --always --dirty)

LEX_SRC := $(SRC_DIR)/lexer.l
LEX_GEN := $(SRC_DIR)/lex.yy.c
SRCS := $(filter-out $(LEX_GEN),$(wildcard $(SRC_DIR)/*.c)) $(LEX_GEN)
OBJS := $(SRCS:.c=.o)

CC  ?= gcc
LEX := flex
RM  := rm
CFLAGS := -O3 -Wall -Wformat -I$(INC_DIR) -DGIT_VERSION=\"$(GIT_VERSION)\"
LDLIBS :=

TEST_SRCS := $(sort $(wildcard cobol-exemplos/*.cob) $(wildcard exemplos/*.cbl))

.PHONY: all test clean help

all: $(BIN)

$(LEX_GEN): $(LEX_SRC)
	$(LEX) -o $(LEX_GEN) $(LEX_SRC)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

%.o: %.c
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

clean:
	$(RM) -f $(OBJS) $(BIN) $(LEX_GEN)

help:
	@echo "Available targets:"
	@echo "  all   - Build compiler (default)"
	@echo "  test  - Run cbc on cobol-exemplos/*.cob and exemplos/*.cbl"
	@echo "  clean - Remove build artifacts"
	@echo "  help  - Show this menu"
