# components/rtw88 — DIVERGENCES（偏差台账）

S9 spec FR-2（v1.3 偏差台账制）：rtw88 允许源改动，但每处必须先记本表
（文件:行 + 原文 + 改写 + 理由）；每轮纪检 = 对 [LNX] `e71d9a28760f`
基线 diff，输出与本表条目一一对应、无未记账改动。

## 当前条目

### D-1 rtw8821cu.c — usb_driver 对象去 static（W2）

- **文件:行**：`rtw8821cu.c:52` 附近（`static struct usb_driver rtw_8821cu_driver` →
  `struct usb_driver rtw_8821cu_driver` + 就地注释）。
- **原文**：`static struct usb_driver rtw_8821cu_driver = { ... };`
- **改写**：去掉 `static`；`module_usb_driver()` 展开保持原样（其填充的
  `.lkpi_module` 段在本树链接脚本中无 KEEP/走查）。
- **理由**：本树注册纪律 = 显式 `usb_register(&drv)`（app main 在
  `usbh_platform_start()` 前调 `rtw88_rtos_register()`，udisk/hub 同款）；
  导出驱动对象是显式注册的最小改动面。
- **审计**：`git diff components/rtw88 | grep '^[+-][^+-]'` 应仅命中此一处。

（W1 编译面的一切适配落在 components/linuxkpi/ 80211 shim 增量：
mac80211.h 补 7.3 MLO 面（ieee80211_link_sta / link_sta_rc_update /
AMPDU_TX_START_IMMEDIATE）、iopoll.h 补 read_poll_timeout(_atomic) 7.x 签名、
asm/byteorder.h 新建、mac80211.h 内核词表 abs。rtw88 20 TU 逐字。）
