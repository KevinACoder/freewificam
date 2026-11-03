# @file    wlan.mk
# @brief   The 8189FTV vendor-stack port's source set and flag worlds.
#
# The vendor driver is vendored at its pin under
# wireless-drivers/rtl8189fs/src (GPL-2.0, embedded firmware array). The build
# uses that local copy; VENDOR_8189FS is only an override for a different tree.
#
# Three worlds meet here and are kept apart by their include sets:
#   - the vendor driver (its own config/warning set, linuxkpi headers only);
#   - the linuxkpi runtime face (CMSIS-RTOS2 + the Linux headers);
#   - the platform's SDIO port (fsl_sdmmc + dwc_mmc) and the wlan case.
# The driver never sees the platform's case/backend defines, and the platform's
# units never see the vendor tree's `include/linux` copies.

ifneq ($(filter wlan,$(CASES)),)

# The vendor tree is vendored at the pin (wireless-drivers/rtl8189fs/src).
# VENDOR_8189FS remains an override for an external checkout, but the default
# build needs no path outside the repository.
ifeq ($(strip $(VENDOR_8189FS)),)
VENDOR_8189FS := $(ROOT)/wireless-drivers/rtl8189fs/src
endif

# Scan diagnostic arms: WLAN_SCAN_DIAG=1 runs the four request shapes before the
# production full scan.  WLAN_SCAN_BATCH=1 is the production shape: the band is
# covered by four <=4-channel requests, which the vendor duplicates 3x (300 ms
# per channel) so the 100 ms beacon is not missed; the one-shot 14-channel
# request is the diagnostic/fallback shape.  Both are build-time so one boot is
# one arm.
WLAN_SCAN_DIAG := $(CONFIG_WLAN_SCAN_DIAG)
WLAN_SCAN_BATCH := $(CONFIG_WLAN_SCAN_BATCH)
WLAN_TCP_SINK := $(CONFIG_WLAN_TCP_SINK)
WLAN_TCP_SINK_SECONDS := $(CONFIG_WLAN_TCP_SINK_SECONDS)
# Observation build. WLAN_OBS=1 turns on the linuxkpi census (the SDIO offer /
# read / pump / claim counters), the 1 Hz layer snapshots, the per-sequence ping
# trace and the UDP sink. It is a measurement image: the default build stays
# byte-for-byte the delivery image.
WLAN_OBS := $(CONFIG_WLAN_OBS)
WLAN_PING_TRACE := $(CONFIG_WLAN_PING_TRACE)
WLAN_PING_COUNT := $(CONFIG_WLAN_PING_COUNT)
WLAN_PING_INTERVAL_MS := $(CONFIG_WLAN_PING_INTERVAL_MS)
WLAN_UDP_SINK := $(CONFIG_WLAN_UDP_SINK)
WLAN_UDP_SINK_SECONDS := $(CONFIG_WLAN_UDP_SINK_SECONDS)
WLAN_UDP_PORT := $(CONFIG_WLAN_UDP_PORT)
WLAN_PUMP_PRIO := $(CONFIG_WLAN_PUMP_PRIO)
WLAN_PUMP_DRAIN := $(CONFIG_WLAN_PUMP_DRAIN)
ifeq ($(WLAN_PUMP_PRIO),normal)
WLAN_PUMP_PRIO_LEVEL := 1
else ifeq ($(WLAN_PUMP_PRIO),above)
WLAN_PUMP_PRIO_LEVEL := 2
else
WLAN_PUMP_PRIO_LEVEL := 0
endif

LKPI := $(COMPONENTS)/linuxkpi/rtos

WLAN_SKB_CFLAGS :=

# --- source sets -------------------------------------------------------------

WLAN_APP_SRCS := $(RTL8189FS_APP_SRCS)

WLAN_SDIO_SRCS := $(RTL8189FS_SDIO_SRCS)

WLAN_LKPI_SRCS := $(LINUXKPI_WLAN_SRCS)

# The 131 RTL8188F translation units: the ground-truth module link table
# (37 core + 16 os_dep + 29 hal + 48 hal/phydm + 1 platform).
WLAN_DRV_SRCS := $(RTL8189FS_IMPL_SRCS)

WLAN_LWIP_SRCS := $(RTL8189FS_LWIP_SRCS)

WLAN_OBS_SRCS := $(RTL8189FS_OBS_SRCS)
WLAN_ALL_SRCS := $(WLAN_APP_SRCS) $(WLAN_SDIO_SRCS) $(WLAN_LKPI_SRCS) \
	$(WLAN_DRV_SRCS) $(WLAN_LWIP_SRCS) $(WLAN_OBS_SRCS)
WLAN_OBJS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(WLAN_ALL_SRCS))))

# --- flag worlds -------------------------------------------------------------

# The observation world, shared by every WLAN translation unit that carries a
# counter face. The engine's own RX-presentation probe is address-filtered: the
# configured addresses are the vendor's SDIO local registers (RX0_REQ_LEN=0x1C,
# HISR=0x18), so no vendor byte is edited and the engine stays register-agnostic
# in every other image.
ifeq ($(WLAN_OBS),1)
WLAN_OBS_CFLAGS :=
else
WLAN_OBS_CFLAGS :=
endif

# The controller this image's host glue drives, named explicitly. Every port the
# imported stack supports exports the same three symbols, so a silent default
# would link another board's controller code and then drive the wrong hardware
# (see sdmmc_port.h); the header refuses to compile without a name. This lane is
# the rk3568 board's DesignWare MMC block.
#
# It goes to EVERY unit that reaches the port boundary, the case included: the
# port header gates its declarations on the selection, so a unit compiled
# without it gets implicit int declarations and truncates a returned pointer.
WLAN_SDIO_PORT_DEFS :=

# The SDIO port and the case live in the platform's world; they need the
# imported stack's SDIO directory and the linuxkpi seam headers.
WLAN_SDIO_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) $(WLAN_OBS_CFLAGS) -std=gnu11 \
	$(WLAN_SDIO_PORT_DEFS) \
	-I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio -I$(LKPI)/skbuff \
	-I$(THIRD_PARTY)/fsl_sdmmc/ports -I$(THIRD_PARTY)/fsl_sdmmc/sdio

# The linuxkpi face compiles against the CMSIS-RTOS2 facade with the Linux
# headers it carries; the board's device header is replaced by cmsis_os2.h,
# exactly as the reference image compiles it.
WLAN_LKPI_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) $(WLAN_OBS_CFLAGS) \
	-std=gnu11 -I$(ROOT) -I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio \
	-I$(LKPI)/skbuff

WLAN_APP_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) $(WLAN_OBS_CFLAGS) \
	-std=gnu11 -I$(ROOT)/wireless-drivers/rtl8189fs/ports/rtos \
	-I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio -I$(LKPI)/skbuff \
	-I$(THIRD_PARTY)/fsl_sdmmc/ports -I$(THIRD_PARTY)/fsl_sdmmc/sdio

# The lwIP half keeps the platform's CMSIS/board flags (the same set the net
# case compiles with): lkPInetif's stack side, sys_arch and the stack must not
# see the linuxkpi device-header substitution.
WLAN_LWIP_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) $(WLAN_OBS_CFLAGS) -std=gnu11

# The driver's build configuration is the reference image's resolved set
# (transcribed from the driver's own build), with the log level lowered for
# the 115200 console.  The first bring-up keeps the reference's feature set;
# PS/concurrent/MP are removed only after the attach baseline exists.
WLAN_DRV_CONFIG :=

WLAN_DRV_WARN := -Wall -Wno-address-of-packed-member -Wno-maybe-uninitialized \
	-Wno-missing-field-initializers -Wno-shift-negative-value -Wno-sign-compare \
	-Wno-unused-parameter -Wno-type-limits -Wno-pointer-sign -Wno-unused \
	-Wno-unused-function -Wno-unused-variable -Wno-array-bounds

WLAN_DRV_CFLAGS := -std=c11 -O2 -g3 -ffreestanding -fno-builtin \
	-fno-stack-protector -fno-strict-aliasing -fms-extensions \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections $(WLAN_DRV_WARN) \
	-I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio \
	-I$(THIRD_PARTY)/ulog/src \
	-I$(ROOT)/wireless-drivers/rtl8189fs/ports/rtos \
	-I$(VENDOR_8189FS) -I$(VENDOR_8189FS)/include -I$(VENDOR_8189FS)/platform \
	-I$(VENDOR_8189FS)/hal/phydm -I$(VENDOR_8189FS)/hal/btc \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-include stdio.h -include string.h -include strings.h -include stdlib.h

WLAN_FLAGS_STAMP := $(WLAN_APP_CFLAGS) | $(WLAN_SDIO_CFLAGS) | \
	$(WLAN_LKPI_CFLAGS) | $(WLAN_DRV_CFLAGS) | $(VENDOR_8189FS) | $(WLAN_OBS_SRCS)
WLAN_STAMP := $(BUILD)/wlan-flags-$(BACKEND).stamp
WLAN_LDFLAGS := -Wl,-u,lkpi_module_init_run \
	-Wl,-u,lkpi_sdio_fsl_install -Wl,-u,lkpi_sdio_be_card_init

endif # wlan
