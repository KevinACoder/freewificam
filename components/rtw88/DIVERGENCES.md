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

### D-2 usb.c — rtw_usb_start 补 rx 队列 kill+重臂（W3）

- **文件:行**：`usb.c:839`（`rtw_usb_start` 原为空函数体；调用链 = ops->start
  → rtw_core_start → rtw_power_on → rtw_hci_start → 本函数）。
- **原文**：
  ```c
  static int rtw_usb_start(struct rtw_dev *rtwdev)
  {
  	return 0;
  }
  ```
- **改写**：`rtw_usb_cancel_rx_bufs(rtwusb)` + `RTW_USB_RXCB_NUM` 次
  `rtw_usb_rx_resubmit(..., GFP_KERNEL)`。
- **理由**：probe 末尾（rtw_usb_setup_rx）armed 的 rx urb 首笔 TD 跨芯片
  power-off/on 周期（efuse 读后芯片断电、ops->start 再上电）；本栈 xHCI 上
  该 TD 之后设备侧永不吐数据（板测 2026-10-04：usbh stall 显示 TD 完好挂在
  环上/端点 running/门铃已按，usbkpi rxq 恒 busy=1 queued=3，dump 零 ep84
  记录，桥 rx frames=0——360s 零完成），而 vendor 8821cu 线在芯片 init 之后
  才 arm rx urb、同栈收发正常。重臂使常驻 TD 与"芯片已运行"同代；纯新增，
  不改任何既有语义。
- **审计**：`git diff components/rtw88` 此轮应仅命中本 hunk。

（W1 编译面的一切适配落在 components/linuxkpi/ 80211 shim 增量：
mac80211.h 补 7.3 MLO 面（ieee80211_link_sta / link_sta_rc_update /
AMPDU_TX_START_IMMEDIATE）、iopoll.h 补 read_poll_timeout(_atomic) 7.x 签名、
asm/byteorder.h 新建、mac80211.h 内核词表 abs。rtw88 20 TU 逐字。）
