# components/netutils — vendored 来源记录（S7b/netutils，specs/netutils-adapt §3.1）

| 项 | 值 |
|---|---|
| 上游 | https://github.com/RT-Thread-packages/netutils.git |
| pin | `b77ba3f`（git describe = `1.3.3-13-gb77ba3f`；参考树 ~/workspace/reference/netutils 同 pin 可 diff -r 对账） |
| 许可 | **Apache-2.0**（包级，本目录 LICENSE）；上游 `ntp/ntp.h` 的 **GPL-2 头已随裁剪物理隔离在树外**（ntp 件不 vendor） |
| 引入方式 | vendored（非 submodule——shim 地基替换侵入性论证 [NOTICE]:32；升格对账：pin 已锁，随时 `git submodule add` + `diff -r`） |
| 获取日期 | 2026-10-02（N1 轮；经前作 vendored 树 ~/workspace/project_web_camera/freewebcamera/third-party/netutils 转载，板证修复随行） |
| 裁剪子集（6 文件） | ping/ping.c + tftp/{tftp.h, tftp_client.c, tftp_server.c, tftp_xfer.c, tftp_xfer.h}；netio/tcpdump/ntp/telnet/iperf(netutils 自带) 全不进（§3.2 逐件裁决） |
| 偏差表（前作板证，随行登记） | ① ping/ping.c：`cmd_ping` 支持 count 参数（`ping <host> <count>`，upstream 硬编码 4 忽略 argv[2]；前作 2026-09-26 板证）+ 两条诊断打印（sendto 短返带 errno / rt_malloc 失败点名，ping.c:118-121）；② tftp/tftp_xfer.c：`tftp_send_request` 去 RFC2347 blksize/tsize option（upstream 广告 option 不解析 OACK，对 option-honouring 服务器首包即断；oslab TFTP 即 OACK 型，前作 2026-09-25 板证）；③ tftp.h/tftp_client.c/tftp_server.c 零偏差 |
| 本仓编译形制 | frozen import `-w`；include 序 shim/ 最前（影子头必胜）；树内 SConscript/README 不导入 |

## 偏差 vs 上游 pin 的对账说明

升格 submodule 时 `diff -r` 预期命中：ping.c 两处修复、tftp_xfer.c 一处修复，
其余逐字节相同。任何超出本表的差异 = 违反 vendored 纪律。
