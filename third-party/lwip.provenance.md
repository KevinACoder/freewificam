# third-party/lwip — submodule 来源记录

| 项 | 值 |
|---|---|
| 上游 | https://github.com/lwip-tcpip/lwip.git |
| pin | `77dcd25a72509eb83f72b033d219b1d40cd8eb95`（= `STABLE-2_2_1_RELEASE`，`git describe` 实测） |
| 许可 | BSD 风格（submodule 根 `COPYING`，Swedish Institute of Computer Science） |
| 引入方式 | git submodule（`git submodule add` + checkout pin，gitlink 锁定） |
| 获取日期 | 2026-10-02（P7-1a 轮，specs/lwip-adapt/SPEC.md FR-1） |
| 本地差异 | **无（预计零 patch）**。如编译期确需差异，走 `patches/lwip/` 重放（模型照 third-party/ports/patches/cherryusb/README.md），禁止直接改 submodule 工作树 |
| 实际编译集 | 阶段一（P7-1a，NO_SYS=1）：core 16 文件 + core/ipv4 7 文件（含 dhcp/acd，LWIP_DHCP=1）+ src/netif/ethernet.c + src/apps/lwiperf/lwiperf.c；api/ 与 dns.c 不编（P7-2a 随 NO_SYS=0 加入）。编译面锁死为 Makefile LWIP_SRCS 清单，每轮验收做产物反向检查 |
| contrib/ | 上游仓库内嵌套内容，**只读参考不编译**（ports/freertos/sys_arch.c 是 P7-2a sys_arch 的契约参照） |
| 消费方 | `os/lwip/` 第一方适配层（lwipopts.h / arch/cc.h / lwip_diag.c）+ `app/lwip_lan/`；lwip 头不出 os/lwip 与 app 消费面（specs/lwip-adapt FR-2） |

引入理由：SPEC `specs/lwip-adapt`（S7）FR-1——pin 与前作两条 lwip 线（wlan/GMAC
M1-A）同一 commit，三方哈希可对拍；前作 lwipopts/cc.h 教训值随 `os/lwip/` 适配层
搬运（搬运≠vendored：lwip 本体零 patch 原样编译 `-w` frozen import）。
