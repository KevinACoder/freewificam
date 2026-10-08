# Host suite: the WLAN bridge chain over a scripted driver.
#
# Self contained; build it from anywhere with:
#   make -C <repo>/tests/host/wlan-bridge-test
#
# The chain under test is the real one: the imported net80211 stack, the bridge's
# two halves (net80211 world + linuxkpi world) and the mac80211 substitute all
# run as their production translation units; only the chip at the far end is
# scripted (fake_driver.c registers an ops table and records every call, and
# ap_frames.c plays a fake AP's answers). The point is to exercise the bridge
# itself - the scan group's ordering, the queue drains, the receive injection,
# the events - on the host, where a broken sequence is a second-long failure
# instead of a board round.
#
# The real driver is linked here for link closure only (its two RTL8188EU units
# plus every chip unit core.c's dev_table references), so a signature or symbol
# mismatch is caught at link time; its register-level bring-up runs on the board,
# where a real chip makes a far better device model than a hand-written guess.
#
# Targets:
#   all      the binary (default)
#   run      build, then run it
#   clean    remove the build directory

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT := $(abspath $(MAKEFILE_DIR)/../../..)

NET   := $(ROOT)/components/net80211
PORT  := $(ROOT)/ports/net80211
LWIP  := $(ROOT)/third-party/lwip
DRV   := $(ROOT)/third-party/rtl8xxxu
FR    := $(ROOT)/third-party/FreeRTOS-Kernel
CMM   := $(ROOT)/third-party/CMSIS_6
ADAPT := $(ROOT)/third-party/CMSIS-FreeRTOS
TX    := $(ROOT)/third-party/threadx

BUILD_DIR := $(MAKEFILE_DIR)/build
BIN := $(BUILD_DIR)/wlan-bridge-test

CC ?= cc

# ---- the BSD-world flags the port layer's own suite uses ---------------------
BSD_CFLAGS := -std=gnu11 -g3 -O0 -Wall -Wno-unused-function -Wno-pointer-sign \
    -D_KERNEL -include $(PORT)/compat/prelude.h \
    -I$(PORT)/compat -I$(NET)/compat/netbsd -I$(NET) -I$(PORT) \
    -I$(PORT)/bridge -I$(ROOT) -I$(MAKEFILE_DIR) \
    -DCMSIS2_BACKEND_THREADX -I$(ROOT)/os/cmsis2 -I$(ROOT)/os/freertos \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(ROOT)/third-party/CMSIS_6/CMSIS/RTOS2/Include

# ---- the linuxkpi-world flags (the substitute + the bridge's driver half) ----
KPI_CFLAGS := -std=gnu11 -Wall -Wextra -DCMSIS2_BACKEND_THREADX \
    -I$(ROOT) -I$(ROOT)/os/freertos -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(ROOT)/components/linuxkpi \
    -I$(ROOT)/components/linuxkpi/include \
    -I$(ROOT)/components/linuxkpi/usb \
    -I$(NET) -I$(PORT) -I$(PORT)/bridge -I$(MAKEFILE_DIR) \
    -I$(CMM)/CMSIS/RTOS2/Include -I$(ADAPT)/CMSIS/RTOS2/FreeRTOS/Include

# the driver units compile against the first-party mac80211 face, the flags the
# driver build suite already proved (-Wall, the pinned-header relaxations)
# the driver units compile exactly as the driver build suite compiles them:
# -O2 matters (the face's BUILD_BUG_ON demands constant folding)
DRV_CFLAGS := -std=c11 -O2 -Wall \
    -DCMSIS_device_header='"cmsis_host_device.h"' \
    -I$(ROOT) -I$(ROOT)/os/freertos -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat \
    -I$(ROOT)/components/linuxkpi \
    -I$(ROOT)/components/linuxkpi/include \
    -I$(CMM)/CMSIS/RTOS2/Include \
    -Wno-unused-parameter

RT_CFLAGS := -ggdb3 -O0 -Wall -D_GNU_SOURCE -DTX_LINUX_MULTI_CORE \
    -DTX_INCLUDE_USER_DEFINE_FILE -DCMSIS2_BACKEND_THREADX \
    -I$(ROOT) -I$(ROOT)/os/threadx -I$(ROOT)/os/cmsis2 \
    -I$(ROOT)/os/cmsis2/host-compat -I$(CMM)/CMSIS/RTOS2/Include \
    -I$(TX)/common/inc -I$(TX)/ports/linux/gnu/inc

# ---- the presentation layer's flags (real wlannetif.c over stubbed lwIP) -----
# The unit under test is ports/lwip/wlannetif.c compiled against the real lwIP
# headers; the lwIP functions it calls are answered by this suite's recording
# stubs. The lwIP component itself is not in this link (its acceptance is the
# cross-build gate plus the board's dataplane), which is the line the design
# draws for the host tier.
NETIF_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror \
    -I$(ROOT)/ports/lwip/include -I$(LWIP)/src/include \
    -I$(ROOT)/ports/lwip -I$(ROOT) -I$(NET) -I$(MAKEFILE_DIR)

# ---- source groups ----------------------------------------------------------

# the imported stack: the same set the port suite compiles
STACK_SRCS := \
    $(NET)/net80211/ieee80211.c \
    $(NET)/net80211/ieee80211_proto.c \
    $(NET)/net80211/ieee80211_node.c \
    $(NET)/net80211/ieee80211_output.c \
    $(NET)/net80211/ieee80211_input.c \
    $(NET)/net80211/ieee80211_amrr.c \
    $(NET)/net80211/ieee80211_netbsd.c \
    $(NET)/net80211/ieee80211_crypto.c \
    $(NET)/net80211/ieee80211_crypto_none.c \
    $(NET)/net80211/ieee80211_crypto_ccmp.c \
    $(NET)/crypto/aes/aes_bear.c \
    $(NET)/crypto/aes/aes_ct.c \
    $(NET)/crypto/aes/aes_ct_enc.c \
    $(NET)/crypto/aes/aes_ct_dec.c \
    $(NET)/crypto/aes/aes_ccm.c \
    $(NET)/crypto/aes/aes_ccm_mbuf.c \
    $(NET)/port/aes_impl_compat.c

PORT_SRCS := \
    $(PORT)/osal/cmsis2/osal_memory.c \
    $(PORT)/osal/cmsis2/osal_cmsis2.c \
    $(PORT)/net/mbuf.c \
    $(PORT)/net/ifnet.c \
    $(PORT)/bridge/wlan_bridge.c \
    $(MAKEFILE_DIR)/n8_peek.c

# the linuxkpi world: the substitute body, the pool it allocates from, the
# bridge's driver half, and this suite's scripted driver
POOL_SRC := $(ROOT)/components/linuxkpi/skbuff/skbuff_pool.c

KPI_SRCS := \
    $(ROOT)/components/linuxkpi/linuxkpi_core.c \
    $(ROOT)/components/linuxkpi/linuxkpi_80211.c \
    $(ROOT)/components/linuxkpi/fw_registry.c \
    $(ROOT)/components/linuxkpi/usb/linuxkpi_usb.c \
    $(PORT)/bridge/wlan_seam_adapter.c \
    $(MAKEFILE_DIR)/fake_driver.c \
    $(MAKEFILE_DIR)/lkpi_platform_host.c

APP_SRCS := $(MAKEFILE_DIR)/main.c $(MAKEFILE_DIR)/ap_frames.c

# the presentation layer: the production wlannetif.c with the lwIP surface
# stubbed and recorded by the suite
NETIF_SRCS := $(ROOT)/ports/lwip/wlannetif.c $(MAKEFILE_DIR)/wlnetif_stub.c

# the seam driver is net80211-world (it calls the port's own delivery paths)
SEAM_SRCS := $(MAKEFILE_DIR)/seam_drive.c

# the real driver: link closure only (see the header note)
DRV_SRCS := \
    $(DRV)/core.c $(DRV)/8188e.c $(DRV)/8188f.c $(DRV)/8192c.c \
    $(DRV)/8192e.c $(DRV)/8192f.c $(DRV)/8710b.c $(DRV)/8723a.c $(DRV)/8723b.c

# The runtime is the ThreadX host lane: the port layer's FreeRTOS run is
# documented as blocked by the upstream handle truncation (the port suite links
# that lane but cannot run it), so the chain runs on the lane the port actually
# runs on - the same runtime the frame-test suite uses.
RT_SRCS := \
    $(ROOT)/os/threadx/tx_glue.c \
    $(ROOT)/os/threadx/heap.c \
    $(ROOT)/os/threadx/cmsis2_ext.c \
    $(ROOT)/os/threadx/cmsis2_threadx.c \
    $(ROOT)/os/threadx/cmsis2_host.c \
    $(wildcard $(TX)/ports/linux/gnu/src/*.c) \
    $(wildcard $(TX)/common/src/*.c)

define OBJ
$(BUILD_DIR)/$(1)/$(subst /,_,$(patsubst $(ROOT)/%,%,$(2))).o
endef

STACK_OBJS := $(foreach s,$(STACK_SRCS),$(call OBJ,stack,$(s)))
SEAM_OBJS  := $(foreach s,$(SEAM_SRCS),$(call OBJ,stack,$(s)))
PORT_OBJS  := $(foreach s,$(PORT_SRCS),$(call OBJ,port,$(s)))
KPI_OBJS   := $(foreach s,$(KPI_SRCS),$(call OBJ,kpi,$(s)))
APP_OBJS   := $(foreach s,$(APP_SRCS),$(call OBJ,app,$(s)))
NETIF_OBJS := $(foreach s,$(NETIF_SRCS),$(call OBJ,netif,$(s)))
DRV_OBJS   := $(foreach s,$(DRV_SRCS),$(call OBJ,drv,$(s)))
RT_OBJS    := $(foreach s,$(RT_SRCS),$(call OBJ,rt,$(s)))

# the pool's platform hooks are the host's (the same switch the sibling suites use)
POOL_OBJ := $(call OBJ,kpi,$(POOL_SRC))
KPI_OBJS += $(POOL_OBJ)

LDFLAGS := -pthread -lrt

.PHONY: all run clean

all: $(BIN)

$(BUILD_DIR)/stack $(BUILD_DIR)/port $(BUILD_DIR)/kpi $(BUILD_DIR)/app \
$(BUILD_DIR)/netif $(BUILD_DIR)/drv $(BUILD_DIR)/rt:
	mkdir -p $@

# Explicit rules (the object name folds the whole source path, so a pattern
# rule cannot express it): one per source group.
define GRP_RULE
$$(call OBJ,$(2),$(1)): $(1) | $(BUILD_DIR)/$(2)
	$$(CC) $(3) -c $$< -o $$@
endef

$(foreach src,$(STACK_SRCS),$(eval $(call GRP_RULE,$(src),stack,$(BSD_CFLAGS))))
$(foreach src,$(SEAM_SRCS),$(eval $(call GRP_RULE,$(src),stack,$(BSD_CFLAGS))))
$(foreach src,$(PORT_SRCS),$(eval $(call GRP_RULE,$(src),port,$(BSD_CFLAGS))))
$(foreach src,$(KPI_SRCS),$(eval $(call GRP_RULE,$(src),kpi,$(KPI_CFLAGS))))
$(foreach src,$(APP_SRCS),$(eval $(call GRP_RULE,$(src),app,$(KPI_CFLAGS))))
$(foreach src,$(NETIF_SRCS),$(eval $(call GRP_RULE,$(src),netif,$(NETIF_CFLAGS))))
$(foreach src,$(DRV_SRCS),$(eval $(call GRP_RULE,$(src),drv,$(DRV_CFLAGS))))
$(foreach src,$(RT_SRCS),$(eval $(call GRP_RULE,$(src),rt,$(RT_CFLAGS))))

# the pool body, with the host's platform hooks
$(POOL_OBJ): $(POOL_SRC) | $(BUILD_DIR)/kpi
	$(CC) $(KPI_CFLAGS) -DLKPI_SKB_POOL_NO_TARGET_HOOKS -c $< -o $@

$(BIN): $(STACK_OBJS) $(SEAM_OBJS) $(PORT_OBJS) $(KPI_OBJS) $(APP_OBJS) \
	$(NETIF_OBJS) $(DRV_OBJS) $(RT_OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

run: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD_DIR)
