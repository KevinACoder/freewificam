# components/rtw88 — DIVERGENCES（偏差台账）

S9 spec FR-2（v1.3 偏差台账制）：rtw88 允许源改动，但每处必须先记本表
（文件:行 + 原文 + 改写 + 理由）；每轮纪检 = 对 [LNX] `e71d9a28760f`
基线 diff，输出与本表条目一一对应、无未记账改动。

## 当前条目

（零条目——W1 编译面的一切适配落在 components/linuxkpi/ 80211 shim 增量：
mac80211.h 补 7.3 MLO 面（ieee80211_link_sta / link_sta_rc_update /
AMPDU_TX_START_IMMEDIATE）、iopoll.h 补 read_poll_timeout(_atomic) 7.x 签名、
asm/byteorder.h 新建、mac80211.h 内核词表 abs。rtw88 20 TU 逐字。）
