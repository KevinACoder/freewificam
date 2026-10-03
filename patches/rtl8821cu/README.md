# patches/rtl8821cu

Realtek RTL8821CU vendor 驱动上游差异的**唯一真值源**（submodule 工作树禁改；
差异只以 patch 形式存 here，`make modules` 构建期重放进 `third-party/rtl8821cu/`）。

## pin

- 上游 URL：<https://github.com/Yuan-W/rtl8821cu>（.gitmodules）
- pin = **`8df92c5ebae574da2c50ad5f0e6b8107ae3b7fb0`**（2019-03-11 封版，
  "Add kernel 4.19 and 4.20 support"，全树 3 commits，GPL-2.0，551 c/h /
  590,585 行）。与 [VND] 参考树（~/workspace/reference/rtl8821cu）、
  前作 [FWC] submodule（project_web_camera/fwc-rtl8821cu）三方同一 commit
  （2026-09-30 逐一对拍，spec [S5a] §3.1）。
- 消费轮：S5a W1（feat/wlan-8821cu-r1，2026-10-02）；先行验证轮 W0-L 在
  ~/workspace/rk3568_lab/os/rtl8821cu 工作副本上以 Linux 7.3-rc2 板证
  （runs/20261002-w0l-rtl8821cu-linux/，751 行兼容 patch 台账）。

## patches

| patch | 内容 |
|---|---|
| `0001-rtos-trim-macros.patch` | **裁剪宏**（include/autoconf.h，[FWC] 0001 非平台项 + v2 增项）：`CONFIG_USB_TX/RX_AGGREGATION` off（RX=固定 recv-buf 重排队、TX=bulk-OUT 语义，聚合是数据面稳住后的二段开关）、`DBG 1→0`、`CONFIG_PROC_DEBUG` off（RTOS 无 procfs 垫= [S6] C 档；与 W0-L REV 相反——W0-L 编 rtw_proc.c 故保 ON，RTOS 不编 rtw_proc.c；rtw_sta_linking_test_*/rtw_get_wait_hiq_empty_ms 等 PROC 门内定义、门外引用的 debug 助手由 wlan8821_seam.c 第一方桩保链接，行为=默认态）、`DBG_MEM_ALLOC` off。**保持 `PLATFORM_LINUX` 定义**（[S5a] FR-2：osdep LINUX 分支照常编译；[FWC] 换 PLATFORM_THREADX 的 hunk 作废不继承）。**AP_MODE/P2P 不裁（W1 对账修正）**：spec §3.2.3 曾列裁剪，但 vendor 头面把 mlme_priv 的 olbc/olbc_ht 等 AP 字段门控在 CONFIG_AP_MODE 内而 rtw_cmd.c 无守卫引用——上游从未分开编过（[FWC]/W0-L 均未关）；形态=编译在场、运行时休眠（MLME_IS_AP 恒假）。 |
| `0002-rtos-export-shells-and-stubs.patch` | **导出壳 + 链接桩**（[FWC] 0002 血统）：①usb_intf.c 末尾导出 `rtl8821cu_rtos_init/_deinit` 包 `rtw_drv_entry/_halt`——module_init/module_exit 被 linuxkpi no-op 化，adapter attach 路径显式调用，drv_registered 处理与 Linux 模块加载路径逐字节同路；②os_intfs.c 顶部 `rtw_ioctl` 3 行 `-EOPNOTSUPP` 桩——ioctl_linux.c 不编，net_device_ops 表的门内引用（CONFIG_AP_MODE 族）万一存活时保链接（[FWC] 形态该缺口从未走通过链接，W1 是第一次真链接）；③core/rtw_mem.c 末尾 `rtl8821cu_mem_init_rtos/_deinit_rtos` 包 `rtw_mem_init/_exit`——prealloc-skb 池播种（module_init no-op 后由 attach 路径代播）。 |
| `0003-rtos-consumer-compat.patch` | **W0-L 派生消费适配**：①usb_intf.c drvwrap 版本梯子摊平为 `.usbdrv.driver.shutdown`（W0L-S8 板证形态；linuxkpi `struct usb_driver` 带 `struct device_driver driver` 成员承接）；②include/ieee80211.h 三个 mac 判断函数 `extern __inline`→`static __inline`（W0L-S10：gnu11 下 extern __inline 每 TU 强定义，多 TU 单链接必炸——W0-L 在 7.3 宿主同款修法）。 |
| `0004-rtos-sctx-completion-deinit.patch` | 释放 halmac 事件前注销 RTOS completion，避免 created-object 链悬挂（W3 板证）。 |
| `0005-rtos-cmdxmitframe-memset-clamp.patch` | 限制对齐后 cmd xmitbuf 的清零长度，避免覆写相邻 URB（W4 板证）。 |
| `0006-rtos-rx-lifecycle-and-w7-logs.patch` | 物化 W4/W5 的 RX 生命周期修复与计数探针、W7 的 RF FAIL 日志等级和默认降噪；从当前板证工作树相对 0001-0005 的差异生成。 |
| `0007-rtos-rx-last-timestamps.patch` | 手动状态读出最近 vendor 完成、成功完成、解析帧、指示帧的 tick；修复 WARNING 档吞掉手动 RXDBG 状态行，并将超长状态拆为两行以免串口截断。无后台轮询或自动恢复。 |
| `0008-rtos-rx-rearm-unknown-status.patch` | **[W11-C2] completion default 分支补重装**：未知 urb status（本栈实为 `-ETIMEDOUT/-ENOMEM/-EIO`，`lkpi_usb_errno` 对 cherryusb errorcode 的兜底映射）原样只计数不回收不重装 → 每次发生永久少一支 bulk-IN recvbuf，8 支耗尽 = "整条 RX 含完成一起冻结"停摆形态（issues/2026-10-03-rtl8821cu-rx-queue-stall.md 形态②）。修复 = 与 `-EPROTO` 族同款 `rtw_read_port` 重装；既有 continual_io_error 看护（switch 之前对一切错误 status 生效）兜底持续重装失败 → surprise removed。`-EINPROGRESS` 在本栈无写入点（死分支），不动。 |

> W0-L 兼容 patch（751 行）的其余条目**不搬运**：断层按「linuxkpi 头面补优先」
> 落进 components/linuxkpi 增量档（B1-B8，见 [S6] §3.4 附表与
> runs/20261002-w1-rtl8821cu-intro/RUN.md 对账表）——S1/S9（set_fs/strncpy）
> 由 linuxkpi 头面呈现 4.20 形态 API 保 vendor 逐字；S3（route_dump iov_iter）
> 实证是死代码（WOWLAN=n → ARP_KEEP_ALIVE 未定义，`nm os_intfs.o` 无符号）；
> S5/S7（NAPI/sha256）随宏裁剪与无撞名环境自然消失；S6（timer 双形态）linuxkpi
> 两个名字都已提供。

## 重放（构建期，Makefile `modules` target 同款）

```sh
git -C third-party/rtl8821cu apply --check "$PWD/patches/rtl8821cu/<p>.patch" \
  && git -C third-party/rtl8821cu apply "$PWD/patches/rtl8821cu/<p>.patch"
```

物化态（应用后的工作树）**不入 submodule 提交**；当前 0001-0008
应重放出与 `third-party/rtl8821cu/` 工作树逐字节相同的差异。
重放幂等两连验（apply → stat 逐字节同 → checkout 复原）。

## 0004-rtos-sctx-completion-deinit（2026-10-03，W3 板验轮）

hal_halmac.c `free_halmac_event` 在 `rtw_mfree(sctx)` 前补
`_rtw_deinit_completion(&sctx->done)`。原因：`rtw_sctx_init` 经
`init_completion` 在 RTOS 侧注册全局 TX 信号量（created-object 链），
Linux 里 completion 只是普通结构体、kfree 无需配对；RTOS 里 free 不
deinit 等于「释放不摘链」，僵尸节点内存被复用后，下一次
`tx_semaphore_create` 的去重遍历即 data abort（W3 板测：
`cv new node=a563280` 于 DUMP_LOGICAL_EFUSE feature 后无配对 delete，
tx_fault_park far=0x200020205207，链快照 poison 于该节点）。映射宏
`_rtw_deinit_completion` 在 components/linuxkpi/linuxkpi_core.h。

## 0005-rtos-cmdxmitframe-memset-clamp（2026-10-03，W4 板验轮）

`core/rtw_xmit.c __rtw_alloc_cmdxmitframe` 的 `_rtw_memset(buf_addr, 0,
alloc_sz)` 越界清零修复：`buf_addr = pbuf` 是分配块内部的 512B 对齐点，
`alloc_sz`(=MAX_CMDBUF_SZ+XMITBUF_ALIGN_SZ=5632) 字节从 pbuf 起会写出
块尾 `(pbuf - pallocated_buf)` 字节（0..511 视堆对齐）。Linux 上 >4KiB 的
kmalloc 来自页分配器（页对齐，slack 恒 0）故上游从不暴露；RTOS tlsf 堆块
16B 对齐，越界写淹到紧邻分配——本板堆把 cmd xmitbuf 自己的 `pxmit_urb[0]`
（440B urb，KPI `usb_alloc_urb` 分配）放在 payload 之后，up 期首个 cmd 帧
（rsvd-page 固件包）把 urb 头 176B 清零：`_usbh_urb` 链接变 NULL →
usbh_submit_urb 分发器 -USB_ERR_INVAL(-2) → 线上零活动 + Bcn_Valid 超时
（W3 open 阻塞根因；早期 KPI 错误码映射经 NULL 链接读出 0 时伪装成
"假成功"）。板证：up 前后 `usbkpi peek` 对拍（up 前 `_usbh_urb=0xa5caea0`
完好 → up FAIL 后为 0）+ 分配账本（n=326 该地址确为 KPI urb）+ 越界侧
`pbuf+8=0xa5c9808`（find 命中 txdesc）。修法 = 把清零长度夹到分配块尾
`alloc_sz - (pbuf - pallocated_buf)`（≥5121，覆盖 5120 可用窗口）。

## 编译配置归属（本 README 即记账）

vendor 源零修改口径下的构建宏（freewebcam Makefile `RTL8821CU_VENDOR_CFG`，
对表 W0-L 板证命令行 minus kbuild 内建宏）：`-DCONFIG_RTL8821C
-DCONFIG_MP_INCLUDED -DCONFIG_POWER_SAVING -DCONFIG_BT_COEXIST
-DCONFIG_EFUSE_CONFIG_FILE -DCONFIG_TRAFFIC_PROTECT
-DCONFIG_LOAD_PHY_PARA_FROM_FILE -DCONFIG_TXPWR_BY_RATE_EN=1
-DCONFIG_TXPWR_LIMIT_EN=0 -DCONFIG_RTW_ADAPTIVITY_EN=0
-DCONFIG_RTW_ADAPTIVITY_MODE=0 -DCONFIG_RTW_DEBUG -DCONFIG_LITTLE_ENDIAN
-DDM_ODM_SUPPORT_TYPE=0x04 -DRTW_LOG_LEVEL=$(CONFIG_RTW_LOG_LEVEL)`（W7 默认值在 configs/，
`lwip-wlan.config` 为 3）。
**不定义**：CONFIG_WOWLAN（ARP_KEEP_ALIVE/route_dump netlink 面随之消失）、
CONFIG_RTW_NAPI/GRO、CONFIG_BR_EXT、CONFIG_IOCTL_CFG80211、
CONFIG_WIRELESS_EXT、CONFIG_USB_SUSPEND（→AUTOSUSPEND）、
CONFIG_MP_VHT_HW_TX_MODE、CONFIG_MINIMAL_MEMORY_USAGE。
W0-L 差异注记：W0-L 板证时 kbuild 给了 `DM_ODM_SUPPORT_TYPE=0`（vendor
Makefile 默认），spec 裁决 RTOS 用 0x04（ODM_CE，fw_nic 分支）——行为面
差异在 W3 scan 板验时对表。
