# third-party/rtw88 — submodule 来源记录

| 项 | 值 |
|---|---|
| 上游 URL | `git@github.com:KevinACoder/rtw88.git`（分支 `linux-7.3-rc2`） |
| pin | `b057bfd807f3341a3289e22f345503089ebd554f`（快照提交；= lab linux-upstream `e71d9a28760f` mainline 7.3-rc2 的 `drivers/net/wireless/realtek/rtw88/` 全目录 verbatim，2026-10-05 推送） |
| 许可 | 每文件 `GPL-2.0 OR BSD-3-Clause` 双许可，本仓**取 BSD-3-Clause 选项**（FreeBSD contrib/dev/rtw88 同例；前作先例 [FWCI] D-LK3）——trunk 合入底线 |
| 引入方式 | git submodule + pin 锁定（分支仅作人读标注，gitlink 是 SHA）；本地差异走 `patches/rtw88/` 重放（`make modules`），禁改 submodule 工作树 |
| 获取日期 | 2026-10-04（vendored 初引入，S9-W1）；2026-10-05 转 submodule（重构轮 feat/refactor-layout-r1） |
| 本地差异 | 见 `patches/rtw88/`（D-1 去 static 永久；D-2 rx 重臂；D-3 取证组结案后撤）；台账 `patches/rtw88/DIVERGENCES.md` |
| 实际编译集 | 顶层 Makefile `RTW88_SRCS` 锁定 21 TU（rtw88_core-y 16 件 + usb.c + 8821c 三件 + fw blob TU）；快照分支其余 77 文件不编译 |
| 消费方 | `third-party/ports/rtw88/`（modeswitch/class 钩子/entry/cmds/wpa 后端/netif 桥）+ `third-party/ports/rtw88/fw_rtw8821c.c`（生成物） |

## 引入理由

GT 载体一致性：[S9-GT] 板证全在 in-tree 7.3-rc2 上（选 in-tree 而非
lwfinger dist）。上游仓 master（a56bcd2）是 downstream 仓（同步
wireless-next，含 8814AU 等扩展），与本基线 27/60 编译文件不同源，
2026-10-05 用户裁决推快照分支 `linux-7.3-rc2` 作 pin 基底、master 不动。
wow.o/led.o 编译关断（CONFIG_PM/CONFIG_RTW88_LEDS，S9 N5/Q7），但快照
保留上游全目录（verbatim 定义 = 上游目录原样，编译选集由 Makefile 锁定）。
