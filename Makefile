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

.PHONY: all

all: $(BIN)

$(LEX_GEN): $(LEX_SRC)
	$(LEX) -o $(LEX_GEN) $(LEX_SRC)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(BIN) $(LEX_GEN)

help:
	@echo "Available targets:"
	@echo "  all      - Build compiler (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Show this menu"
	@echo ""
