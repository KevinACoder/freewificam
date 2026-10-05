# third-party/CMSIS_6 — submodule 来源记录

| 项 | 值 |
|---|---|
| 上游 URL | `git@github.com:ARM-software/CMSIS_6.git` |
| pin | `45dab712ad84f8cbbf2b7bfc089c19088507df6f`（tag **v6.3.0**，2026-10-05） |
| 许可 | Apache-2.0（根 LICENSE 随 sparse-checkout 保留） |
| 引入方式 | git submodule + pin 锁定 + **cone sparse-checkout 仅取 `CMSIS/RTOS2/Include`（cmsis_os2.h 供给）与 `CMSIS/Driver/Include`（Driver_*.h API 头供给）**，工作树 ~444KB；禁改 submodule 工作树（本项目零 patch 预期，无 patches/CMSIS_6/） |
| 获取日期 | 2026-10-05（feat/refactor-layout-r1 S3 轮） |
| 本地差异 | 预期零 patch |
| 实际编译集 | 无 TU（纯头文件供给）；cmsis_os2.h 全仓全局 -I，Driver_Common.h 经 USB_CFLAGS |
| 消费方 | os/cmsis2（RTOS2 实现）、os/lwip、components/ports/netutils、components/ports/net80211（curlwp）、components/ports/cherrysh、third-party/ports/cherryusb（usbh_msc）、third-party/ports/wpa_supplicant、third-party/ports/lwip、board 以太网驱动、app/main |

## 引入理由

顶替 vendored `components/cmsis5/cmsis_os2.h`（CMSIS_5 V2.3.0 单头拷贝）。
核验：submodule v6.3.0 的 `CMSIS/RTOS2/Include/cmsis_os2.h` 与原 cmsis5
现物**逐字节一致**（含 v6.3.0 tag 与 master tip 两点对拍），API 面零变化。
CMSIS-Driver 的 API 头（Driver_Common.h 等 18 件）也位于本仓
`CMSIS/Driver/Include/`——`ARM-software/CMSIS-Driver` 仓只放驱动实现，
不放 API 头。注意：上游已有 `Driver_USBH.h`（USB Host API），
components/ports/cmsis/usb/usb_host.h 的自研 gap-fill 注释（"官方无
host 控制器头"）已过时；本轮不换抽象（功能风险），未来对齐
Driver_USBH.h 单独立项。
