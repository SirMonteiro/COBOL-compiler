BIN := cbc

SRC_DIR := src
INC_DIR := include

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:.c=.o)

CC ?= gcc
RM := rm
CFLAGS := -O3 -Wall -Wformat -I\$(INC_DIR)
LDLIBS :=

.PHONY: all

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(BIN)

help:
	@echo "Available targets:"
	@echo "  all      - Build compiler (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Show this menu"
	@echo ""
