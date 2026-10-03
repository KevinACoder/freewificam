# patches/RTL8851bu

Realtek RTL8851BU vendor 驱动上游差异的**唯一真值源**（submodule 工作树禁改；
差异只以 patch 形式存 here，`make modules` 构建期重放进 `third-party/RTL8851bu/`；
目录名必须与 submodule 目录逐字一致——modules 重放循环以 patches/<目录名> 推导
目标 `git -C third-party/<目录名>`）。

## pin

- 上游 URL：<https://gitcode.com/windyoung2020/RTL8851bu>（.gitmodules）
- pin = **`5b6408dd54fae36abe97114554e9883c38332e7f`**（HEAD，PHL 架构 2024 树，
  GPL-2.0 标头体例、根无 LICENSE 文件——见 third-party/RTL8851bu.provenance.md，
  985 c/h、fw 内嵌 `phl/hal_g6/mac/fw_ax/rtl8851b/`）。
- 消费轮：S5b W1（feat/wlan-8851bu-r1，2026-10-04，自 feat/wlan-8821cu @08c1be7
  派生，spec v3.2）；先行验证轮 W0-B 在 ~/workspace/rk3568_lab/os/rtl8851bu-v5
  工作副本分支 `linux-7.3` @ `b0928db`（= pin + 8 提交对 Linux 7.3-rc2，546 行
  compat patch，runs/20261004-rtl8851bu-w0b-linux/）板证全链。

## patches

| patch | 内容 |
|---|---|
| `0001-rtos-trim-macros.patch` | **裁剪宏**（include/autoconf.h，[S5a] 0001 血统）：`DBG 1→0`；`DBG_MEM_ALLOC/DBG_PHL_MEM_ALLOC/DBG_HAL_MAC_MEM_MOINTOR/DBG_HAL_MEM_MOINTOR` 内存审计计数器族 off（RTOS 基线）；`CONFIG_USB_TX/RX_AGGREGATION` off（RX=每 recvbuf 重排队、TX=bulk-OUT 语义，同 [S5a] W1 形态——聚合是 W11b 已量化的数据面二段开关，跨线遗留 8821→8851 单向同步后另议）。**保持 PLATFORM_LINUX / PHL_PLATFORM_LINUX 经编译命令行定义**（[S5a] FR-2 同款：osdep LINUX 分支照常编译）。 |
| `0002-rtos-export-shells-and-stubs.patch` | **导出壳 + 链接桩 + 数据 seam**（[FWC]/[S5a] 0002 血统）：①os_dep/linux/usb_intf.c 末尾导出 `rtl8851bu_rtos_init/_deinit` 包 `rtw_drv_entry/_halt`——module_init/module_exit 被 linuxkpi no-op 化，adapter attach 路径显式调用，drv_registered 处理与 Linux 模块加载路径同路；②os_dep/linux/os_intfs.c 顶部 `rtw_ioctl` 3 行 `-EOPNOTSUPP` 桩——ioctl_linux.c 不编，而本文件 rtw_siocdevprivate/net_device_ops 表 5 处引用（:215/:218/:340/:356/:386）；③os_dep/osdep_service.c `_rtw_netif_rx` 补 `#else` 直通（`skb->dev=ndev; return netif_rx(skb);`）——**V5 特有**：该函数唯一定义点整体在 `#ifdef CONFIG_HWSIM` 内（:18-27）而 HWSIM 缺省关，缺省配置链接必败；直通使数据 seam 落点天然归第一方（spec §3.2.2 V5 差异项 2）。 |

> **W0-B 兼容 patch（546 行/12 文件）不搬运的记账**：W0-B 断层是「[V5] 2024 树 ×
> 内核 7.3」的版本断层（timer 改名/sha256 撞名/strncpy 删除/cfg80211 ops
> netdev→wireless_dev 现代化/cookie 传值/CAC wdev 化）；RTOS 侧的 Linux API 面
> 由 components/linuxkpi **自行呈现**（按 [V5] 期望的 API 形态，非 7.3 形态），
> 故 timer_container_of/timer_delete、rtw_hmac_sha256、strscpy、cfg80211 ops
> 现代化等条目在 RTOS 面不存在同款断层，不搬运。osdep 面（writeFile/kthread/
> iov_iter/set_fs）W0-B 实测零断层（PATCH-LEDGER"明确不做"节），RTOS 同样零补。
> W1 编译若现新断层 → 补 0003+ 记账（模型照 patches/rtl8821cu/ 演进史）。

## 编译面（freewebcam Makefile `NIC_RTL8851BU` 块持有，此处记账口径）

- 编译集 = W0-B 板证 374 单元面（USB/PHL/POWER_SAVE=n/BR_EXT=n）**minus 11 显式
  裁剪**：os_dep/linux/{ioctl_linux,ioctl_cfg80211,rtw_cfgvendor,wifi_regd,
  rtw_proc,nlrtw,rtw_android,rtw_rhashtable,ioctl_mp,ioctl_efuse}.c +
  core/wds/rtw_wds.c。nlrtw/rtw_rhashtable/rtw_wds 三单元内容天然空体
  （`CONFIG_RTW_NLRTW/RTW_WDS` 定义在 `CONFIG_RTW_MULTI_AP` 门内、MULTI_AP 缺省
  关；W0-B 板上 `nm` 实证 nlrtw.o/rtw_rhashtable.o 零符号）——**无需 drv_conf
  patch**，仅编译面剔除；nlrtw.h 自带 inline-fail no-op 覆盖 core 消费者。
- 宏集 = W0-B 板证 kbuild .cmd 抽取（core/.rtw_cmd.o.cmd）minus kbuild 内建
  （KBUILD_*/MODULE/KASAN/CONFIG_CC_HAS_K_CONSTRAINT/ARM64_ASM_ARCH）minus 裁剪
  宏（CONFIG_IOCTL_CFG80211/RTW_USE_CFG80211_STA_EVENT/CONFIG_PROC_DEBUG/
  CONFIG_RTW_NAPI/CONFIG_RTW_GRO/CONFIG_RTW_NETIF_SG/CONFIG_RADIO_WORK）。DM_ODM
  系列**不适用**（PHL 架构无 PHYDM）。
- include 集：linuxkpi 阴影层打头 + `-I include -I core/crypto -I phl/hal_g6
  -I platform`（W0-B .cmd 同款 4 目录）。

## 重放（构建期，Makefile `modules` target 同款）

```sh
git -C third-party/RTL8851bu apply --check "$PWD/patches/RTL8851bu/<p>.patch" \
  && git -C third-party/RTL8851bu apply "$PWD/patches/RTL8851bu/<p>.patch"
```

物化态（应用后的工作树）**不入 submodule 提交**；0001+0002 重放出与生成时
工作树逐字节相同的差异（W1 轮 replay 幂等两连验已过：apply → diff 逐字节同
→ checkout 复原）。
