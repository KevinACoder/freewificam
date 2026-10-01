# PROVENANCE — components/linuxkpi

- **来源**: 前作实验线 `~/workspace/project_web_camera/fwc-net80211-lkpi/port/adapters/linuxkpi/`
  （[EXPL]，spec specs/linuxkpi/SPEC.md §0/§3.1；本仓第一方代码，BSD-2，
  clean-room 成文——只依 [LNX] 真 Linux 头与协议事实，无上游内核代码拷贝）。
- **搬运轮**: 2026-10-01，P6-a（feat/linuxkpi-p6a），搬运清单与行数见 spec §3.1。
- **许可证**: BSD-2-Clause（文件头随行）；fw 数组（未来 wlan 线）另有 PROVENANCE 与
  Realtek 许可注记，本目录的 fw_registry.c 只有机制、零 blob。

## 本轮适配（[EXPL] → 新基座，spec §3.9 OSAL 落点）

| 件 | 适配 |
|---|---|
| linuxkpi_impl.h | **重写**：NetBSD compat（sys/mutex.h 等）→ cmsis2 osMutex + TX 信号量/TX 定时器；storage 尺寸 static_assert 重钉 |
| linuxkpi_core.c | OSAL 半边重写：kmem→tlsf（heap.h pvPortMalloc，去 size header）、printk→board_log（vsnprintf 静态缓冲）、tsleep/wakeup→tx_thread_sleep/信号量、callout→TX_TIMER、atomics→__atomic 内建；completion 粘滞语义（5f43157 教训）原样保留；HZ 100→1000（板上 tick 1kHz） |
| linuxkpi_core.h | 原样搬运 + GFP_NOIO/MAX_SCHEDULE_TIMEOUT（§3.6-7）+ HZ 注记；其余驱动可见面零改动 |
| linuxkpi_usb.c | 搬运 + **缺口补齐**：usb_submit_urb 控制类 URB（setup_packet 64B stage + ep0 路径，§3.6-1）、usb_maxpacket（§3.6-2）；usb_reset_device 改显式 -EOPNOTSUPP（§3.5 行 8 定案）；裁掉旧 pin 专属件（0003 data_toggle 载体——xHC 硬件管 toggle、0004 usbh_ehci_diag_dump）；驱动注册改显式 usb_register（去 .lkpi_module 段依赖）；printf→lkpi_printf |
| linuxkpi_usb.h | 去 lkpi_usb_init（注册显式化）+ 加 drain 线程句柄查询 |
| include/linux/*.h | 44 头裁剪迁移（N10：mmc.h 不迁）；include/linux/usb.h 补 urb 字段 setup_packet/interval/error_count（§3.6-9）+ usb_fill_control_urb 存 setup + usb_maxpacket 声明（签名对齐 [LNX] usb.h:2060） |
| fw_registry.c | **新写**（§3.6-6）：C 数组 + 裸名索引 + PROVENANCE，缺省空表（U 盘零 fw） |

## 板证教训随行（[FWCI]，全部保留在代码注释）

- complete_all 粘滞（`5f43157`，R-LK3 ops->start wedge）；complete()/complete_all()
  必须是真函数不是宏（urb->complete 标识符冲突）。
- usb_fill_* 只清公共字段不 memset 整 urb（M3 坑 3，_usbh_urb 被 wipe → -EINVAL）。
- 每 urb 完成走 done-list + drain 线程（IRQ 完成在被动上下文交付，spinlock=被动互斥
  模型的前提）。
- workqueue queued 旗标在持锁窗口内清除（2026-09-24 板证）；NULL func 先 dump 再 park
  （jump-to-0 取证）。
