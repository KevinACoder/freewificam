# components/tlsf — vendored 来源记录

| 项 | 值 |
|---|---|
| 文件 | `tlsf.c`（1289 行）+ `tlsf.h`（90 行） |
| 上游 | tlsf.baisoku.org，TLSF v3.1 package（2016-04-10）；作者 Matthew Conte，基于 Miguel Masmano 的规范文档 |
| 许可 | BSD-2（tlsf.h 版权头全文声明；"no GPL restrictions apply" 条款在头注释） |
| 引入方式 | vendored 直拷（无规范 git 上游：官方包与 mattconte GitHub mirror 的每个提交都不一致，沿用前作同款裁决，[IMP]:20） |
| 获取日期 | 2026-09-30，从前作 `project_web_camera/freewebcamera/third-party/tlsf/` 字节级拷贝（md5 `6caed2d3…`/`4fe7a189…` 双侧一致） |
| 修改声明 | **无修改**。任何本地改动须在此登记并说明理由 |
| 依赖 | stddef/limits/assert 等.freestanding 头；assert 路径由消费方提供 `__assert_func`（本仓：os/threadx/heap.c） |
| 消费方 | os/threadx/heap.c（pvPortMalloc/vPortFree 系统分配面，线程栈来源） |

引入理由：SPEC `specs/threadx-up-boot`（S3）FR-6——ThreadX 无集成堆，线程栈与
后续组件的分配面落在镜像内 4M heap 区上的 tlsf。
