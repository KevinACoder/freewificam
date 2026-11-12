# tests/host/kpi/Makefile - the host semantic bench.
#
# One binary for the x86 host: the component's real kernel-API bodies, the
# upstream FreeRTOS POSIX port, the CMSIS-RTOS2 adapter, the host OS port, and
# the cases.  It is deliberately not part of the image build and not a profile:
# no board, no linker script, no Kconfig target.
#
#   make -C tests/host/kpi run            build, run, then run --break (must fail)
#   make -C tests/host/kpi clean
#
# The configuration header comes from a real profile (the component profile is
# the natural one); ./make.rtos host-test generates it first.

ROOT := $(abspath $(CURDIR)/../../..)
PLATFORM := $(ROOT)/tests/board/rtos/platform
KERNEL := $(ROOT)/third-party/FreeRTOS-Kernel
ADAPTER := $(ROOT)/third-party/CMSIS-FreeRTOS
CMSIS := $(ROOT)/third-party/CMSIS_6
LIB8 := $(ROOT)/components/liblinux80211
ULOG := $(ROOT)/third-party/ulog/src

PROFILE ?= rk3568-kpi-mock
CONFIG_DIR := $(ROOT)/build/kconfig/$(PROFILE)

CC ?= cc
BUILD := $(CURDIR)/build
BIN := $(BUILD)/kpi-test

HOST_CFLAGS := -std=gnu11 -O1 -g -Wall -Wextra \
	-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
	-Wno-unused-function \
	-fno-strict-aliasing -fno-strict-overflow -fno-delete-null-pointer-checks

INCLUDES := \
	-I$(CONFIG_DIR) \
	-I$(CURDIR) \
	-I$(PLATFORM) \
	-I$(ROOT)/tests/board/rtos/app \
	-I$(PLATFORM)/os/freertos \
	-I$(PLATFORM)/os/cmsis2 \
	-I$(PLATFORM)/os/cmsis2/host-compat \
	-I$(KERNEL)/include \
	-I$(KERNEL)/portable/ThirdParty/GCC/Posix \
	-I$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Include \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-I$(CMSIS)/CMSIS/Core/Include \
	-I$(LIB8)/include \
	-I$(LIB8)/include/linux80211 \
	-I$(LIB8) \
	-I$(LIB8)/bus \
	-I$(ULOG)

FORCE := -include $(PLATFORM)/host_freertos_defs.h

SRCS := \
	$(LIB8)/linuxkpi_core.c \
	$(LIB8)/linuxkpi_compat.c \
	$(LIB8)/linuxkpi_osdep.c \
	$(LIB8)/linuxkpi_netdev.c \
	$(LIB8)/linuxkpi_containers.c \
	$(LIB8)/linuxkpi_device.c \
	$(LIB8)/linuxkpi_rfkill.c \
	$(LIB8)/linuxkpi_netlink.c \
	$(LIB8)/linuxkpi_util.c \
	$(LIB8)/linuxkpi_crypto.c \
	$(LIB8)/fw_registry.c \
	$(LIB8)/net_egress.c \
	$(LIB8)/hostap_rx_wrap.c \
	$(LIB8)/skbuff/skbuff_pool.c \
	$(KERNEL)/tasks.c \
	$(KERNEL)/list.c \
	$(KERNEL)/queue.c \
	$(KERNEL)/event_groups.c \
	$(KERNEL)/stream_buffer.c \
	$(KERNEL)/timers.c \
	$(KERNEL)/portable/MemMang/heap_4.c \
	$(KERNEL)/portable/ThirdParty/GCC/Posix/port.c \
	$(KERNEL)/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c \
	$(ADAPTER)/CMSIS/RTOS2/FreeRTOS/Source/cmsis_os2.c \
	$(PLATFORM)/os/freertos/cmsis2_ext.c \
	$(PLATFORM)/os/freertos/cmsis2_host.c \
	$(PLATFORM)/os/freertos/freertos_hooks.c \
	$(ROOT)/tests/board/rtos/app/port/clock_host.c \
	$(CURDIR)/main.c \
	$(CURDIR)/host_log.c \
	$(CURDIR)/host_stubs.c \
	$(CURDIR)/cases/work.c \
	$(CURDIR)/cases/timer.c \
	$(CURDIR)/cases/jiffies.c \
	$(CURDIR)/cases/sync.c \
	$(CURDIR)/cases/skb.c \
	$(CURDIR)/cases/skbgeom.c \
	$(CURDIR)/cases/macros.c \
	$(CURDIR)/cases/net_stubs.c \
	$(CURDIR)/cases/idr_rb.c \
	$(CURDIR)/cases/nla.c \
	$(CURDIR)/cases/netdev.c \
	$(CURDIR)/cases/net_xmit.c \
	$(CURDIR)/cases/ctx.c \
	$(CURDIR)/cases/hostap_rx.c \
	$(CURDIR)/cases/selftest.c

OBJS := $(patsubst %,$(BUILD)/obj/%.o,$(basename $(notdir $(SRCS))))

.PHONY: all run clean

all: $(BIN)

# One explicit rule per source: the sources are absolute paths outside this
# directory, and a pattern rule would look for them relative to the cwd.
define HOST_OBJ
$(BUILD)/obj/$(basename $(notdir $(1))).o: $(1)
	@mkdir -p $(BUILD)/obj
	$$(CC) $$(HOST_CFLAGS) $$(INCLUDES) $$(FORCE) -c $(1) -o $$@
endef
$(foreach s,$(SRCS),$(eval $(call HOST_OBJ,$(s))))

$(BIN): $(OBJS)
	$(CC) $(OBJS) -pthread -o $@

run: $(BIN)
	@echo "== kpi-test (normal: must be green) =="
	$(BIN)
	@echo "== kpi-test --break (negative control: must be red) =="
	@if $(BIN) --break; then \
		echo "kpi-test: FAIL: --break exited 0 (the bench cannot go red)" >&2; \
		exit 1; \
	else \
		echo "kpi-test: selftest red ok"; \
	fi

clean:
	rm -rf $(BUILD)
