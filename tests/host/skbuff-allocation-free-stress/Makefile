# Host contract harness: the skbuff pool seam under a stress matrix and a
# seeded fuzz layer - in two backends.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/skbuff-allocation-free-stress
#
# Three binaries come out of this directory, one matrix/cases/judge/fuzz set
# driving each backend:
#
#   skbuff-allocation-free-stress        the harness double (skb_contract.c)
#   skbuff-allocation-free-stress-real   the production pool body
#                                        (components/linuxkpi/skbuff/skbuff_pool.c),
#                                        reached through skb_real.c
#   ...-real-mcu                         the same, compiled with the constrained
#                                        geometry bounds and the storage budget
#
# The double keeps the contract's shape check; the real backend is the one the
# acceptance criterion names: the identical nineteen cells, seeded streams and
# judge, run against the code that ships. All three are pure libc; the
# production headers under components/linuxkpi/ are included directly.
#
# Targets:
#   all     the three binaries (default)
#   run     build, then the default backend's matrix run
#   clean   remove the build directory

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT         := $(abspath $(MAKEFILE_DIR)/../../..)

BUILD_DIR := $(MAKEFILE_DIR)/build
BIN       := $(BUILD_DIR)/skbuff-allocation-free-stress
BIN_REAL  := $(BUILD_DIR)/skbuff-allocation-free-stress-real
BIN_MCU   := $(BUILD_DIR)/skbuff-allocation-free-stress-real-mcu

CC     ?= cc
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -ggdb3 -O1 \
          -fsanitize=address,undefined -fno-omit-frame-pointer \
          -I$(ROOT)/components -I$(ROOT)/components/linuxkpi \
          -I$(ROOT)/components/linuxkpi/include -I$(MAKEFILE_DIR)
LDFLAGS := -fsanitize=address,undefined

SHARED_SRCS := judge.c cases.c fuzz.c main.c

# The double build carries skb_contract.c's implementation of the observation
# face; the real builds carry skb_real.c's, which includes the production
# translation unit. The two never link together - each defines the same face.
DOUBLE_SRCS := skb_contract.c $(SHARED_SRCS)
REAL_SRCS   := skb_real.c $(SHARED_SRCS)

# The bounded build: eight slots, 256-byte payloads, and the pool's own
# storage-budget assertion turned on, so the MCU-shaped claim is a build-time
# fact rather than a comment.
MCU_CFLAGS := -DCONFIG_LKPI_SKB_POOL_NUM=8 -DCONFIG_LKPI_SKB_DATA_POOL_NUM=8 \
              -DCONFIG_LKPI_SKB_DATA_BUF_SZ=256 \
              -DCONFIG_LKPI_SKB_POOL_BUDGET_BYTES=8192

DOUBLE_OBJS := $(addprefix $(BUILD_DIR)/double/,$(DOUBLE_SRCS:.c=.o))
REAL_OBJS   := $(addprefix $(BUILD_DIR)/real/,$(REAL_SRCS:.c=.o))
MCU_OBJS    := $(addprefix $(BUILD_DIR)/mcu/,$(REAL_SRCS:.c=.o))

# Objects depend on the production headers too: a header edit must rebuild every
# backend (a stale object here is a false green - the trap this suite's sibling
# round already paid for once).
HDRS := $(MAKEFILE_DIR)/skb_contract.h $(MAKEFILE_DIR)/judge.h \
        $(wildcard $(ROOT)/components/linuxkpi/*.h) \
        $(wildcard $(ROOT)/components/linuxkpi/skbuff/*.h) \
        $(wildcard $(ROOT)/components/linuxkpi/include/linux/*.h) \
        $(wildcard $(ROOT)/components/linuxkpi/include/net/*.h)

.PHONY: all run clean

all: $(BIN) $(BIN_REAL) $(BIN_MCU)

$(BUILD_DIR)/double $(BUILD_DIR)/real $(BUILD_DIR)/mcu:
	mkdir -p $@

$(BUILD_DIR)/double/%.o: $(MAKEFILE_DIR)/%.c $(HDRS) | $(BUILD_DIR)/double
	$(CC) $(CFLAGS)  -MMD -MP -c $< -o $@

$(BUILD_DIR)/real/%.o: $(MAKEFILE_DIR)/%.c $(HDRS) | $(BUILD_DIR)/real
	$(CC) $(CFLAGS)  -MMD -MP -c $< -o $@

$(BUILD_DIR)/mcu/%.o: $(MAKEFILE_DIR)/%.c $(HDRS) | $(BUILD_DIR)/mcu
	$(CC) $(CFLAGS) $(MCU_CFLAGS)  -MMD -MP -c $< -o $@

$(BIN): $(DOUBLE_OBJS)
	$(CC) $(DOUBLE_OBJS) $(LDFLAGS) -o $@

$(BIN_REAL): $(REAL_OBJS)
	$(CC) $(REAL_OBJS) $(LDFLAGS) -o $@

$(BIN_MCU): $(MCU_OBJS)
	$(CC) $(MCU_OBJS) $(LDFLAGS) -o $@

run: $(BIN)
	$(BIN)

# Header dependencies: -MMD -MP writes one .d per object, so a
# header edit rebuilds every object that included it (a stale object
# here once linked two disagreeing struct layouts into one binary).
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD_DIR)
