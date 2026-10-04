# components/rtw88 — PROVENANCE

S9 rtw88-adapt spec FR-1：vendored 冻结副本（非 submodule——[LNX] 是 lab 仓，
lab-only 纪律 N8 不允许作为 pin 基底）。

## 来源

- 树：`~/workspace/rk3568_lab/os/linux-upstream`（mainline **7.3-rc2**，
  HEAD `e71d9a28760f`）
- 目录：`drivers/net/wireless/realtek/rtw88/`（98 文件）
- 选集：**USB 构建选集 20 TU = 29,778L**（rtw88_core-y 16 件 + usb.c +
  rtw8821c.c + rtw8821c_table.c + rtw8821cu.c——[RTW88]/Makefile 组成）+
  全部 40 个 .h（本地头随行）
- 获取日期：2026-10-04（S9-W1 实施轮）；选 in-tree 而非 lwfinger dist =
  GT 载体一致性（[S9-GT] 板证全在 in-tree 7.3-rc2 上）
- wow.o/led.o 有意不在副本（CONFIG_PM/CONFIG_RTW88_LEDS 关断，S9 N5/Q7）

## 许可

每文件 `GPL-2.0 OR BSD-3-Clause` 双许可，本仓**取 BSD-3-Clause 选项**
（FreeBSD contrib/dev/rtw88 同例；前作先例 [FWCI] D-LK3）——trunk 合入底线。

## 固件

- `fw_rtw8821c.c` = `tools/gen_firmware_array.py` 生成（非手工）：
  rtw8821c_fw.bin，139,472B，v24.11.0，
  sha256 `2ef409bc418549fcf294061dd0cae1fc22fd9da79b60524950b25de18732f3f0`
  （[LNX] initramfs rootfs 与 lwfinger dist 双源对拍一致 = spec FR-8 登记值）。
- 注册：`fw_rtw8821c_register()` → linuxkpi fw_registry（rtw88 是 registry
  第一个真实消费者）。
- blob 随 Realtek 固件再分发声明分发（源目录随行文件）。

## 验收对拍（W1 判据留痕）

2026-10-04：20 TU + 40 头对 [LNX] 基线 `cmp` 全量**零差异**——
W1 的一切适配都落在 shim 侧（components/linuxkpi/ 80211 增量），rtw88 源
零改动。`DIVERGENCES.md` 当前零条目；后续轮任何源改动必须先记账再改。
