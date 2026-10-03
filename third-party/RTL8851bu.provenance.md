# third-party/RTL8851bu — submodule 来源记录

| 项 | 值 |
|---|---|
| 上游 | https://gitcode.com/windyoung2020/RTL8851bu（gitcode 镜像重定向确认） |
| pin | `5b6408dd54fae36abe97114554e9883c38332e7f`（HEAD，"Explain how install with dkms"；PHL 架构 2024 树） |
| 许可 | **仓库根无 LICENSE/COPYING 文件**（W1 判据 2 注记义务）；源文件头为 Realtek GPL-2.0 标头体例（实以各 .c/.h 文件头为准）；fw blob（`phl/hal_g6/mac/fw_ax/rtl8851b/`，内嵌 C 数组）为 Realtek 固件，** redistribution 遵 Realtek 固件发布条款，本仓仅以 submodule 引用不复制** |
| 引入方式 | git submodule（`git submodule add` + checkout pin，gitlink 锁定） |
| 获取日期 | 2026-10-04（S5b W1 轮，specs/rtl8851bu-vendor-driver/SPEC.md §3.1 既定 pin） |
| 先行验证 | W0-B Linux 真值轮（runs/20261004-rtl8851bu-w0b-linux/）：工作副本 `~/workspace/rk3568_lab/os/rtl8851bu-v5` 分支 `linux-7.3` @ `b0928db`（= pin + 8 提交内核 7.3-rc2 兼容，546 行 compat patch），fork 推送待用户执行；**linux-7.3 兼容分支是 Linux lane 专属，RTOS 侧不基于它**（断层由 linuxkpi 头面呈现，见 patches/rtl8851bu/README.md） |
| 本地差异 | 仅 `patches/rtl8851bu/` 重放（模型照 patches/rtl8821cu/README.md），禁止直接改 submodule 工作树 |
| 板证事实 | USB ID `0bda:b851`（无 modeswitch，直枚举；三接口复合设备含 BT）；fw 内嵌 v1.19.10-78-20240422（W0-B 真值）；模块名 `8851bu`（MODULE_NAME，rmmod 用名） |
| 消费方 | `port/adapters/rtl8851bu/` 薄层 + `components/linuxkpi`（V5 增量面）+ `app/wlan8851/`；vendor 头不出 port/adapters/rtl8851bu 与 os/ 消费面（spec FR-2 同款） |
| 分支纪律 | 消费分支 `feat/wlan-8851bu*`（自 `feat/wlan-8821cu` @ `08c1be7` 派生，用户 2026-10-04 裁决），**永不合并 trunk**（spec §4 分支条款） |

引入理由：S5b 第二条 vendor 先验线（specs/rtl8851bu-vendor-driver/）——验证 S5a
适配方法论可复制性（换芯片只动驱动侧）+ 硬件备选 + rtw89 先行情报轴（K-R）载体。
pin 与 W0-B 板证树（linux-7.3 分支基底）同一 commit 5b6408d，三方（上游 gitcode /
本 submodule / W0-B 工作副本）哈希可对拍。
