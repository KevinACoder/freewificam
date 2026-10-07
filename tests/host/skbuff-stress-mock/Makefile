# Host contract harness: the skbuff pool seam under a stress matrix and a
# seeded fuzz layer.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/skbuff-stress-mock
#
# One binary, skbuff-stress, pure libc: the harness double of the pool body
# (skb_contract.c) plus the matrix, the judge and the seeded random layer.
# The production headers under components/linuxkpi/ are the source of shape
# and are included directly - the include order and the two host-toolchain
# accommodations around them live in skb_contract.h.
#
# Targets:
#   all     the binary (default)
#   run     build, then the default matrix run
#   clean   remove the build directory

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

BUILD_DIR := $(MAKEFILE_DIR)/build
BIN       := $(BUILD_DIR)/skbuff-stress

CC     ?= cc
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -ggdb3 -O1 \
          -fsanitize=address,undefined -fno-omit-frame-pointer \
          -I$(ROOT)/components/linuxkpi -I$(MAKEFILE_DIR)
LDFLAGS := -fsanitize=address,undefined

SRCS := skb_contract.c judge.c cases.c fuzz.c main.c
OBJS := $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))

.PHONY: all run clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: $(MAKEFILE_DIR)/%.c $(MAKEFILE_DIR)/skb_contract.h \
                   $(MAKEFILE_DIR)/judge.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

run: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD_DIR)
