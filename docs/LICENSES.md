# LICENSES

Per-component license registry. This is not legal advice.

| Part | License | Notes |
|---|---|---|
| `tests/board/rtos/` (platform, app, build) | BSD-3-Clause | first-party; `LICENSE` at the repository root |
| `components/liblinux80211/` (written here) | BSD-3-Clause | the component's own code (`Kconfig`, `sources.mk`, seams, adapters, cases) |
| `components/liblinux80211/include/` + the imported bodies | BSD-3-Clause | the previous tree's first-party linuxkpi, byte-identical; the Linux face is the interface the stack compiles against, not the stack |
| `tools/` (written here) | BSD-3-Clause | |
| `tools/kconfig/` | ISC | Kconfiglib, vendored (see its `VENDOR`) |
| `third-party/FreeRTOS-Kernel/` | MIT | `LICENSE.md` at the tree root |
| `third-party/CMSIS_6/` | Apache-2.0 | `LICENSE` at the tree root |
| `third-party/CMSIS-FreeRTOS/` | Apache-2.0 AND MIT | `LICENSE` at the tree root |
| `third-party/lwip/` | BSD-3-Clause style | `COPYING` at the tree root |
| `third-party/cherryusb/` | Apache-2.0 | `LICENSE` at the tree root; eight patches applied in the tree, recorded in `patches/` |
| `third-party/fsl_sdmmc/` | BSD-3-Clause | NXP copyright blocks; `COPYING-BSD-3` beside the tree |
| `third-party/printf/` | MIT | |
| `third-party/tlsf/` | BSD-3-Clause | terms in the `tlsf.h` header |
| `third-party/ulog/` | MIT | |
| `third-party/wpa_supplicant-rtos/` | BSD-3-Clause | NXP `wpa_supplicant-rtos` `release/25.06.00` (`c21bbad`), 897 files, unmodified apart from the port patches in its `patches/`; upstream `port/` (mbedtls/KSDK glue, Apache-2.0) is **not** imported - see `PROVENANCE.md` |
| `components/liblinux80211/fw/iwlwifi-7260-17.ucode` | Intel binary firmware licence (`LICENCE.iwlwifi_firmware` beside it) | **tracked input**, unmodified; sha256 `5d81a600…`; source and terms in `fw/PROVENANCE.md` |

## Combined products

The four acceptance images (tick, wired network, PCIe/NVMe, eMMC, USB
storage) contain no GPL unit: they are platform, bus and network code under the
licenses above. The wireless profiles below do link the GPL-2.0 stack.

When `components/liblinux80211/` lands the real `net/mac80211` and
`net/wireless` (`GPL-2.0-only`), every binary that links it becomes a GPL-2.0
combined work regardless of the license of the parts written here. That
boundary is the reason the component is a separate build unit and a separate
profile family: the Realtek BSD-option profiles must never link it.

## Import direction

Imports come in; nothing goes back out to a tree that must stay free of
GPL-2.0. If a component here is ever useful elsewhere, the version to share is
the one without the imported stack linked in.

## 4. 本轮的无线产物（2026-11-10，S1b/C2/C3）

| 产物 | 组成 | 许可证 |
|---|---|---|
| `rk3568-mac80211-c2` 镜像 | mac80211/cfg80211（GPL-2.0-only）静态链接 | **GPL-2.0** 链接产物 |
| `rk3568-iwl7260-mac80211` 镜像 | 同上 + iwlwifi/iwlmvm（GPL-2.0） | **GPL-2.0** 链接产物 |
| `rk3568-iwl7260-c4` 镜像 | 同上（C4 的触发 helper 是第一方 BSD-3-Clause，与 GPL 栈链接） | **GPL-2.0** 链接产物 |
| `rk3568-iwl7260-c5` 镜像 | 同上（C5 的 nl80211 connect helper 是第一方 BSD-3-Clause，与 GPL 栈链接） | **GPL-2.0** 链接产物 |
| `rk3568-iwl7260-c6` 镜像 | 同上（C6T 的 lwIP 相位面与吞吐工具是第一方 BSD-3-Clause，与 GPL 栈链接） | **GPL-2.0** 链接产物 |
| `rk3568-iwl7260-c6-pool128` 镜像 | 同上（只是板级 skb 池尺寸不同，仍是 GPL 栈 + iwlwifi 的链接产物） | **GPL-2.0** 链接产物 |
| `rk3568-rtl8189fs-ap` 镜像 | 8189FTV 厂商栈 `wireless-drivers/rtl8189fs`（GPL-2.0 per its `PROVENANCE.md`）+ 第一方 BSD-3-Clause 的 case/runner/kpi 层，静态链接 | **GPL-2.0** 链接产物 |
| `rk3568-rtl8189fs-ap-tp` 镜像 | 同上，另加 `CONFIG_RTL8189FS_AP_TP` 的相位表 | **GPL-2.0** 链接产物 |
| `rk3568-iwl7260-stab` 镜像 | 同上（稳定性 case 与 pool128 配置） | **GPL-2.0** 链接产物 |

两者都在 `docs/LICENSES.md`（外层）同表登记；`ARCHITECTURE.md` §5 的隔离规则不变：
这两个 profile 不与 `rtw88`/`rtw89` 的 BSD profile 共链。

## 5. 固件（2026-11-11 起入库）

`components/liblinux80211/fw/iwlwifi-7260-17.ucode` 是 **Intel 二进制固件**，
不是 GPL，也不是 BSD：它按 `LICENCE.iwlwifi_firmware` 的条款随镜像再分发
（允许未修改的二进制再分发，要求保留版权声明；禁止反向工程）。它现在与
licence 文本、`PROVENANCE.md` 一起 tracked，因为镜像没有文件系统、干净 clone
必须能从仓内生成 `iwl7260_fw_blob.c`。生成物本身仍是构建产物，不入库。

链接这些固件的无线镜像因此同时是 **GPL-2.0 链接产物** 和 **Intel 固件再分发物**；
两件事分别登记，不互相替代。

`components/liblinux80211/fw/rtw89/rtw8851b_fw-1.bin`（1,408,706 B，sha256
`1c4ccb5f…`，2026-11-13 入库）是 **Realtek 二进制固件**，按同目录
`LICENCE.realtek_firmware` 的条款再分发：允许未修改的二进制再分发，要求保留
版权声明与免责声明，禁止反向工程/反编译。它同样是 tracked 输入，生成的
`rtw8851b_fw_blob.c` 是构建产物、不入库；来源与校验见 `fw/PROVENANCE.md`。
链接它的 rtw89 镜像因此是 **GPL-2.0 链接产物 + Realtek 固件再分发物**，两件事
分别登记。`rtw89` 驱动本身是 `GPL-2.0 OR BSD-3-Clause`，但本仓的 mac80211
backend 把它链接进 GPL-2.0 产物——它不构成老仓按 BSD 发布的那条 rtw89 线。

