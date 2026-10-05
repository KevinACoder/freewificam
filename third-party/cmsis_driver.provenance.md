# third-party/CMSIS-Driver — submodule 来源记录

| 项 | 值 |
|---|---|
| 上游 URL | `git@github.com:ARM-software/CMSIS-Driver.git` |
| pin | `9f3b5427dae74c8a13b150a10d8adec51d5b52cd`（tag **2.9.0**，2026-10-05） |
| 许可 | Apache-2.0 |
| 引入方式 | git submodule + pin 锁定；全量检出（仓为驱动实现集合，体积小）；禁改 submodule 工作树 |
| 获取日期 | 2026-10-05（feat/refactor-layout-r1 S3 轮） |
| 本地差异 | 预期零 patch |
| 实际编译集 | 无（本轮**不编译**其中任何驱动） |
| 消费方 | 暂无——本仓是 CMSIS-Driver **驱动实现**的家（API 头在 CMSIS_6 仓 `CMSIS/Driver/Include/`）；components/ports/cmsis/ 承载本项目的 CMSIS-Driver 形态 port（usb_host gap-fill + cherryusb 后端） |

## 引入理由

用户裁决（2026-10-05）：CMSIS 家族以 CMSIS_6 + CMSIS-Driver 双 submodule
管控，不再散放。本轮 CMSIS-Driver 仓先占位管控（零消费），未来若把
usb_host gap-fill 对齐上游 Driver_USBH.h 或实现真正的 CMSIS-Driver
USB host 控制器驱动，实现落在本仓引用的 upstream 线或
components/ports/cmsis/ 二选一再议。
