# components/cmsis5 — vendored 来源记录

| 项 | 值 |
|---|---|
| 文件 | `cmsis_os2.h`（本目录，882 行） |
| 上游 | Arm CMSIS 5（CMSIS/RTOS2/Include/cmsis_os2.h），https://github.com/ARM-software/CMSIS_5 |
| 版本 | V2.3.0（30. June 2023，文件头 `$Revision: V2.3.0` 自述） |
| 许可 | Apache-2.0（SPDX 头在文件首部；全文见本目录 `LICENSE.apache2`，取自 Ubuntu `common-licenses/Apache-2.0` 权威文本） |
| 引入方式 | 从前作 `project_web_camera/freewebcamera/include/cmsis_os2.h` 字节级拷贝（md5 `bbc42872d35f314f5f5afdc7bcbb8e88` 双侧一致，2026-09-30 验证）；前作同为 vendored 拷贝 |
| 修改声明 | **无修改**。任何本地改动须在此登记并说明理由 |
| 依赖 | 仅 C 标准头 stdint.h/stddef.h（freestanding 可用） |

引入理由：SPEC `specs/aarch64-baremetal-boot` §3.7.1（R4 CMSIS2 时基子集）——
os/cmsis2 shim 需要接口头，app 层只见该头（分层纪律：接口宁少勿多）。
