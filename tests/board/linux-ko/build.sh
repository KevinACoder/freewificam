#!/bin/sh
# Build the kernelmod line's single kernel module from the repository's own sources.
#
# The kernel build copies nothing: kbuild compiles the files it is told about.
# To keep .o files out of the source tree, this script builds a small module
# directory in the build tree with one symlink per source file, generates the
# Kbuild Makefile from the source lists below, and runs `make M=`.
#
# Environment:
#   BUILD   build root (default: <workspace>/build/linux-ko)
#   KSRC    prepared kernel source tree (default: $BUILD/src)
#   KOBJ    prepared kernel object tree (default: $BUILD/obj-debug if present,
#           else $BUILD/obj-stock)
#   KREL    kernel release string (default: 7.3.0-rc2-ge71d9a28760f)
#   REPO    repository root (default: two levels above this script)
#   DEBUGFS 1 = build the rtw89 driver with its debugfs support
#   DIAG    1 = apply patches/diag to a copy of the driver tree in the mirror
#           (diagnostics only; never part of a delivered build)
#   PROF    1 = build the per-frame profiler (prof_enable/prof_dump module
#           params). The module is then an instrumented artifact: its hash and
#           its rates never serve as a baseline.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=${REPO:-$(cd "$HERE/../../.." && pwd)}
BUILD=${BUILD:-$(cd "$REPO/.." && pwd)/build/linux-ko}
KSRC=${KSRC:-$BUILD/src}
if [ -z "${KOBJ:-}" ]; then
	if [ -d "$BUILD/obj-debug/include/config" ]; then
		KOBJ=$BUILD/obj-debug
	else
		KOBJ=$BUILD/obj-stock
	fi
fi
KREL=${KREL:-7.3.0-rc2-ge71d9a28760f}
MOD=$BUILD/wlan-ko
CROSS=${CROSS_COMPILE:-aarch64-linux-gnu-}
JOBS=${JOBS:-$(nproc)}
PROF=${PROF:-0}

# ---- source lists -----------------------------------------------------------
# The net80211 world: imported stack + its port layer + the bridge.  The list
# comes from the same components/net80211/sources.mk the RTOS image uses;
# query.mk's native mode selects the module subset (no Kconfig/Python here, by
# design) so there is no second copy to drift.
STACK_SRCS=$(make -s -f "$REPO/tools/build/query.mk" MODULE=net80211-ko SRCS)
[ -n "$STACK_SRCS" ] || {
	echo "build.sh: empty net80211-ko source list" >&2
	exit 2
}
# The kernel world: the Linux OS services, the mac80211 substitute, the seam
# adapter, and the module entry. No BSD headers here.
LINUX_SRCS="
tests/board/linux-ko/ports/osal/cmsis2_linux.c
tests/board/linux-ko/ports/osal/osal_memory_linux.c
tests/board/linux-ko/ports/osal/osal_abort_linux.c
"
for f in "$REPO"/tests/board/linux-ko/ports/mac80211/*.c "$REPO"/tests/board/linux-ko/ports/adapter/*.c \
	 "$REPO"/tests/board/linux-ko/ports/control/*.c "$REPO"/tests/board/linux-ko/ports/netdev/*.c; do
	[ -f "$f" ] && LINUX_SRCS="$LINUX_SRCS
${f#$REPO/}"
done
if [ "$PROF" = 1 ]; then
	LINUX_SRCS="$LINUX_SRCS
tests/board/linux-ko/ports/prof/wlan_ko_prof.c"
fi
LINUX_SRCS="$LINUX_SRCS
tests/board/linux-ko/main.c"
# The verbatim driver: compiled only against kernel headers. Off until the
# mac80211 substitute and the seam adapter exist (K2): its undefined
# ieee80211_* face is exactly what those two files provide.
#
# DRIVER selects the one driver the module carries. Each is a byte-faithful
# import; the kernelmod line loads one at a time because the USB drivers would
# otherwise claim the same dongles. The default is the repository's primary
# line (rtw89); the rtl8xxxu arm passes DRIVER=rtl8xxxu.
WITH_DRIVER=${WITH_DRIVER:-1}
DRIVER=${DRIVER:-rtw89}
DRIVER_SRCS=""
DRIVER_DEFINES=""
FW_SRCS=""
if [ "$WITH_DRIVER" = 1 ]; then
	case "$DRIVER" in
	rtl8xxxu)
		for f in "$REPO"/wireless-drivers/rtl8xxxu/*.c; do
			[ -f "$f" ] && DRIVER_SRCS="$DRIVER_SRCS
${f#$REPO/}"
		done
		;;
	rtw88)
		# The set the kernel builds for CONFIG_RTW88_8821CU, plus the
		# two units the debug kernel's CONFIG_PM/CONFIG_RTW88_LEDS pull
		# in (the driver's own Makefile adds them the same way).
		DRIVER_DEFINES="CONFIG_RTW88_CORE CONFIG_RTW88_8821C \
CONFIG_RTW88_USB CONFIG_RTW88_8821CU"
		for f in "$REPO"/wireless-drivers/rtw88/*.c; do
			[ -f "$f" ] || continue
			case "$(basename "$f")" in
			main.c|mac80211.c|util.c|debug.c|tx.c|rx.c|mac.c|\
phy.c|coex.c|efuse.c|fw.c|ps.c|sec.c|bf.c|sar.c|regd.c|\
wow.c|led.c|rtw8821c.c|rtw8821c_table.c|rtw8821cu.c|usb.c)
				DRIVER_SRCS="$DRIVER_SRCS
${f#$REPO/}"
				;;
			esac
		done
		# The 8821CU firmware is compiled into the module from the
		# project's own registry/blob pair, so a cold boot needs no
		# /lib/firmware file. The 8188 build keeps firmware_class.
		# The 8821C blob is an image fact and is not vendored in this
		# repository.  The protocol-stage arm generates it outside the
		# tree and names it with RTW88_FW_BLOB; a build without it (or
		# FW_EMBED=0) falls back to request_firmware().
		FW_SRCS="
components/linuxkpi/fw_registry.c"
		if [ -n "${RTW88_FW_BLOB:-}" ] && [ -f "$REPO/${RTW88_FW_BLOB#/}" ]; then
			FW_SRCS="$FW_SRCS
$RTW88_FW_BLOB"
			DRIVER_DEFINES="$DRIVER_DEFINES CONFIG_WLAN_KO_FW_EMBED \
CONFIG_WLAN_KO_FW_RTW8821C"
		fi
		;;
	rtw89)
		# The set the kernel builds for CONFIG_RTW89_8851BU: the core
		# units (which include the BE files, as the driver's Makefile
		# does unconditionally), the units the debug kernel's
		# CONFIG_PM/CONFIG_RTW89_DEBUG/CONFIG_RTW89_LEDS* pull in, the
		# four 8851B chip units, the 8851BU USB unit and the USB bus.
		DRIVER_DEFINES="CONFIG_RTW89_CORE CONFIG_RTW89_USB \
CONFIG_RTW89_8851B CONFIG_RTW89_8851BU"
		for f in "$REPO"/wireless-drivers/rtw89/*.c; do
			[ -f "$f" ] || continue
			case "$(basename "$f")" in
			core.c|mac80211.c|mac.c|mac_be.c|phy.c|phy_be.c|fw.c|cam.c|efuse.c|efuse_be.c|regd.c|sar.c|coex.c|ps.c|chan.c|ser.c|acpi.c|util.c|wow.c|debug.c|led.c|led_mc.c|rtw8851b.c|rtw8851b_table.c|rtw8851b_rfk.c|rtw8851b_rfk_table.c|rtw8851bu.c|usb.c)
				DRIVER_SRCS="$DRIVER_SRCS
${f#$REPO/}"
				;;
			esac
		done
		# The 8851BU firmware is compiled into the module from the
		# project's own registry/blob pair. The driver asks for the -1
		# form first (fw.c walks the format number downwards from the
		# chip's maximum), which is the name the blob registers.
		if [ "${FW_EMBED:-0}" = 0 ]; then
			# No blob: the registry falls back to the file the
			# image carries at /lib/firmware/rtw89.
			FW_SRCS="components/linuxkpi/fw_registry.c"
		else
			DRIVER_DEFINES="$DRIVER_DEFINES CONFIG_WLAN_KO_FW_EMBED \
CONFIG_WLAN_KO_FW_RTW8851B"
			FW_SRCS="
components/linuxkpi/fw_registry.c
components/linuxkpi/fw/rtw8851b_fw_blob.c"
		fi
		;;
	iwlwifi)
		# The 7260 (mvm) set: core + PCIe gen1/2 transport + the cfg
		# families the IWLMVM id table names + fw/* + all mvm units,
		# plus the units the ko kernel's config enables (ACPI, EFI,
		# DEBUGFS, LEDS, PM_SLEEP, DEVICE_TRACING).  DVM/MLD/MEI/tests
		# are vendored but not compiled.  Re-derived from the driver's
		# Makefile/mvm/Makefile + the ko .config; the RTOS 72-TU list is
		# only a seed.
		DRIVER_DEFINES="CONFIG_IWLWIFI CONFIG_IWLMVM \
CONFIG_IWLWIFI_OPMODE_MODULAR CONFIG_WLAN_KO_DRIVER_IWLWIFI"
		IWL_TUS="iwl-io.c iwl-drv.c iwl-debug.c iwl-nvm-utils.c iwl-utils.c
iwl-phy-db.c iwl-nvm-parse.c iwl-dbg-tlv.c iwl-trans.c iwl-devtrace.c
pcie/ctxt-info.c pcie/ctxt-info-v2.c pcie/drv.c pcie/utils.c
pcie/gen1_2/rx.c pcie/gen1_2/tx.c pcie/gen1_2/trans.c
pcie/gen1_2/trans-gen2.c pcie/gen1_2/tx-gen2.c
cfg/7000.c cfg/8000.c cfg/9000.c cfg/22000.c cfg/ax210.c cfg/bz.c cfg/sc.c
cfg/rf-jf.c cfg/rf-hr.c cfg/rf-gf.c
fw/img.c fw/notif-wait.c fw/rs.c fw/dbg.c fw/dbg-old.c fw/pnvm.c fw/dump.c
fw/regulatory.c fw/paging.c fw/smem.c fw/init.c fw/acpi.c fw/uefi.c
mvm/fw.c mvm/mac80211.c mvm/nvm.c mvm/ops.c mvm/phy-ctxt.c mvm/mac-ctxt.c
mvm/utils.c mvm/rx.c mvm/rxmq.c mvm/tx.c mvm/binding.c mvm/quota.c mvm/sta.c
mvm/sf.c mvm/scan.c mvm/time-event.c mvm/rs.c mvm/rs-fw.c mvm/power.c
mvm/coex.c mvm/tt.c mvm/offloading.c mvm/tdls.c mvm/ftm-responder.c
mvm/ftm-initiator.c mvm/rfi.c mvm/mld-key.c mvm/mld-mac.c mvm/link.c
mvm/mld-sta.c mvm/mld-mac80211.c mvm/ptp.c mvm/time-sync.c
mvm/led.c mvm/d3.c"
		for f in $IWL_TUS; do
			[ -f "$REPO/wireless-drivers/iwlwifi/$f" ] || {
				echo "build.sh: missing iwlwifi TU $f" >&2
				exit 2
			}
			DRIVER_SRCS="$DRIVER_SRCS
wireless-drivers/iwlwifi/$f"
		done
		# The driver-specific Linux port lives with the driver tree.
		LINUX_SRCS="$LINUX_SRCS
wireless-drivers/iwlwifi/ports/wlan_iwl_linux.c
wireless-drivers/iwlwifi/ports/mac80211_iwl_closure.c"
		# No blob: iwlwifi asks firmware_class for the name in its cfg
		# table; the board image supplies /lib/firmware/iwlwifi-7260-17.ucode.
		FW_SRCS=""
		;;
	*)
		echo "build.sh: unknown DRIVER=$DRIVER (want rtl8xxxu|rtw88|rtw89|iwlwifi)" >&2
		exit 2
		;;
	esac
fi

# The debugfs surface (rtw89_debugfs) is compiled out unless the kernel has
# CONFIG_RTW89_DEBUGFS; this only mirrors that config on the module side.
if [ "${DEBUGFS:-0}" = 1 ]; then
	DRIVER_DEFINES="$DRIVER_DEFINES CONFIG_RTW89_DEBUGFS"
fi

# ---- compiled-in firmware ---------------------------------------------------
# The blob units are a build product of firmware/ (the RTOS build generates them
# in tests/board/rtos/build/fw.mk); a module that embeds firmware needs the same
# unit, so generate it here rather than requiring a hand-run step first.  The
# rtw88 arm keeps taking the path from RTW88_FW_BLOB, which is also how the
# protocol-stage arm points at a blob outside the tree.
if [ "$DRIVER" = rtw89 ] && [ "${FW_EMBED:-0}" != 0 ]; then
	"$REPO/tools/rtos/fw-blobs.py" --stem rtw8851b_fw
fi
case "${RTW88_FW_BLOB:-}" in
*rtw8821c_fw_blob.c) "$REPO/tools/rtos/fw-blobs.py" --stem rtw8821c_fw ;;
esac

# ---- module directory: a mirrored symlink tree -----------------------------
# Relative includes inside the imported trees ("../wlan_osal.h", same-directory
# headers) must keep resolving, so the module directory mirrors the repository
# layout with one symlink per file (sources and headers alike).  kbuild then
# compiles the mirrored paths; every object stays under this build tree.
TREE=$MOD/tree
rm -rf "$MOD"
mkdir -p "$TREE"
CONFIG_H="$MOD/linuxko_config.h"
{
	echo '#ifndef LINUXKO_CONFIG_H'
	echo '#define LINUXKO_CONFIG_H'
	echo '#define _KERNEL 1'
	echo '#define WLAN_PORT_LINUX 1'
	# _ROUTE_H is deliberately NOT here: it is the kernel's own
	# <net/route.h> guard, and this header is force-included into the driver's
	# TUs too (config header, ccflags).  Defining it there made the kernel's
	# net/ip.h compile without struct rtable (W7-11: iwlwifi pcie/gen1_2/tx.o
	# "invalid use of undefined type const struct rtable").  The BSD headers'
	# guard is the stack's business: STACK_CFLAGS below defines it.
	echo '#define CONFIG_WLAN_KO 1'
	# The seam's TX admission gate (W6-05).  Without a number here the module
	# has no flow control at all (W7-03: unbounded admission -> OOM); 16
	# keeps the driver's queue and the URB in-flight set short.  Until W6-05
	# the define reached no code: the ko build compiles its own adapter
	# (tests/board/linux-ko/ports/adapter/wlan_seam_adapter_linux.c), not the
	# RTOS one that reads this macro, so the module admitted every frame
	# (3.4 GB of pending URB buffers in 4 s).  The ko adapter reads it now,
	# and the module parameter tx_inflight_max overrides it at load time.
	echo "#define CONFIG_WLAN_SEAM_TX_INFLIGHT_MAX ${TX_INFLIGHT_MAX:-96}"
	# How many frames the ko netdev work hands the bridge before it opens the
	# gate (W6-07).  One at a time is what made rtw88's USB layer build a
	# 20 KB aggregate around a single 64-byte frame: the driver's work ran
	# between two frames and always found a queue of one or two.  The batch
	# is capped by the bridge's own 64-entry send queue.
	echo "#define CONFIG_WLAN_KO_TX_BATCH ${TX_BATCH:-96}"
	if [ "$WITH_DRIVER" = 1 ]; then
		echo '#define CONFIG_WLAN_KO_DRIVER 1'
	fi
	if [ "$PROF" = 1 ]; then
		echo '#define CONFIG_WLAN_KO_PROF 1'
	fi
	for d in $DRIVER_DEFINES; do
		echo "#define $d 1"
	done
	echo '#endif'
} > "$CONFIG_H"
for root in "$REPO/components/net80211" "$REPO/components/linuxkpi" \
	    "$REPO/wireless-drivers/rtl8xxxu" "$REPO/wireless-drivers/rtw88" \
	    "$REPO/wireless-drivers/rtw89" "$REPO/wireless-drivers/iwlwifi" \
	    "$REPO/tests/board/linux-ko"; do
	[ -d "$root" ] || continue
	find "$root" -type f | while read -r f; do
		rel=${f#$REPO/}
		mkdir -p "$TREE/$(dirname "$rel")"
		ln -s "$f" "$TREE/$rel"
	done
done

# Diagnostics need a driver tree that can be edited without touching the
# repository's pinned copy: replace the mirror's symlinks with a real copy and
# apply the patches there.  The delivered build leaves DIAG unset, so the
# verbatim tree is what gets compiled and the manifest still describes it.
if [ "${DIAG:-0}" = 1 ]; then
	rm -rf "$TREE/wireless-drivers/rtw89"
	cp -a "$REPO/wireless-drivers/rtw89" "$TREE/wireless-drivers/rtw89"
	for p in "$REPO"/patches/diag/0*.patch; do
		echo "build.sh: diag $(basename "$p")"
		(cd "$TREE/wireless-drivers/rtw89" && patch -p1 --no-backup-if-mismatch < "$p")
	done
fi

# LAB_TRACE=1 adds the observation-only payload hex dump to the driver's
# tracepoints (patches/lab/iwl-trace-payload).  Same rule as DIAG: the mirrored
# symlinks are replaced by a real copy so the repository's verbatim driver tree
# is never edited.  It changes trace output only - no driver behaviour - but the
# module is an instrumented artifact with its own hash, and its results are
# labelled instrumented.
if [ "${LAB_TRACE:-0}" = 1 ]; then
	# LAB_PATCHES: the lab patch series lives in the project checkout, which is
	# not always this repository's parent (the release repo is checked out on
	# its own here).  Default keeps the historical layout.
	LAB_PATCHES=${LAB_PATCHES:-$REPO/../patches/lab}
	patch_file=$LAB_PATCHES/iwl-trace-payload/0001-iwlwifi-trace-payload-hex.patch
	[ -f "$patch_file" ] ||
		{ echo "build.sh: LAB_TRACE patch not found: $patch_file" >&2; exit 2; }
	rm -rf "$TREE/wireless-drivers/iwlwifi"
	cp -a "$REPO/wireless-drivers/iwlwifi" "$TREE/wireless-drivers/iwlwifi"
	patch -p6 -d "$TREE/wireless-drivers/iwlwifi" < "$patch_file"
	echo "build.sh: LAB_TRACE payload hex enabled"
fi

stack_objs=""
for src in $STACK_SRCS; do
	stack_objs="$stack_objs tree/${src%.c}.o"
done
linux_objs=""
for src in $LINUX_SRCS; do
	linux_objs="$linux_objs tree/${src%.c}.o"
done
driver_objs=""
for src in $DRIVER_SRCS; do
	driver_objs="$driver_objs tree/${src%.c}.o"
done
fw_objs=""
for src in $FW_SRCS; do
	fw_objs="$fw_objs tree/${src%.c}.o"
done

{
	echo "obj-m := wlan_ko.o"
	echo "wlan_ko-y := $stack_objs $linux_objs $driver_objs$fw_objs"
	echo
	echo "REPO := $REPO"
	echo "TREE := $TREE"
	echo "CONFIG_H := $CONFIG_H"
	echo
	echo "ccflags-y := \\"
	echo "  -I\$(TREE)/components/net80211/ports/compat \\"
	echo "  -I\$(TREE)/tests/board/linux-ko/ports/include \\"
	echo "  -I\$(TREE)/components/net80211/compat/netbsd \\"
	echo "  -I\$(TREE)/components/net80211 \\"
	echo "  -I\$(TREE)/components/net80211/ports \\"
	echo "  -I\$(TREE)/components/net80211/ports/bridge \\"
	echo "  -I\$(TREE)/tests/board/linux-ko/ports/mac80211 \\"
	echo "  -I\$(TREE)/components/linuxkpi \\"
	echo "  -I\$(TREE)/wireless-drivers/iwlwifi \\"
	echo "  -I\$(TREE) \\"
	echo "  -include \$(CONFIG_H) \\"
	echo "  -mno-outline-atomics"

	echo
	echo "STACK_CFLAGS := -include \$(TREE)/tests/board/linux-ko/ports/stack_prelude.h \\"
	echo "  -D_ROUTE_H -std=gnu11 \\"
	echo "  -Wno-unused-function -Wno-pointer-sign -Wno-unused-parameter \\"
	echo "  -Wno-address-of-packed-member -Wno-missing-braces"
	for src in $STACK_SRCS; do
		echo "CFLAGS_tree/${src%.c}.o += \$(STACK_CFLAGS)"
	done
	if [ "$WITH_DRIVER" = 1 ]; then
		for src in $DRIVER_SRCS; do
			echo "CFLAGS_tree/${src%.c}.o += -include \$(TREE)/tests/board/linux-ko/ports/driver_glue.h"
		done
	fi
	for src in $FW_SRCS; do
		echo "CFLAGS_tree/${src%.c}.o += -include \$(TREE)/tests/board/linux-ko/ports/fw_embed_prelude.h"
	done
} > "$MOD/Makefile"

echo "build.sh: module dir $MOD"
echo "build.sh: kernel     $KOBJ ($KREL)"
echo "build.sh: driver     $DRIVER debugfs=${DEBUGFS:-0} diag=${DIAG:-0}"
make -C "$KSRC" O="$KOBJ" ARCH=arm64 CROSS_COMPILE="$CROSS" \
	KERNELRELEASE="$KREL" M="$MOD" -j"$JOBS" modules

# The closure gate is part of the build, not a separate chore: the module must
# define every mac80211/ieee80211 symbol the verbatim driver pulls, except the
# few cfg80211 exports check-closure.sh names.
if [ -f "$MOD/wlan_ko.ko" ]; then
	NM="${CROSS}nm" sh "$HERE/check-closure.sh" "$MOD/wlan_ko.ko"
fi
