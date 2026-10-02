# components/iperf3_embedded — vendored 来源记录（S7b，specs/netutils-adapt §3.3）

| 项 | 值 |
|---|---|
| 上游 | https://github.com/sqqdfny/iperf3_embedded.git |
| pin | `66614e4`（2026-06-17 first commit 世代；参考 = 前作 vendored 树 ~/workspace/project_web_camera/freewebcamera/third-party/iperf3_embedded） |
| 许可 | **MIT**（本目录 LICENSE） |
| 引入方式 | vendored（2 文件全树引入：iperf3_embedded.c/.h） |
| 获取日期 | 2026-10-02（N1 轮） |
| 自述 | "Standard iperf3 protocol compatible (esnet/iperf3). Client only — TCP/UDP, forward/reverse, bandwidth limiting"（.c:21-25）；**server 角色上游未实现**（N8 非目标） |
| 登记偏差（随 PROVENANCE 平移） | ① FreeRTOS include 块换 `iperf3_port.h`（os/netutils/，CMSIS 映射）；② `<errno.h>` 移除——freestanding 无 newlib reent，errno 走 lwip/errno.h（LWIP_PROVIDE_ERRNO，变量在 os/lwip/lwip_diag.c 定义一次）；③ N7（2026-10-02）`IPERF3_TCP_BUF_SIZE` 4096→32KB——TCP 发送一次 32KB 摊薄 socket/netconn 往返开销（R0 基线 17.25Mbit/s 时每调用开销主导上行），tcp_write 内部按 MSS 分段 |
| 对端 | 宿主 `iperf3.exe -s`（:5201；D:\Software\iperf3.19.1\，NOTICE 授权） |
