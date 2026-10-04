# components/net80211 — DIVERGENCES（偏差台账）

S8 spec FR-1：本地差异只允许机械改写并逐条记台账；验收 diff 对拍只允许命中
本表条目。

## D-1 ieee80211_netbsd.c — sysctl 配置树编译门

- **文件:行**：`net80211/ieee80211_netbsd.c:63` / `:74-76` / `:509-526`（相对
  基线新增三 hunk，共 +25 行）
- **原文**：无守卫——`ieee80211_sysctl_attach/detach/setup` 与巨型
  `sysctl_node` walker（`SYSCTLFN_ARGS` 展开依赖 NetBSD 真实
  `name/namelen/oldp/oldlenp` 形参）直接编入。
- **改写**：以 `#ifndef IEEE80211_PORT_NO_SYSCTL` 包住 sysctl 面，`#else` 分支
  提供 `ieee80211_sysctl_setup/attach/detach` 空实现；宏由
  `os/net80211/compat/net80211_prelude.h` 定义（S8 N8：无 ifconfig/SIOC*
  用户面，sysctl 树整体编译出局）。
- **理由**：无 BSD 用户land；compat 的最小 SYSCTLFN_PROTO 形参面撑不起
  sysctl_node walker；fwc 先例同款 patch（其树同 pin 对拍一致）。
- **来源**：与 fwc-net80211-lkpi `third-party/net80211/net80211/ieee80211_netbsd.c`
  逐字节一致（成形副本直接采用）。

## D-2 aes_ccm_mbuf.c — mbuf 链推进次序

- **文件:行**：`crypto/aes/aes_ccm_mbuf.c:49-51` 与 `:74-76`（两处相邻行互换）
- **原文**：`m = m->m_next;` 在本段处理之**前**推进（先跳后用）。
- **改写**：推进移到本段处理之**后**（先用后跳）。
- **理由**：本仓 mini-mbuf（`os/net80211/compat/net80211_mbuf.c`）的簇模型
  与前作相同——零长 mbuf 尾链场景下原次序会丢段；fwc 同款修正在其板上
  CCMP 数据面验证过。
- **来源**：与 fwc-net80211-lkpi 同文件逐字节一致。

## 台账纪律

- 新增偏差必须先落本表再改文件（commit 顺序保证）。
- 验收命令：对 `netbsd-11 @111525e242df` 逐文件 `cmp`，DIFF 输出与本表
  一一对应、无未记账改动（S8-1 判据 2；后续轮同口径复审）。
