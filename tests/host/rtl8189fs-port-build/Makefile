# Static build gates for the vendored rtl8189fs driver and its first-party header face.
#
# Self contained; run it from anywhere with:
#   make -C <repo>/tests/host/rtl8189fs-port-build
#
# The driver is vendored bytes and the cfg80211/netdev/WEXT/osdep header face it compiles
# against is this project's own, so the checks split the same way as the rtw88 gate:
#
#   compile    the 131 RTL8188F translation units compile for aarch64 against the
#              first-party face, at the warning level the driver's own build takes
#   contract   the face headers compile as one translation unit on their own, at a
#              stricter warning level than the driver's, so the face cannot hide
#              behind the bytes it serves
#   spdx       the license split is mechanical: the vendored files carry exactly the
#              Realtek GPL-2.0 see-bottom block (no SPDX line anywhere), the first-party
#              face carries the project's (BSD-3-Clause) and no license text copied from
#              elsewhere
#   gates      the kernel-isolation gates over the driver objects (symbol gate) and
#              the first-party face sources, with their canary proofs
#
# Configuration, and why it is what it is: the driver's own build selects the compiled set
# and the feature set through the make variables in its Makefile. Those variables resolve
# to a set of -D switches, and the lab's own build of this pin left them in its .o.cmd
# files; DRV_CONFIG below is that resolved set, transcribed rather than re-derived, so the
# units compile exactly as the driver's own build compiles them. Two switches worth naming:
# CONFIG_RTW_MESH is absent there, which is why the rhashtable layer (and the kernel
# rhashtable/jhash/rcupdate headers it would need) compiles away entirely, and
# CONFIG_RTL8188F is what selects the chip half.
#
# Warning level, and why it is what it is: the driver units are compiled at the warning
# level the kernel's own build of this directory takes - -Wall -Wextra plus the switches
# the kernel's default warning set turns back off for every directory
# (scripts/Makefile.warn: -Wno-array-bounds and -Wno-address-of-packed-member among them),
# the per-directory relaxations the rtw88/rtw89 gates already carry, and the ones this
# driver's own bytes need (-Wno-unused, -Wno-unused-variable, which its Makefile sets
# itself). Everything else stays on, and warnings are counted rather than escalated, so a
# regression shows up as a count in the log; the vendored bytes' own always-true-comparison
# diagnostics are recorded by file and line in KNOWN_WARN below.

MAKEFILE_DIR := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
ROOT  := $(abspath $(MAKEFILE_DIR)/../../..)
DRV   := $(shell sh $(ROOT)/tools/patches/replay.sh rtl8189fs)
# The license ledger describes the pin, not the patched build input: the replay
# copy has no .git, so the scan reads the submodule itself.
VENDOR := $(ROOT)/third-party/rtl8189fs
FACE  := $(ROOT)/components/linuxkpi/include
BUILD := $(MAKEFILE_DIR)/build

# A space, used to turn the allowlisted warning sites into a grep alternation.
empty :=
space := $(empty) $(empty)

AARCH64_CROSS_PATH ?= $(HOME)/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1
CROSS_COMPILE ?= $(AARCH64_CROSS_PATH)/bin/aarch64-none-elf-
CC  := $(CROSS_COMPILE)gcc

# The 131 RTL8188F translation units: the ground-truth module link table 8189fs.mod of the
# lab's own build of this pin (37 core, 16 os_dep, 29 hal, 48 hal/phydm, 1 platform).
SRCS := \
    core/rtw_cmd.c core/rtw_security.c core/rtw_debug.c core/rtw_io.c \
    core/rtw_ioctl_query.c core/rtw_ioctl_set.c core/rtw_ieee80211.c core/rtw_mlme.c \
    core/rtw_mlme_ext.c core/rtw_mi.c core/rtw_wlan_util.c core/rtw_vht.c \
    core/rtw_pwrctrl.c core/rtw_rf.c core/rtw_chplan.c core/rtw_recv.c \
    core/rtw_sta_mgt.c core/rtw_ap.c \
    core/mesh/rtw_mesh.c core/mesh/rtw_mesh_pathtbl.c core/mesh/rtw_mesh_hwmp.c \
    core/rtw_xmit.c core/rtw_p2p.c core/rtw_rson.c core/rtw_tdls.c core/rtw_br_ext.c \
    core/rtw_iol.c core/rtw_sreset.c core/rtw_btcoex_wifionly.c core/rtw_btcoex.c \
    core/rtw_beamforming.c core/rtw_odm.c core/rtw_rm.c core/rtw_rm_fsm.c \
    core/efuse/rtw_efuse.c core/rtw_sdio.c core/rtw_mp.c \
    os_dep/osdep_service.c os_dep/linux/os_intfs.c os_dep/linux/sdio_intf.c \
    os_dep/linux/sdio_ops_linux.c os_dep/linux/ioctl_linux.c os_dep/linux/xmit_linux.c \
    os_dep/linux/mlme_linux.c os_dep/linux/recv_linux.c os_dep/linux/ioctl_cfg80211.c \
    os_dep/linux/rtw_cfgvendor.c os_dep/linux/wifi_regd.c os_dep/linux/rtw_android.c \
    os_dep/linux/rtw_proc.c os_dep/linux/rtw_rhashtable.c os_dep/linux/ioctl_mp.c \
    os_dep/linux/custom_gpio_linux.c \
    hal/hal_intf.c hal/hal_com.c hal/hal_com_phycfg.c hal/hal_phy.c hal/hal_dm.c \
    hal/hal_dm_acs.c hal/hal_btcoex_wifionly.c hal/hal_btcoex.c hal/hal_mp.c \
    hal/hal_mcc.c hal/hal_hci/hal_sdio.c hal/led/hal_led.c hal/led/hal_sdio_led.c \
    hal/HalPwrSeqCmd.c hal/rtl8188f/Hal8188FPwrSeq.c hal/rtl8188f/rtl8188f_sreset.c \
    hal/rtl8188f/rtl8188f_hal_init.c hal/rtl8188f/rtl8188f_phycfg.c \
    hal/rtl8188f/rtl8188f_rf6052.c hal/rtl8188f/rtl8188f_dm.c \
    hal/rtl8188f/rtl8188f_rxdesc.c hal/rtl8188f/rtl8188f_cmd.c \
    hal/rtl8188f/hal8188f_fw.c hal/rtl8188f/sdio/sdio_halinit.c \
    hal/rtl8188f/sdio/rtl8189fs_led.c hal/rtl8188f/sdio/rtl8189fs_xmit.c \
    hal/rtl8188f/sdio/rtl8189fs_recv.c hal/rtl8188f/sdio/sdio_ops.c \
    hal/efuse/rtl8188f/HalEfuseMask8188F_SDIO.c \
    hal/phydm/phydm_debug.c hal/phydm/phydm_antdiv.c hal/phydm/phydm_soml.c \
    hal/phydm/phydm_smt_ant.c hal/phydm/phydm_antdect.c hal/phydm/phydm_interface.c \
    hal/phydm/phydm_phystatus.c hal/phydm/phydm_hwconfig.c hal/phydm/phydm.c \
    hal/phydm/phydm_dig.c hal/phydm/phydm_pathdiv.c hal/phydm/phydm_rainfo.c \
    hal/phydm/phydm_dynamictxpower.c hal/phydm/phydm_adaptivity.c \
    hal/phydm/phydm_cfotracking.c hal/phydm/phydm_noisemonitor.c \
    hal/phydm/phydm_beamforming.c hal/phydm/phydm_direct_bf.c hal/phydm/phydm_dfs.c \
    hal/phydm/txbf/halcomtxbf.c hal/phydm/txbf/haltxbfinterface.c \
    hal/phydm/txbf/phydm_hal_txbf_api.c hal/phydm/phydm_adc_sampling.c \
    hal/phydm/phydm_ccx.c hal/phydm/phydm_psd.c hal/phydm/phydm_primary_cca.c \
    hal/phydm/phydm_cck_pd.c hal/phydm/phydm_rssi_monitor.c hal/phydm/phydm_auto_dbg.c \
    hal/phydm/phydm_math_lib.c hal/phydm/phydm_api.c hal/phydm/phydm_pow_train.c \
    hal/phydm/phydm_lna_sat.c hal/phydm/phydm_pmac_tx_setting.c hal/phydm/phydm_mp.c \
    hal/phydm/halrf/halrf.c hal/phydm/halrf/halrf_debug.c hal/phydm/halrf/halphyrf_ce.c \
    hal/phydm/halrf/halrf_powertracking_ce.c hal/phydm/halrf/halrf_powertracking.c \
    hal/phydm/halrf/halrf_kfree.c hal/phydm/halrf/halrf_psd.c \
    hal/phydm/rtl8188f/halhwimg8188f_bb.c hal/phydm/rtl8188f/halhwimg8188f_mac.c \
    hal/phydm/rtl8188f/halhwimg8188f_rf.c hal/phydm/rtl8188f/phydm_regconfig8188f.c \
    hal/phydm/halrf/rtl8188f/halrf_8188f.c hal/phydm/rtl8188f/phydm_rtl8188f.c \
    platform/platform_ops.c

ARCH   := -mcpu=cortex-a55 -mgeneral-regs-only -mstrict-align -mno-outline-atomics
COMMON := -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fms-extensions \
    -g3 -O2 -ffunction-sections -fdata-sections -fno-strict-aliasing
INC    := -I$(FACE) -I$(DRV) -I$(DRV)/include -I$(DRV)/platform -I$(DRV)/hal/phydm \
    -I$(DRV)/hal/btc -I$(ROOT)/components/linuxkpi/sdio \
    -I$(ROOT)/components/ulog/src

# The configuration the driver's own build resolves to for CONFIG_RTL8188F + CONFIG_SDIO_HCI
# (transcribed from the lab build's .o.cmd, see the header comment). One switch differs from
# the lab build: CONFIG_PROC_DEBUG is off here, because it exists to publish the driver's
# /proc/net/rtl8189fs debug interface and this port has no procfs. With it off the whole
# seq_file/procfs surface compiles away (rtw_proc.c becomes empty and the proc_get_* blocks
# in rtw_debug.c vanish), the file set is unchanged, and the driver's debug output reaches
# the port through the console log instead. That trim is recorded in the round's ledger.
DRV_CONFIG := -D__KERNEL__ -DMODULE -D__KBUILD_MODNAME=8189fs -DKBUILD_MODNAME='"8189fs"' \
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

# Header dependencies are tracked: the face is edited far more often than the driver
# bytes, and a stale object would report the previous header's warnings.
DEPFLAGS := -MMD -MP

# The kernel's own default set for a driver directory (scripts/Makefile.warn), plus the
# relaxations this driver's own bytes need (its Makefile sets -Wno-unused-variable,
# -Wno-unused-function, -Wno-unused itself).
DRV_WARN    := -Wall -Wextra -Wno-address-of-packed-member -Wno-maybe-uninitialized \
    -Wno-missing-field-initializers -Wno-shift-negative-value -Wno-sign-compare \
    -Wno-unused-parameter -Wno-type-limits -Wno-pointer-sign -Wno-unused \
    -Wno-unused-function -Wno-unused-variable -Wno-array-bounds
DRV_CFLAGS  := $(COMMON) $(DRV_CONFIG) $(DRV_WARN)
FACE_CFLAGS := $(COMMON) -Wall -Wextra

.PHONY: all compile contract closure spdx gates clean

all: compile contract closure spdx gates

# ---- compile: the vendored bytes against the first-party face ----------------

OBJS := $(addprefix $(BUILD)/,$(SRCS:.c=.o))

# The interface's own record of the pinned imported headers' one known warning site, plus
# the vendored bytes' own diagnostics. The driver's build takes -Wall, which enables
# -Waddress, and its code has a family of `if (array + n)` and `if (&member)` comparisons
# that the front end reports as always-true; the kernel's own build of this driver emits
# the same lines (its warning set only turns off -Warray-bounds, which this gate also turns
# off). They are recorded by file and line, so a *different* warning in any of these files
# still fails the gate.
KNOWN_WARN := sysfs.h:47 \
    core/rtw_ap.c:4385 core/rtw_ap.c:4405 core/rtw_sta_mgt.c:384 \
    hal/phydm/phydm.c:1579 hal/phydm/phydm.c:1947 \
    hal/phydm/phydm_adaptivity.c:372 hal/phydm/phydm_api.c:1656 \
    hal/phydm/phydm_api.c:1748 hal/phydm/phydm_ccx.c:1402 \
    hal/phydm/phydm_ccx.c:1860 hal/phydm/phydm_debug.c:3137 \
    hal/phydm/phydm_debug.c:3196 hal/phydm/phydm_debug.c:3353 \
    hal/phydm/phydm_debug.c:3739 hal/phydm/phydm_debug.c:3921 \
    hal/phydm/phydm_debug.c:4175 hal/phydm/phydm_debug.c:4969 \
    hal/phydm/phydm_psd.c:413 hal/phydm/phydm_rainfo.c:128 \
    hal/phydm/phydm_rainfo.c:196 hal/phydm/halrf/halrf.c:855 \
    hal/phydm/halrf/halrf_debug.c:90 hal/phydm/halrf/halrf_debug.c:227 \
    hal/phydm/txbf/phydm_hal_txbf_api.c:579

# The two pinned imported headers carry no SPDX line of their own (they are covered by the
# component's provenance record, and editing them is not allowed); the license scan
# exempts exactly those, by path.
FACE_PINNED_NO_SPDX := $(FACE)/linux/device.h $(FACE)/linux/sysfs.h

compile: $(OBJS)
	@set -e; \
	logs=$$(cat $(BUILD)/*.compile.log 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$logs" | grep -c 'warning:' || true); \
	known=$$(printf '%s\n' "$$logs" | grep 'warning:' | grep -cE "$(subst $(space),|,$(KNOWN_WARN))" || true); \
	echo "compile: $(words $(SRCS)) translation units, aarch64, warning lines: $$warn ($$known known, allowlisted by file and line)"; \
	if [ "$$warn" != "$$known" ]; then \
		echo "FAIL: warning outside the recorded known set:"; \
		printf '%s\n' "$$logs" | grep 'warning:' | grep -vE "$(subst $(space),|,$(KNOWN_WARN))"; \
		exit 1; \
	fi

$(BUILD)/%.o: $(DRV)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	@set -e; \
	$(CC) $(ARCH) $(DRV_CFLAGS) -DKBUILD_BASENAME='"$(notdir $(basename $<))"' \
		$(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/$(subst /,_,$(basename $*)).compile.log || \
		{ cat $(BUILD)/$(subst /,_,$(basename $*)).compile.log; exit 1; }

# ---- contract: the face on its own -------------------------------------------

contract: $(BUILD)/face_contract.o
	@set -e; \
	log=$$(cat $(BUILD)/face_contract.compile.log 2>/dev/null || true); \
	warn=$$(printf '%s\n' "$$log" | grep -c 'warning:' || true); \
	known=$$(printf '%s\n' "$$log" | grep 'warning:' | grep -cE "$(subst $(space),|,$(KNOWN_WARN))" || true); \
	echo "contract: the face compiles standalone at -Wall -Wextra, warning lines: $$warn ($$known known, allowlisted)"; \
	if [ "$$warn" != "$$known" ]; then \
		echo "FAIL: the first-party face must compile without warnings of its own:"; \
		printf '%s\n' "$$log" | grep 'warning:' | grep -vE "$(subst $(space),|,$(KNOWN_WARN))"; \
		exit 1; \
	fi

$(BUILD)/face_contract.o: $(MAKEFILE_DIR)/face_contract.c | $(BUILD)
	@set -e; \
	$(CC) $(ARCH) $(FACE_CFLAGS) $(DEPFLAGS) $(INC) -c $< -o $@ 2> $(BUILD)/face_contract.compile.log || \
		{ cat $(BUILD)/face_contract.compile.log; exit 1; }

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

# ---- closure: what the driver's bytes need from the rest of the system --------
#
# The compile gate is per translation unit, so it cannot see a symbol the driver calls
# that nothing defines - that only shows up when the units are put together. This gate
# links them relocatably (no garbage collection, so nothing is dropped before it can be
# counted) and checks the undefined set against the checked-in ledger face-undefined.txt.
# A new entry is a face gap; a removed one is a face change. Either way the ledger has to
# be updated deliberately, which is the point: the rtw88 round found two face gaps this
# way that the compile gate had passed.
LD := $(CROSS_COMPILE)ld
NM := $(CROSS_COMPILE)nm

closure: $(OBJS)
	@set -e; \
	$(LD) -r -o $(BUILD)/rtl8189fs-all.o $(OBJS); \
	$(NM) -u $(BUILD)/rtl8189fs-all.o | awk '{print $$NF}' | LC_ALL=C sort -u > $(BUILD)/undefined.txt; \
	if ! diff -u $(MAKEFILE_DIR)/face-undefined.txt $(BUILD)/undefined.txt > $(BUILD)/closure.diff; then \
		echo "FAIL: the driver's undefined-symbol set changed (a face gap, or a face change):"; \
		cat $(BUILD)/closure.diff; \
		echo "(if the change is intended, update $(MAKEFILE_DIR)/face-undefined.txt)"; \
		exit 1; \
	fi; \
	echo "closure: $$(wc -l < $(BUILD)/undefined.txt) undefined symbols, exactly the recorded face ledger"

# ---- spdx: the license split, mechanically ------------------------------------

spdx:
	@set -e; \
	cd "$(VENDOR)"; \
	total=$$(git ls-files -z | tr '\0' '\n' | wc -l); \
	realtek=$$(git ls-files -z | xargs -0 grep -al 'version 2 of the GNU General Public License' 2>/dev/null | wc -l); \
	if [ "$$realtek" != "518" ]; then echo "FAIL: files carrying the Realtek GPL-2.0 block: $$realtek of $$total (expected 518)"; exit 1; fi; \
	spdx=$$(git ls-files -z | xargs -0 grep -al 'SPDX-License-Identifier' 2>/dev/null | wc -l); \
	if [ "$$spdx" != "0" ]; then echo "FAIL: the vendored tree carries an SPDX line, so the scan is not the see-bottom scan: $$spdx"; exit 1; fi; \
	other=$$(git ls-files -z | xargs -0 grep -laiE 'MIT License|BSD-[0-9]|Apache License|GNU Lesser General Public|version 3 of the GNU' 2>/dev/null || true); \
	if [ -n "$$other" ]; then echo "FAIL: a second license form in the vendored tree:"; printf '%s\n' "$$other"; exit 1; fi; \
	face=$$(find $(FACE)/linux $(FACE)/net $(FACE)/asm -name '*.h' | grep -vE '$(subst $(space),|,$(FACE_PINNED_NO_SPDX))' || true); \
	bad=$$(printf '%s\n' "$$face" | xargs grep -L 'SPDX-License-Identifier: BSD-3-Clause' 2>/dev/null || true); \
	if [ -n "$$bad" ]; then echo "FAIL: first-party face file without the project SPDX:"; printf '%s\n' "$$bad"; exit 1; fi; \
	other=$$(printf '%s\n' "$$face" | xargs grep -h 'SPDX-License-Identifier' 2>/dev/null | sort -u | grep -v 'BSD-3-Clause' || true); \
	if [ -n "$$other" ]; then echo "FAIL: a non-project SPDX form in the face:"; printf '%s\n' "$$other"; exit 1; fi; \
	copy=$$(printf '%s\n' "$$face" | xargs grep -l 'GNU General Public' 2>/dev/null || true); \
	if [ -n "$$copy" ]; then echo "FAIL: license text copied into the first-party face:"; printf '%s\n' "$$copy"; exit 1; fi; \
	echo "spdx: $$total vendored files, 518 with the Realtek GPL-2.0 block, 0 SPDX, no second license form; $$(printf '%s\n' "$$face" | wc -l) face files BSD-3-Clause with no copied license text"

# ---- gates: kernel isolation over the driver objects and the face sources -----

gates: $(OBJS)
	PATH=$(dir $(CROSS_COMPILE)):$$PATH \
	sh $(ROOT)/tools/gates/check-kernel-isolation.sh \
	    --root "$(ROOT)" \
	    --stack-objs "$(BUILD)" \
	    --sources "$(FACE) $(MAKEFILE_DIR)" \
	    --canary-src "$(MAKEFILE_DIR)" \
	    --canary

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
