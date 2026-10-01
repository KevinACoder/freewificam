#!/bin/sh
# P6-a 判据 2 的三驱动编译级验证 (specs/linuxkpi/SPEC.md §4/P6-a 判据 2)。
#
# 口径: gcc -fsyntax-only, 驱动零修改; 头面 = components/linuxkpi/include
# (+ [EXPL] include/net 形状头只读引用, 不迁不入库, §3.1) + driver-check
# overlay (见该头注)。镜像只链 lkpi_udisk; 两个 wlan 驱动是编译级验证件。
# 已知排除 (留痕于 runs 台账):
#   - *_e.c/*_s.c (PCIe/SDIO 变体): kbuild 在 USB 目标不会编;
#   - mac80211.c / wow.c (rtw88): 7.3 新结构 (ieee80211_link_sta /
#     cfg80211_pkt_pattern) 属 80211 胶水面 = P6-2/S8 域 (spec §3.8);
#   - core.c (rtl8xxxu): debugfs 块无预处理守卫, wiphy->debugfsdir 成员
#     是 netdev/debugfs 结构面 ([EXPL] cfg80211.h 无 wiphy 结构), 不可
#     宏补 —— 同 P6-2/S8 域。芯片表文件含 rtl8xxxu.h 的 usb_anchor 内嵌
#     全部过编 (anchor 形状与四函数本仓已交付)。
# 用法: sh components/linuxkpi/driver-check/run_check.sh
set -e

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LNX=${LNX:-$HOME/workspace/rk3568_lab/os/linux-upstream}
EXPL=${EXPL:-$HOME/workspace/project_web_camera/fwc-net80211-lkpi/port/adapters/linuxkpi}
CC=${CC:-$HOME/workspace/env/src/xpack-aarch64-none-elf-gcc-13.2.1-1.1/bin/aarch64-none-elf-gcc}

FLAGS="-fsyntax-only -std=gnu11 -Wno-unused -Wno-address-of-packed-member \
  -mgeneral-regs-only -include $REPO/components/linuxkpi/driver-check/drvcheck_overlay.h \
  -I$REPO/components/linuxkpi/include -I$EXPL/include"

check_one() {
	# check_one <file> <extra-defs...>
	f=$1; shift
	if $CC $FLAGS "$@" "$f" 2>/tmp/lkpi_drvcheck.err; then
		return 0
	fi
	echo "FAIL: $f"
	grep 'error' /tmp/lkpi_drvcheck.err | sort -u | head -4
	return 1
}

fail=0
pass=0

echo "== rtw88 (USB 相关全集) =="
cd "$LNX/drivers/net/wireless/realtek/rtw88"
for f in $(ls *.c | grep -v 'pci\|sdio\|[se]\.c$\|rtw8703b\.c\|mac80211\.c\|wow\.c'); do
	extra=""
	[ "$f" = "led.c" ] && extra="-DCONFIG_RTW88_LEDS=1"   # kbuild: obj-$(CONFIG_RTW88_LEDS)
	if check_one "$f" -DKBUILD_MODNAME='"rtw88"' $extra; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
	fi
done

echo "== rtl8xxxu (全集) =="
cd "$LNX/drivers/net/wireless/realtek/rtl8xxxu"
for f in $(ls *.c | grep -v '^core\.c$'); do
	if check_one "$f" -DKBUILD_MODNAME='"rtl8xxxu"'; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
	fi
done

echo "== lkpi_udisk (本仓, 双靶共源 core + 两壳) =="
cd "$REPO"
for f in app/lkpi_udisk/lkpi_udisk_core.c app/lkpi_udisk/lkpi_udisk_linux.c \
         app/lkpi_udisk/lkpi_udisk_rtos.c; do
	if check_one "$REPO/$f" -DKBUILD_MODNAME='"lkpi_udisk"' \
	    -Iport/adapters/cherryusb -Iport/adapters/cherrysh -Icomponents/cherrysh \
	    -Icomponents/cherrysh/cherryrl -Icomponents/cherryrb \
	    -Ithird-party/cherryusb/core -Ithird-party/cherryusb/common \
	    -Ithird-party/cherryusb/class/hub -Iport/adapters/usb \
	    -Iboard/rk3568-itx -DCONFIG_LINUXKPI=1 -DCONFIG_SHELL=1 \
	    -DCONFIG_USBHOST_XHCI=1 -DTHREADX_BUILD=1 \
	    -Ithird-party/threadx/common/inc \
	    -Ithird-party/threadx/ports/cortex_a55/gnu/inc -Ios/threadx \
	    -DCONFIG_TX_TICKS_PER_SECOND=1000 -DCONFIG_TX_TIMER_STACK=4096 \
	    -DCONFIG_TX_MAX_THREADS=16 -DCONFIG_HEAP_BYTES=0x400000 \
	    -Icomponents/cmsis5 -Ios/cmsis2; then
		pass=$((pass+1))
	else
		fail=$((fail+1))
	fi
done

echo "check-drivers: pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
