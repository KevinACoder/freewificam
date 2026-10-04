# patches/RTL8851bu

Realtek RTL8851BU vendor 驱动上游差异的**唯一真值源**（submodule 工作树禁改；
差异只以 patch 形式存 here，`make modules` 构建期重放进 `third-party/RTL8851bu/`；
目录名必须与 submodule 目录逐字一致——modules 重放循环以 patches/<目录名> 推导
目标 `git -C third-party/<目录名>`）。

## pin

- 上游 URL：<https://gitcode.com/windyoung2020/RTL8851bu>（.gitmodules）
- pin = **`5b6408dd54fae36abe97114554e9883c38332e7f`**（HEAD，PHL 架构 2024 树，
  GPL-2.0 标头体例、根无 LICENSE 文件——见 third-party/RTL8851bu.provenance.md，
  985 c/h、fw 内嵌 `phl/hal_g6/mac/fw_ax/rtl8851b/`，产物 hal8851b_fw.o data 段
  529,028B = 516.6KB）。
- 消费轮：S5b W1（feat/wlan-8851bu-r1，2026-10-04，自 feat/wlan-8821cu @08c1be7
  派生，spec v3.2）；先行验证轮 W0-B 在 ~/workspace/rk3568_lab/os/rtl8851bu-v5
  工作副本分支 `linux-7.3` @ `b0928db`（= pin + 8 提交对 Linux 7.3-rc2，546 行
  compat patch，runs/20261004-rtl8851bu-w0b-linux/）板证全链。

## patches

| patch | 内容 |
|---|---|
| `0001-rtos-trim-macros.patch` | **裁剪宏**（include/autoconf.h，[S5a] 0001 血统）：`DBG 1→0`；`DBG_MEM_ALLOC/DBG_PHL_MEM_ALLOC/DBG_HAL_MAC_MEM_MOINTOR/DBG_HAL_MEM_MOINTOR` 内存审计计数器族 off（RTOS 基线）；`CONFIG_USB_TX/RX_AGGREGATION` off（RX=每 recvbuf 重排队、TX=bulk-OUT 语义，同 [S5a] W1 形态——聚合是 W11b 已量化的数据面二段开关，跨线遗留 8821→8851 单向同步后另议）。**保持 PLATFORM_LINUX / PHL_PLATFORM_LINUX 经编译命令行定义**（[S5a] FR-2 同款）。 |
| `0002-rtos-export-shells-and-stubs.patch` | **导出壳 + 链接桩**（[FWC]/[S5a] 0002 血统）：①os_dep/linux/usb_intf.c 末尾导出 `rtl8851bu_rtos_init/_deinit` 包 `rtw_drv_entry/_halt`——module_init/module_exit 被 linuxkpi no-op 化，adapter attach 路径显式调用；②os_dep/linux/os_intfs.c 顶部 `rtw_ioctl` 3 行 `-EOPNOTSUPP` 桩——ioctl_linux.c 不编，而本文件 rtw_siocdevprivate/net_device_ops 表 5 处引用（:215/:218/:340/:356/:386）。**修订（W1 编译迭代）**：原计划的 `_rtw_netif_rx #else 直通` hunk **裁撤**——W0-B 台账"osdep_service.c:21-27 是唯一定义点、缺省链接必败"的判断被 RTOS 侧证伪：osdep_service_linux.h:849 本就有 `#else static inline` fallback（CONFIG_HWSIM 门自带），头面已满足数据 seam，vendor .c 零补。 |
| `0003-rtos-ungate-wdev-face.patch` | **wdev 结构面 un-gate**（W0-B 事实的 RTOS 落地：[V5] core 硬依赖 cfg80211 头面，使用点无 CONFIG_IOCTL_CFG80211 门而字段定义有门——门的两侧错位）：①include/drv_types.h：`wdev_data/rtw_wdev`（:1753-1757）、`cfg80211_rochinfo`（:1743）、`dvobj wiphy` 成员（:1247-1249）、`dvobj_to_wiphy` 宏（:1359-1361）四处 un-gate；②include/osdep_intf.h：ioctl_cfg80211.h include 无条件化（rtw_wdev_priv 定义所在，成员类型需要）；③include/osdep_service_linux.h：漏斗补 `#include <net/cfg80211.h>`（linuxkpi stub 面，先于全部 drv_types.h 消费者）；④include/sta_info.h：`pauth_frame/auth_len/passoc_req/assoc_req_len` un-gate（rtw_ap.c 无门消费）；⑤include/rtw_mlme.h：`struct cfg80211_roch_info` un-gate（rtw_scan.c ROC 路径无门消费）；⑥core/rtw_p2p.c：`#error "Enable CONFIG_P2P without CONFIG_IOCTL_CFG80211"` 放宽——P2P 走 [S5a] 编译在场/运行时休眠纪律，结构性 cfg80211 面由 linuxkpi stub 提供、事件面由第一方 seam 桩兜（wlan8851_seam.c）。 |
| `0004-rtos-print-clean-ser-dumps.patch` | **打印降噪（W3，8821 W7 配方血统）**：W2 板证 SER 失败路径 ~35s 转储洪流冲掉环形串口缓冲的事发窗口。根因修正（dbg_lvl 六位默认本就全 0，洪流不在其节制下）：①ser.c `mac_dump_err_status` heavy 块（per-4B DMAC/CMAC CR 走读 + ss/dle dbgpkg + dbg-port 全循环 + 整块 share-mem dump，:398-430）加 `dbg_lvl.dbgprt_dump`（bit5）门——保留 `---> err=0x%x`、SER_DBG_INFO/计数器、dump_err_status_dmac/cmac 有界头、fw_st_dbg_dump（UDM0-3 + HALT_C2H + 15×fw PC）浓缩取证面；恢复手段 = `set_ser_log_lvl` bit5 / `MAC_AX_HW_SET_SER_DBG_LVL`；②init.c 四处失败路径（hal_init/deinit/fast_init/fast_deinit :569/:670/:820/:865）六个 en 位强制 1 撤销——dbg_en 保持声明处 {0}，dbg_status_dump 只跑无条件 crit+fw_st 段；③rtw_cfg.c `#else` 默认档 `_DRV_INFO_→_DRV_WARNING_`（8821 0006 镜像双保险；Makefile 恒传 -DRTW_LOG_LEVEL=3 时惰性）。 |

> **W0-B 兼容 patch（546 行/12 文件）不搬运的记账**：W0-B 断层是「[V5] 2024 树 ×
> 内核 7.3」的版本断层（timer 改名/sha256 撞名/strncpy 删除/cfg80211 ops
> netdev→wireless_dev 现代化/cookie 传值/CAC wdev 化）；RTOS 侧的 Linux API 面
> 由 components/linuxkpi **自行呈现**（按 [V5] 期望的 API 形态，非 7.3 形态），
> 故 timer_container_of/timer_delete、rtw_hmac_sha256、strscpy、cfg80211 ops
> 现代化等条目在 RTOS 面不存在同款断层，不搬运。W1 编译 28 轮迭代实证的
> RTOS 侧真实断层全部落 components/linuxkpi 增量面（cfg80211 stub 头/
> namei/inetdevice/addrconf/sched 漏斗头/nl80211 band+channel+hdr/dev_addr/
> NETIF_F/ndo_siocdevprivate/ss_ep_comp/skb frag walk/list_splice/hlist rcu/
> kthread_complete_and_exit/printk KERN_CONT 族）或 seam 桩（cfg80211_ft_event
> 族 7 件 + BR_EXT seam），零 vendor .c 补丁外溢。W0-B 的 osdep 面（writeFile/
> kthread/iov_iter/set_fs）零断层结论 RTOS 同样成立。

## 编译面（freewebcam Makefile `NIC_RTL8851BU` 块持有，此处记账口径）

- 编译集 = W0-B 板证 374 单元面（USB/PHL/POWER_SAVE=n/BR_EXT=n）**minus 11 显式
  裁剪**：os_dep/linux/{ioctl_linux,ioctl_cfg80211,rtw_cfgvendor,wifi_regd,
  rtw_proc,nlrtw,rtw_android,rtw_rhashtable,ioctl_mp,ioctl_efuse}.c +
  core/wds/rtw_wds.c = **363 单元**（W1 产物 .o 计数精确命中 363）。
  nlrtw/rtw_rhashtable/rtw_wds 三单元内容天然空体（`CONFIG_RTW_NLRTW/RTW_WDS`
  定义在 `CONFIG_RTW_MULTI_AP` 门内、MULTI_AP 缺省关；W0-B 板上 `nm` 实证
  零符号）——无需 drv_conf patch，仅编译面剔除；nlrtw.h 自带 inline-fail
  no-op 覆盖 core 消费者。
- 宏集 = W0-B 板证 kbuild .cmd 抽取（core/.rtw_cmd.o.cmd）minus kbuild 内建
  （KBUILD_*/MODULE/KASAN/CONFIG_CC_HAS_K_CONSTRAINT/ARM64_ASM_ARCH）minus 裁剪
  宏（CONFIG_IOCTL_CFG80211/RTW_USE_CFG80211_STA_EVENT/CONFIG_PROC_DEBUG/
  CONFIG_RTW_NAPI/CONFIG_RTW_GRO/CONFIG_RTW_NETIF_SG/CONFIG_RADIO_WORK）。
  W1 迭代勘误：`CONFIG_RTW_DEBUG` 必带（rtw_cfg.c 的 rtw_drv_log_level 定义
  与 `wlan8851 dbg` 命令面的根）、`CONFIG_RTW_UP_MAPPING_RULE=0`（W0-B .cmd
  原名，初版误抄 RTW_UP_MAPPING_RULE）。DM_ODM 系列**不适用**（PHL 架构无
  PHYDM）。
- include 集：linuxkpi 阴影层打头 + `-I include -I core/crypto -I phl/hal_g6
  -I platform`（W0-B .cmd 同款 4 目录）+ adapter 层另带 CSH_CFLAGS。
- seam 第一方桩（wlan8851_seam.c，ioctl_cfg80211.c 不编而 core 无门引用）：
  `cfg80211_ft_event`（ioctl_cfg80211.h:457 宏映射落点）/`rtw_cfg80211_get_is_
  roch`/`rtw_cfg80211_indicate_sta_assoc`/`rtw_cfg80211_rx_p2p_action_public`/
  `rtw_cfg80211_rx_rrm_action`/`rtw_cfg80211_ch_switch_notify`（no-op/false）
  + `core_br_client_tx`（BR_EXT 裁、V5 调用点无门 → SUCCESS 直通，[S5a]
  W0L-M3 血统）。
- V5 与 8821 seam 差异注记：8821 seam 的 PROC_DEBUG-off 五桩（rtw_sta_linking_
  test_* 族）**不需要**——V5 的 core/rtw_debug.c:529-537 无条件定义之。

## 重放（构建期，Makefile `modules` target 同款）

```sh
git -C third-party/RTL8851bu apply --check "$PWD/patches/RTL8851bu/<p>.patch" \
  && git -C third-party/RTL8851bu apply "$PWD/patches/RTL8851bu/<p>.patch"
```

物化态（应用后的工作树）**不入 submodule 提交**；0001-0003 重放出与生成时
工作树逐字节相同的差异（W1 轮 replay 幂等已验：apply → diff 逐字节同 →
checkout 复原）；0004 生成即验 `apply --check --reverse` 通过（与物化态精确
一致，W3 轮）。
