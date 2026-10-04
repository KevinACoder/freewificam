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

### D-3 取证打印组 — c2h/txrpt/ops_tx/bss_info 回读（W4 板测临时，收口后撤）

- **文件**：`fw.c`（rtw_fw_c2h_cmd_handle 入口）、`tx.c`（rtw_tx_report_handle
  sn/st 解析后）、`mac80211.c`（rtw_ops_tx mgmt 分支、rtw_ops_bss_info_changed
  入口回读、rtw_ops_sw_scan_complete 进出、rtw_ops_mgd_prepare_tx 进出、
  rtw_ops_add_interface 尾部回读）。
- **改写**：各加 1-2 行 `printf("rtw88u: ...")`，无任何行为改动（prepare_tx/
  scan_complete 仅打印；bss_info 的回读读寄存器只读不写）。
- **理由**：W4 定位"auth 响应不达桥"的板上取证面：C2H class 流水、TX report
  的 ACK 结果（st==0=ACKed）、mgmt TX 时的芯片信道视图、RX 过滤链读回
  （MACID/RCR/RXFLTMAP/BSSID regs）。mac80211.h 侧同期修正
  `mgd_prepare_tx` 原型为 7.3 `ieee80211_prep_tx_info*` 形（见 linuxkpi 侧
  注释；shim 是 first-party 不入本表）。
- **撤除条件**：单播 RX 断链结案后整组撤除，本表条目同步销。
