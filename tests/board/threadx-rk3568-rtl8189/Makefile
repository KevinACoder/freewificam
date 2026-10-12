# @file    Makefile
# @brief   Build the ThreadX board RTL8189FTV image for RK3568: the SDIO bus line
#          (imported protocol layer, card service, host glue, controller) plus the
#          vendored Realtek SDIO vendor driver and the first-party runtime face it
#          links against, and the suite's own cases and console command.
# @author  zhugengyu
# @date    12.10.2026
# @design  self-contained project makefile - the SDIO image's structure
#          (tests/board/threadx-rk3568-sdio/Makefile) with one driver added: the
#          same base/TX/common/shell domains and the same SDIO domain, plus the
#          driver's own compile set and the runtime face it needs.  The image
#          exists on its own because it carries exactly one driver: an image with
#          two would make the module-section check meaningless and would put one
#          driver's regression on the other's board rounds.
#
#          Unlike the SoftMAC images this one has no net80211, no supplicant and
#          no TCP/IP domain: the vendored driver is a FullMAC stack that carries
#          its own MLME and talks to cfg80211 directly, so the linuxkpi runtime
#          face stands in for cfg80211 and the presentation layer, and the
#          net80211 bridge is not on this line at all.
#
# Product: build/threadx-rk3568-rtl8189.bin - the flat raw binary staged as
# rtos.bin (the lab profile's deploy name) and loaded with
# `tftp 0xa000000 rtos.bin`, entered with `go 0xa000000`.
#
# Test execution order = link order = the TESTS_SRCS list below.

ROOT := $(abspath $(CURDIR)/../../..)
TX := $(ROOT)/third-party/threadx
TX_PORT := $(TX)/ports/cortex_a55/gnu
BOARD_CMSIS2 := $(ROOT)/os/threadx/board/rk3568-itx/cmsis2
CMSIS := $(ROOT)/third-party/CMSIS_6
CHERRYSH := $(ROOT)/components/cherrysh
PORT := $(ROOT)/ports/cherrysh
COMMON := $(ROOT)/tests/board/common

# The SDIO domain: the imported protocol layer (byte-faithful), this project's
# OS abstraction, card service, host glue and seam implementation, the seam's
# own dispatchers, and the board controller driver.
FSL := $(ROOT)/components/fsl_sdmmc
SDIOPORT := $(ROOT)/ports/fsl_sdmmc
LKPI := $(ROOT)/components/linuxkpi
BOARD_DRV := $(ROOT)/board/rk3568-itx/drivers

# The driver domain: the vendored Realtek SDIO vendor driver (a pinned
# submodule, compiled here as its own translation units, byte-identical).
DRV := $(ROOT)/third-party/rtl8189fs

SUITE_NAME := threadx-rk3568-rtl8189

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
NM := $(CROSS_COMPILE)nm
READELF := $(CROSS_COMPILE)readelf
SIZE := $(CROSS_COMPILE)size
HOSTCC ?= cc

BUILD_TAG ?= $(shell git -C $(ROOT) describe --always --dirty 2>/dev/null || echo unknown)

BUILD := build
TARGET := $(BUILD)/$(SUITE_NAME)
TAG_STAMP := $(BUILD)/build-tag.stamp

INCLUDES := \
	-I$(ROOT) \
	-I$(ROOT)/os/cmsis2 \
	-I$(ROOT)/arch/aarch64 \
	-I$(ROOT)/board/rk3568-itx \
	-I$(BOARD_DRV) \
	-I$(ROOT)/os/baremetal \
	-I$(ROOT)/components/ulog/src \
	-DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256 \
	-I$(CHERRYSH) \
	-I$(PORT) \
	-I$(ROOT)/components/tlsf \
	-I$(COMMON) \
	-I$(CMSIS)/CMSIS/RTOS2/Include

TX_CFLAGS := -I$(BOARD_CMSIS2) \
	-I$(ROOT)/os/threadx \
	-I$(TX)/common/inc \
	-I$(TX_PORT)/inc \
	-DTX_INCLUDE_USER_DEFINE_FILE -DEL1=1 \
	-DCONFIG_TX_TICKS_PER_SECOND=1000 -DCONFIG_TX_TIMER_STACK=4096 \
	-DCMSIS2_MAX_TIMERS=128 -DCMSIS2_MAX_THREADS=24

OPT ?= -Og
BASE_CFLAGS := $(OPT) -g3 -std=c11 -Wall -Wextra \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections \
	-DFW_BUILD_TAG='"$(BUILD_TAG)"' \
	-DBOARD_SUITE_NAME='"$(SUITE_NAME)"' \
	-DCMSIS2_BACKEND_THREADX \
	-DSHELL_PORT_HOST='"rk3568"'

CFLAGS := $(BASE_CFLAGS) -include $(ROOT)/os/baremetal/minilibc.h
ASM_CFLAGS := $(BASE_CFLAGS)

# The imported stacks' sources compile at the -Wall level the upstream
# projects themselves use (this project does not own their bytes).
VENDORED_CFLAGS := $(filter-out -Wextra,$(CFLAGS))

# --- SDIO domain flag sets ---------------------------------------------------
#
# The imported protocol layer's include roots, plus the seam's. The port files
# are this project's and stay warning-honest; the imported units keep their own
# level.
SDIO_INCLUDES := \
	-I$(FSL)/common -I$(FSL)/sdio -I$(FSL)/osa \
	-I$(SDIOPORT) -I$(SDIOPORT)/shadow \
	-I$(LKPI)/sdio

SDIO_VENDOR_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) \
	-DCONFIG_FSL_SDMMC_ENABLE_SDIO=1

SDIO_PORT_CFLAGS := $(CFLAGS) \
	-DCONFIG_FSL_SDMMC_ENABLE_SDIO=1 \
	-DLKPI_ABS_PLATFORM_PROVIDED \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(LKPI) -I$(LKPI)/include

# The suite's own cases and console command reach the driver through the
# runtime face and the card through the SDIO seam, so they carry both include
# roots.
WLAN8189_CASE_SRCS := \
	$(CURDIR)/tests/wlan8189_probe.c \
	$(CURDIR)/tests/wlan8189_scan.c \
	$(CURDIR)/tests/wlan8189_cmds.c

WLAN8189_CASE_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-DLKPI_ABS_PLATFORM_PROVIDED \
	-DCONFIG_FSL_SDMMC_ENABLE_SDIO=1 \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio -I$(CURDIR)/tests \
	$(SDIO_INCLUDES)

# The skb pool's geometry, which is a compile-time contract between the
# driver's view of the pool (the header) and the pool body: both sides must see
# the same numbers or the slot sizes disagree. The driver's receive path keeps
# buffers alive for the interface's lifetime, so the carrier's default (48 header
# slots, 32 heap-backed ones, 2048-byte data slots) is exhausted before its first
# transfer completes; the data slots stay 2048 bytes and the larger buffers are
# served from the heap, which is where the window above went.
SKB_POOL_CFLAGS := -DCONFIG_LKPI_SKB_POOL_NUM=128 \
	-DCONFIG_LKPI_SKB_DATA_POOL_NUM=64 \
	-DCONFIG_LKPI_SKB_HEAP_HDR_MAX=64

# --- driver domain flag sets -------------------------------------------------
#
# The driver's own build configuration, the same resolved set the host build
# gate compiles it with (transcribed from the driver's own build), minus the
# procfs switch: this port has no procfs, so the driver's /proc interface is
# off and its seq_file surface compiles away.  The include roots are the
# driver's own header directories.
DRV_CONFIG := -D__KERNEL__ -DMODULE -D__KBUILD_MODNAME=8189fs \
	-DKBUILD_MODNAME='"8189fs"' \
	-DCONFIG_LITTLE_ENDIAN -DCONFIG_IOCTL_CFG80211 -DRTW_USE_CFG80211_STA_EVENT \
	-DCONFIG_CONCURRENT_MODE -DCONFIG_RTL8188F -DCONFIG_MP_INCLUDED \
	-DCONFIG_BR_EXT -DCONFIG_BR_EXT_BRNAME='"br0"' -DCONFIG_POWER_SAVING \
	-DCONFIG_RTW_DEBUG -DRTW_LOG_LEVEL=4 -DCONFIG_EFUSE_CONFIG_FILE \
	-DEFUSE_MAP_PATH='"/system/etc/wifi/wifi_efuse_8189fs.map"' \
	-DCONFIG_LOAD_PHY_PARA_FROM_FILE -DREALTEK_CONFIG_PATH='"/lib/firmware/"' \
	-DCONFIG_TXPWR_BY_RATE=1 -DCONFIG_TXPWR_BY_RATE_EN=1 \
	-DCONFIG_TXPWR_LIMIT=1 -DCONFIG_TXPWR_LIMIT_EN=0 \
	-DCONFIG_RTW_ADAPTIVITY_EN=0 -DCONFIG_RTW_ADAPTIVITY_MODE=0 \
	-DCONFIG_RTW_NETIF_SG -DCONFIG_RTW_SDIO_PM_KEEP_POWER \
	-DDM_ODM_SUPPORT_TYPE=0x04 -DHIGH_ACTIVE_HST2DEV=0 \
	-DWIFIMAC_PATH='"/data/wifimac.txt"'

# The driver's bytes are compiled at the warning level its own build takes
# (the kernel's default set for a driver directory), not at this project's.
DRV_WARN := -Wall -Wno-address-of-packed-member -Wno-maybe-uninitialized \
	-Wno-missing-field-initializers -Wno-shift-negative-value -Wno-sign-compare \
	-Wno-unused-parameter -Wno-type-limits -Wno-pointer-sign -Wno-unused \
	-Wno-unused-function -Wno-unused-variable -Wno-array-bounds

# LKPI_ABS_PLATFORM_PROVIDED: the board's freestanding C surface already declares
# abs, so the face's own definition retires (a static definition after the
# platform's declaration is not legal C).
DRV_CFLAGS := -std=c11 -O2 -ffreestanding -fno-builtin -fno-stack-protector \
	-march=armv8-a -mgeneral-regs-only -mstrict-align -mno-outline-atomics \
	-ffunction-sections -fdata-sections $(DRV_WARN) $(DRV_CONFIG) \
	-DLKPI_ABS_PLATFORM_PROVIDED $(SKB_POOL_CFLAGS) \
	-I$(LKPI) -I$(LKPI)/include -I$(DRV) -I$(DRV)/include -I$(DRV)/platform \
	-I$(DRV)/hal/phydm -I$(DRV)/hal/btc \
	-I$(CMSIS)/CMSIS/RTOS2/Include \
	-include $(ROOT)/os/baremetal/minilibc.h

# --- linuxkpi runtime face ---------------------------------------------------
#
# The bodies the driver links against: the linuxkpi service core, the SDIO
# engine, the cfg80211 substitute (this line's replacement for cfg80211, since
# the driver is a FullMAC vendor stack that talks to cfg80211 directly), the
# netdev bodies and the skb pool.  The pool geometry is passed here too: it is a
# compile-time contract between this body and the driver's view of it.
LKPI_RT_CFLAGS := $(filter-out -Wextra,$(CFLAGS)) -std=gnu11 \
	-DLKPI_ABS_PLATFORM_PROVIDED $(SKB_POOL_CFLAGS) \
	-DCMSIS_device_header='"cmsis_os2.h"' \
	-I$(ROOT) -I$(LKPI) -I$(LKPI)/include -I$(LKPI)/sdio -I$(LKPI)/skbuff

LKPI_RT_SRCS := \
	$(LKPI)/linuxkpi_core.c \
	$(LKPI)/linuxkpi_platform_target.c \
	$(LKPI)/sdio/linuxkpi_sdio.c \
	$(LKPI)/skbuff/skbuff_pool.c \
	$(LKPI)/linuxkpi_cfg80211.c \
	$(LKPI)/linuxkpi_netdev.c \
	$(LKPI)/linuxkpi_osdep.c

# --gc-sections drops everything nothing references. The pins below are the
# entry points the start-up order calls that the linker cannot see
# transitively (the module walker, the board backend registration, the module
# init entries the driver registers itself through).
HEAP_SIZE ?= 16M

LDFLAGS := -nostdlib -static \
	-Wl,-T,$(ROOT)/arch/aarch64/baremetal.ld \
	-Wl,--defsym=HEAP_SIZE=$(HEAP_SIZE) \
	-Wl,--gc-sections \
	-Wl,-u,osCriticalEnter \
	-Wl,-u,osCriticalExit \
	-Wl,-u,lkpi_sdio_fsl_install \
	-Wl,-u,lkpi_sdio_be_card_init \
	-Wl,-u,lkpi_module_init_run \
	-Wl,--build-id=none \
	-Wl,-Map,$(TARGET).map

# --- compile set -------------------------------------------------------------

BASE_SRCS := \
	$(ROOT)/arch/aarch64/start.S \
	$(ROOT)/arch/aarch64/gicv3.c \
	$(ROOT)/arch/aarch64/generic_timer.c \
	$(ROOT)/arch/aarch64/mmu.c \
	$(ROOT)/arch/aarch64/cache.c \
	$(ROOT)/board/rk3568-itx/drivers/uart.c \
	$(ROOT)/board/rk3568-itx/param.c \
	$(ROOT)/board/rk3568-itx/reset.c \
	$(ROOT)/os/baremetal/memops.c \
	$(ROOT)/os/baremetal/minilibc.c \
	$(ROOT)/os/baremetal/log.c \
	$(ROOT)/components/ulog/src/ulog.c \
	$(ROOT)/os/baremetal/tick.c

TX_SRCS := \
	$(wildcard $(TX)/common/src/*.c) \
	$(wildcard $(TX_PORT)/src/*.S) \
	$(ROOT)/os/threadx/board/rk3568-itx/tx_glue.c \
	$(ROOT)/os/threadx/board/rk3568-itx/tx_vectors.S \
	$(ROOT)/os/threadx/board/rk3568-itx/heap.c \
	$(ROOT)/os/threadx/board/rk3568-itx/cmsis2/cmsis2_board.c \
	$(ROOT)/os/threadx/cmsis2_threadx.c \
	$(ROOT)/os/threadx/cmsis2_ext.c

COMMON_SRCS := \
	$(COMMON)/board_suite.c \
	$(COMMON)/board_select.c

SHELL_SRCS := \
	$(CURDIR)/main.c \
	$(PORT)/shell_port.c \
	$(PORT)/console_board.c \
	$(PORT)/board_cmds.c

# Execution order = this list's order (link order for the BOARD_TESTS section).
TESTS_SRCS := \
	$(COMMON)/cases/shell_up.c \
	$(WLAN8189_CASE_SRCS)

# The imported protocol layer and the board controller driver: the two units the
# SDIO build set compiles, and the controller.
SDIO_VENDOR_SRCS := \
	$(FSL)/common/fsl_sdmmc_common.c \
	$(FSL)/sdio/fsl_sdio.c \
	$(BOARD_DRV)/dwc_mmc.c

# This project's side of the line.
SDIO_PORT_SRCS := \
	$(SDIOPORT)/fsl_sdmmc_osa.c \
	$(SDIOPORT)/sdmmc_card.c \
	$(SDIOPORT)/sdmmc_host_dwmmc.c \
	$(SDIOPORT)/lkpi_sdio_fsl.c \
	$(LKPI)/sdio/lkpi_sdio_be.c

# The 131 RTL8188F translation units: the ground-truth module link table of the
# driver's own build (37 core, 16 os_dep, 29 hal, 48 hal/phydm, 1 platform).
DRV_SRCS := \
	$(DRV)/core/rtw_cmd.c $(DRV)/core/rtw_security.c $(DRV)/core/rtw_debug.c \
	$(DRV)/core/rtw_io.c $(DRV)/core/rtw_ioctl_query.c $(DRV)/core/rtw_ioctl_set.c \
	$(DRV)/core/rtw_ieee80211.c $(DRV)/core/rtw_mlme.c $(DRV)/core/rtw_mlme_ext.c \
	$(DRV)/core/rtw_mi.c $(DRV)/core/rtw_wlan_util.c $(DRV)/core/rtw_vht.c \
	$(DRV)/core/rtw_pwrctrl.c $(DRV)/core/rtw_rf.c $(DRV)/core/rtw_chplan.c \
	$(DRV)/core/rtw_recv.c $(DRV)/core/rtw_sta_mgt.c $(DRV)/core/rtw_ap.c \
	$(DRV)/core/mesh/rtw_mesh.c $(DRV)/core/mesh/rtw_mesh_pathtbl.c \
	$(DRV)/core/mesh/rtw_mesh_hwmp.c $(DRV)/core/rtw_xmit.c $(DRV)/core/rtw_p2p.c \
	$(DRV)/core/rtw_rson.c $(DRV)/core/rtw_tdls.c $(DRV)/core/rtw_br_ext.c \
	$(DRV)/core/rtw_iol.c $(DRV)/core/rtw_sreset.c $(DRV)/core/rtw_btcoex_wifionly.c \
	$(DRV)/core/rtw_btcoex.c $(DRV)/core/rtw_beamforming.c $(DRV)/core/rtw_odm.c \
	$(DRV)/core/rtw_rm.c $(DRV)/core/rtw_rm_fsm.c $(DRV)/core/efuse/rtw_efuse.c \
	$(DRV)/core/rtw_sdio.c $(DRV)/core/rtw_mp.c \
	$(DRV)/os_dep/osdep_service.c $(DRV)/os_dep/linux/os_intfs.c \
	$(DRV)/os_dep/linux/sdio_intf.c $(DRV)/os_dep/linux/sdio_ops_linux.c \
	$(DRV)/os_dep/linux/ioctl_linux.c $(DRV)/os_dep/linux/xmit_linux.c \
	$(DRV)/os_dep/linux/mlme_linux.c $(DRV)/os_dep/linux/recv_linux.c \
	$(DRV)/os_dep/linux/ioctl_cfg80211.c $(DRV)/os_dep/linux/rtw_cfgvendor.c \
	$(DRV)/os_dep/linux/wifi_regd.c $(DRV)/os_dep/linux/rtw_android.c \
	$(DRV)/os_dep/linux/rtw_proc.c $(DRV)/os_dep/linux/rtw_rhashtable.c \
	$(DRV)/os_dep/linux/ioctl_mp.c $(DRV)/os_dep/linux/custom_gpio_linux.c \
	$(DRV)/hal/hal_intf.c $(DRV)/hal/hal_com.c $(DRV)/hal/hal_com_phycfg.c \
	$(DRV)/hal/hal_phy.c $(DRV)/hal/hal_dm.c $(DRV)/hal/hal_dm_acs.c \
	$(DRV)/hal/hal_btcoex_wifionly.c $(DRV)/hal/hal_btcoex.c $(DRV)/hal/hal_mp.c \
	$(DRV)/hal/hal_mcc.c $(DRV)/hal/hal_hci/hal_sdio.c $(DRV)/hal/led/hal_led.c \
	$(DRV)/hal/led/hal_sdio_led.c $(DRV)/hal/HalPwrSeqCmd.c \
	$(DRV)/hal/rtl8188f/Hal8188FPwrSeq.c $(DRV)/hal/rtl8188f/rtl8188f_sreset.c \
	$(DRV)/hal/rtl8188f/rtl8188f_hal_init.c $(DRV)/hal/rtl8188f/rtl8188f_phycfg.c \
	$(DRV)/hal/rtl8188f/rtl8188f_rf6052.c $(DRV)/hal/rtl8188f/rtl8188f_dm.c \
	$(DRV)/hal/rtl8188f/rtl8188f_rxdesc.c $(DRV)/hal/rtl8188f/rtl8188f_cmd.c \
	$(DRV)/hal/rtl8188f/hal8188f_fw.c $(DRV)/hal/rtl8188f/sdio/sdio_halinit.c \
	$(DRV)/hal/rtl8188f/sdio/rtl8189fs_led.c $(DRV)/hal/rtl8188f/sdio/rtl8189fs_xmit.c \
	$(DRV)/hal/rtl8188f/sdio/rtl8189fs_recv.c $(DRV)/hal/rtl8188f/sdio/sdio_ops.c \
	$(DRV)/hal/efuse/rtl8188f/HalEfuseMask8188F_SDIO.c \
	$(DRV)/hal/phydm/phydm_debug.c $(DRV)/hal/phydm/phydm_antdiv.c \
	$(DRV)/hal/phydm/phydm_soml.c $(DRV)/hal/phydm/phydm_smt_ant.c \
	$(DRV)/hal/phydm/phydm_antdect.c $(DRV)/hal/phydm/phydm_interface.c \
	$(DRV)/hal/phydm/phydm_phystatus.c $(DRV)/hal/phydm/phydm_hwconfig.c \
	$(DRV)/hal/phydm/phydm.c $(DRV)/hal/phydm/phydm_dig.c \
	$(DRV)/hal/phydm/phydm_pathdiv.c $(DRV)/hal/phydm/phydm_rainfo.c \
	$(DRV)/hal/phydm/phydm_dynamictxpower.c $(DRV)/hal/phydm/phydm_adaptivity.c \
	$(DRV)/hal/phydm/phydm_cfotracking.c $(DRV)/hal/phydm/phydm_noisemonitor.c \
	$(DRV)/hal/phydm/phydm_beamforming.c $(DRV)/hal/phydm/phydm_direct_bf.c \
	$(DRV)/hal/phydm/phydm_dfs.c $(DRV)/hal/phydm/txbf/halcomtxbf.c \
	$(DRV)/hal/phydm/txbf/haltxbfinterface.c $(DRV)/hal/phydm/txbf/phydm_hal_txbf_api.c \
	$(DRV)/hal/phydm/phydm_adc_sampling.c $(DRV)/hal/phydm/phydm_ccx.c \
	$(DRV)/hal/phydm/phydm_psd.c $(DRV)/hal/phydm/phydm_primary_cca.c \
	$(DRV)/hal/phydm/phydm_cck_pd.c $(DRV)/hal/phydm/phydm_rssi_monitor.c \
	$(DRV)/hal/phydm/phydm_auto_dbg.c $(DRV)/hal/phydm/phydm_math_lib.c \
	$(DRV)/hal/phydm/phydm_api.c $(DRV)/hal/phydm/phydm_pow_train.c \
	$(DRV)/hal/phydm/phydm_lna_sat.c $(DRV)/hal/phydm/phydm_pmac_tx_setting.c \
	$(DRV)/hal/phydm/phydm_mp.c $(DRV)/hal/phydm/halrf/halrf.c \
	$(DRV)/hal/phydm/halrf/halrf_debug.c $(DRV)/hal/phydm/halrf/halphyrf_ce.c \
	$(DRV)/hal/phydm/halrf/halrf_powertracking_ce.c \
	$(DRV)/hal/phydm/halrf/halrf_powertracking.c $(DRV)/hal/phydm/halrf/halrf_kfree.c \
	$(DRV)/hal/phydm/halrf/halrf_psd.c $(DRV)/hal/phydm/rtl8188f/halhwimg8188f_bb.c \
	$(DRV)/hal/phydm/rtl8188f/halhwimg8188f_mac.c \
	$(DRV)/hal/phydm/rtl8188f/halhwimg8188f_rf.c \
	$(DRV)/hal/phydm/rtl8188f/phydm_regconfig8188f.c \
	$(DRV)/hal/phydm/halrf/rtl8188f/halrf_8188f.c \
	$(DRV)/hal/phydm/rtl8188f/phydm_rtl8188f.c \
	$(DRV)/platform/platform_ops.c

VENDORED_SRCS := \
	$(CHERRYSH)/chry_shell.c \
	$(CHERRYSH)/cherryrl/chry_readline.c \
	$(CHERRYSH)/builtin/clear.c \
	$(CHERRYSH)/builtin/help.c \
	$(CHERRYSH)/builtin/shsize.c

ALLOC_SRCS := $(ROOT)/components/tlsf/tlsf.c

PRINTF_SRCS := $(ROOT)/components/printf/printf.c
PRINTF_CFLAGS := -DPRINTF_DISABLE_SUPPORT_FLOAT -DPRINTF_DISABLE_SUPPORT_EXPONENTIAL

# the board's C-library names the port files reach (memset family over the
# freestanding subset, printf through the stamped sink)
LIBCBIND_SRCS := $(ROOT)/os/threadx/board/rk3568-itx/libc_bindings.c

ALL_SRCS := $(BASE_SRCS) $(TX_SRCS) $(COMMON_SRCS) $(SHELL_SRCS) $(TESTS_SRCS) \
	$(SDIO_VENDOR_SRCS) $(SDIO_PORT_SRCS) $(LKPI_RT_SRCS) $(DRV_SRCS) \
	$(VENDORED_SRCS) $(ALLOC_SRCS) $(PRINTF_SRCS) $(LIBCBIND_SRCS)

OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(patsubst $(ROOT)/%,%,$(ALL_SRCS))))

.PHONY: all check guards clean FORCE
all: $(TARGET).bin check

define COMPILE_RULE
$(BUILD)/obj/$(patsubst $(ROOT)/%,%,$(1)).o: $(1) $(TAG_STAMP)
	@mkdir -p $$(dir $$@)
	@echo "  CC $$<"
	@$(CC) $(if $(filter %.S,$(1)),$(ASM_CFLAGS),$(if $(filter $(1),$(VENDORED_SRCS) $(SDIO_VENDOR_SRCS)),$(if $(filter $(1),$(SDIO_VENDOR_SRCS)),$(SDIO_VENDOR_CFLAGS),$(VENDORED_CFLAGS)),$(if $(filter $(1),$(SDIO_PORT_SRCS)),$(SDIO_PORT_CFLAGS),$(if $(filter $(1),$(LKPI_RT_SRCS)),$(LKPI_RT_CFLAGS),$(if $(filter $(1),$(DRV_SRCS)),$(DRV_CFLAGS),$(if $(filter $(1),$(WLAN8189_CASE_SRCS)),$(WLAN8189_CASE_CFLAGS),$(CFLAGS))))))) $(2) $(INCLUDES) $(if $(filter $(1),$(SDIO_VENDOR_SRCS) $(SDIO_PORT_SRCS)),$(SDIO_INCLUDES),) $(if $(filter %.S,$(1)),,-MMD -MP) -c $$< -o $$@
endef

$(foreach s,$(PRINTF_SRCS),$(eval $(call COMPILE_RULE,$(s),$(PRINTF_CFLAGS))))
# minilibc.c carries its own rule below (the completion block); leaving it out
# of this loop keeps make from warning about a second recipe for its object.
$(foreach s,$(filter-out $(ROOT)/os/baremetal/minilibc.c,$(BASE_SRCS)),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(TX_SRCS),$(eval $(call COMPILE_RULE,$(s),$(TX_CFLAGS))))
$(foreach s,$(COMMON_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(filter-out $(WLAN8189_CASE_SRCS),$(SHELL_SRCS) $(TESTS_SRCS)),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(WLAN8189_CASE_SRCS),$(eval $(call COMPILE_RULE,$(s),$(WLAN8189_CASE_CFLAGS))))
$(foreach s,$(VENDORED_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(ALLOC_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
# the freestanding libc completion block (abs and friends) is part of this
# image's C surface: the imported units and the driver reference the symbols
$(eval $(call COMPILE_RULE,$(ROOT)/os/baremetal/minilibc.c,-DCONFIG_LIBC_COMPLETION))
$(foreach s,$(SDIO_VENDOR_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(SDIO_PORT_SRCS),$(eval $(call COMPILE_RULE,$(s),)))
$(foreach s,$(LKPI_RT_SRCS),$(eval $(call COMPILE_RULE,$(s),$(LKPI_RT_CFLAGS))))
$(foreach s,$(DRV_SRCS),$(eval $(call COMPILE_RULE,$(s),$(DRV_CFLAGS))))
$(foreach s,$(LIBCBIND_SRCS),$(eval $(call COMPILE_RULE,$(s),-I$(ROOT)/os/threadx)))

$(TAG_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s' '$(BUILD_TAG)' | cmp -s - $@ 2>/dev/null || printf '%s' '$(BUILD_TAG)' > $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# --- build-side gates ------------------------------------------------------

guards:
	@echo "== build guards =="
	@echo "$(TX_CFLAGS)" | grep -q -- '-DEL1=1' \
		|| { echo "FAIL: -DEL1=1 missing from TX_CFLAGS"; exit 1; }
	@echo "$(TX_CFLAGS)" | grep -q -- '-DTX_INCLUDE_USER_DEFINE_FILE' \
		|| { echo "FAIL: -DTX_INCLUDE_USER_DEFINE_FILE missing from TX_CFLAGS"; exit 1; }
	@[ "$$(basename $(CURDIR))" = "$(SUITE_NAME)" ] \
		|| { echo "FAIL: SUITE_NAME ($(SUITE_NAME)) is not the directory name"; exit 1; }
	@if echo "$(ALL_SRCS)" | tr ' ' '\n' | grep -qE "^$(ROOT)/os/threadx/(cmsis2_host|heap|tx_glue)\.c$$"; then \
		echo "FAIL: a host-side os/threadx file entered the compile set"; exit 1; \
	fi
	@echo "PASS: port keys present, CMSIS2 flavour in effect, suite name matches, host files out"

check: guards $(TARGET).elf
	@echo "== symbol contract =="
	@set -e; \
	start=$$($(NM) $(TARGET).elf | awk '$$3=="_start"{print $$1}'); \
	[ -n "$$start" ] || { echo "FAIL: _start not found"; exit 1; }; \
	[ $$(( 0x$$start )) -eq $$(( 0xa000000 )) ] \
		|| { echo "FAIL: _start=0x$$start (want 0xa000000)"; exit 1; }; \
	for s in _tx_initialize_kernel_enter osKernelStart osThreadNew osDelay \
	         cmsis2_tx_application_define pvPortMalloc tlsf_malloc \
	         shell_port_init chry_shell_init board_suite_run \
	         SDIO_CfgInitialize SDMMCHOST_Init SDIO_IO_Read_Extended \
	         dwc_mmc_init dwc_mmc_transfer dwc_mmc_board_cfg \
	         lkpi_sdio_fsl_install lkpi_sdio_be_card_init sdmmc_card_start \
	         lkpi_sdio_start sdio_readb sdio_memcpy_fromio \
	         array_mp_8188f_fw_nic lkpi_module_init_run; do \
		$(NM) $(TARGET).elf | awk '{print $$3}' | grep -qx "$$s" \
			|| { echo "FAIL: $$s not in the image"; exit 1; }; \
	done; \
	undef=$$($(NM) -u $(TARGET).elf); \
	[ -z "$$undef" ] || { echo "FAIL: undefined symbols:"; echo "$$undef"; exit 1; }; \
	echo "PASS: _start=0x$$start, SDIO/driver/kernel/board symbols present, 0 undefined"
	@echo "== module-section density =="
	@set -e; \
	lm=$$($(NM) $(TARGET).elf | awk '$$3=="__lkpi_module_end"{print $$1}'); \
	lm0=$$($(NM) $(TARGET).elf | awk '$$3=="__lkpi_module_start"{print $$1}'); \
	li=$$($(NM) $(TARGET).elf | awk '$$3=="__lkpi_init_end"{print $$1}'); \
	li0=$$($(NM) $(TARGET).elf | awk '$$3=="__lkpi_init_start"{print $$1}'); \
	lmz=$$(( 0x$$lm - 0x$$lm0 )); liz=$$(( 0x$$li - 0x$$li0 )); \
	echo "lkpi_module bytes=$$lmz (expect 0: this driver registers through module_init), lkpi_init bytes=$$liz (expect 8*1)"; \
	[ $$lmz -eq 0 ] || { echo "FAIL: the USB module registry section is not empty on the SDIO line"; exit 1; }; \
	[ $$liz -eq 8 ] || { echo "FAIL: the init section does not carry exactly one entry"; exit 1; }; \
	echo "PASS: init section carries one entry; module registry empty"
	@echo "== case-name reconciliation (image / suite.mk / judge) =="
	@sh $(COMMON)/check-case-list.sh $(TARGET).elf $(CURDIR)/suite.mk $(CURDIR)/judge.py $(OBJCOPY) $(READELF) $(BUILD)
	@$(SIZE) $(TARGET).elf
	@echo "build-tag: $(BUILD_TAG)"

# Header dependencies: -MMD -MP writes one .d per object, so a header
# edit rebuilds every object that included it (a stale object would
# otherwise be flashed as the current build).
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

clean:
	rm -rf $(BUILD)
