# third-party/threadx — submodule 来源记录

| 项 | 值 |
|---|---|
| 上游 | https://github.com/eclipse-threadx/threadx （azure-rtos 旧地址重定向至此） |
| pin | `44d7c95c582d415c4ad84527180b29c93c3bf664`（= `v6.5.1.202602a_rel` + 一笔 SECURITY.md，前作记 `v6.5.1.202602a_rel-1`） |
| 许可 | MIT（submodule 根 `LICENSE.txt`） |
| 引入方式 | git submodule（`git submodule add`，gitlink 锁定上述 pin） |
| 获取日期 | 2026-09-30（`git ls-remote` 实测上游 HEAD 恰为该 pin，直接克隆即锁定） |
| 本地差异 | **无（预计零 patch）**。前作对该 pin 逐字节原样编译（[IMP]:17 "Byte-identical to the pin"）；如必须改 kernel 源，走 `third-party/ports/patches/threadx/` 重放（见该目录 README），禁止直接改 submodule 工作树 |
| 实际编译集 | `common/src/*.c`（185 个 TU）+ `ports/cortex_a55/gnu/src/*.S`（UP 单核 port；fp_enable/fp_disable 两个 .c 不编，`-mgeneral-regs-only` 无 FP 状态） |
| 消费方 | `os/threadx/` 适配层（tx_glue.c / tx_vectors.S / tx_up_shim.c / tx_user.h），tx 头不出适配层（FR-3 分层不变量） |

引入理由：SPEC `specs/threadx-up-boot`（S3）FR-1/FR-2——第一个 RTOS 内核上板，
pin 与前作 RK3568 ThreadX UP 主线一致以复用其 board-proven 事实。
