# HANDOFF — STM32F429 (MCU) + RTL8188EUS，W0–W5 已完成，W6 数据面待攻

写于 2026-11-15 会话收尾（goal-blocked：会话上下文耗尽）。这份文档是**新会话的入口**，
先读它，再按"下一轮实验"开跑。所有判词按 `AGENTS.md` §0/§5 的三档纪律：
只写 `runs/` 里有证据的，假设处标"待验证"。

## 1. 一句话状态

`components/liblinux80211` 的**真栈**（mac80211/cfg80211 + linuxkpi + CherryUSB URB 引擎）
+ 驱动 `rtl8xxxu` 已在 STM32F429 上跑到 **W5 关联 3/3 PASS**；**W6 数据面未通**：
RTL8188EUS 只把第一帧数据（广播 ARP）发上空口，之后所有**单播**数据帧被芯片静默丢弃
（ICMP `rx=0`），因此 TCP/UDP 吞吐**一个数都没测、没声称**。

## 2. 已完成的门（都有 `runs/` 里的命令+串口原文+镜像指纹）

| 门 | profile / 镜像 | 证据 |
|---|---|---|
| W0 SDRAM bring-up + self-test | `stm32f429-sdram` | `runs/2026-11-15-f429-wifi-w0/boot7,boot8`（`sdram test ok size=33554432`） |
| W1 attach（probe/固件/wlan0/dev_open） | `stm32f429-wifi-c3` | `runs/2026-11-15-f429-wifi-w1/boot1,boot2` |
| W4 扫描到 `labwifi-ar-ht20s@2432` | `stm32f429-wifi-c4` | 各 w5 boot 的 `bss ssid=… bssid=1c:a7:70:de:29:a1 freq=2432` + 空口 probe request |
| W5 关联（auth+assoc，200 ms） | `stm32f429-wifi-c5`（679,024 B，sha256 `23fa5920e1468f96e5…`） | `runs/2026-11-15-f429-wifi-w5/boot27,boot28,boot29`；boot28 的 AP `iw dev wlan2 station dump` 见到 `20:f4:1b:52:8f:01` |
| W6 数据面 | `stm32f429-wifi-c6` / `-c6t` / `-c6diag` | **未通**：`runs/2026-11-15-f429-wifi-w6/boot1…boot19` |

## 3. W6 已确认的事实（按证据）

- 系统是**健康**的：fast heartbeat 行（无锁轮询 UART）显示 USB 台账一路增长
  （submits/completes 116→151、RX 完成 105→137、TX 完成 10→13），runner 在 ready/blocked
  正常交替，drain 线程在跑，lwIP core lock 从未被持有（`lock=-`）。见 boot19。
- **lwIP 在发包**，驱动 `fill_txdesc_v3` 被调用、USB 传输完成；但 AP 侧
  `iw station dump` 报 station `inactive time: 28964 ms`，ch5 MT7921 monitor 40 s
  只看到板子 **1 个 probe request + 1 个 data frame**：**空口侧只到第一帧数据**。
- 描述符 dump（`rtl8xxxu_diag.c` 的 `fill_txdesc_v3` wrap）：
  - 管理帧 `#1..#3`：probe `fc=0040`/broadcast、auth `fc=00b0`、assoc `fc=0000`，`q=18`(MGNT)、`rate=0`；
  - 数据帧 `#4/#5`：ARP，`fc=0188`、`txdw0=0x0000008d`（OWN|FIRST|LAST|BROADCAST）、`q=7`(VO)、`rate=19`、`macid=0`；
  - 之后的单播 echo 帧（`pkt size 70/118`，驱动自己的 `TX rate: 19` 行有）**不再出现在 wrap 的 dump 之外任何异常上**——即驱动/描述符层面正常，问题在芯片/固件侧。
- **已排除**：A-MPDU 聚合（`--wrap=ieee80211_start_tx_ba_session` 拒会话后干净 boot 无 `AMPDU_TX_START`，仍不通）；"硬卡死/中断风暴"（tick-PC 采样器显示 tick 连走 32 s、空闲任务被采样）；lwIP core lock 死锁（见上）；ISR 上下文的两个真缺陷已修（`osSemaphoreRelease` → `xSemaphoreGiveFromISR` 钩子；日志 sink 拒异常上下文）。
- **修正过的错误结论**（别再重复）：早前"数据路径绕过 `--wrap`"是串口日志轮转造成的假象，已撤回；monitor 上"auth 没上空口"同样是仪器/时序问题，W5 的实际判据是 AP station dump。

## 4. W6 最可疑的机制（待验证，按可能性排序）

1. **H2C media-status 没生效**：`rtl8xxxu_gen2_report_connect()` 的注释原文
   "The firmware turns on the rate control when it knows it's connected to a network."
   现象正好符合：广播帧（ARP，不走 macid 速率表）能发，**单播**帧全被丢。
2. **FS 2-OUT-EP 的队列/端点映射**：F429 全速枚举出 2 个 bulk OUT（EP2/EP3），
   `rtl8xxxu_init_queue_priority` 的 `ep_tx_count=2` 映射下 VO(q=7) 与管理都落在 EP2；
   RK3568 高速 4 EP 的映射不同。可能芯片按队列/端点期望不一致而丢 VO 队列帧。
3. **芯片 TX page / C2H report 释放路径**：8188EU 的 TX 页由 C2H 报告释放，
   C2H 流没被消费时 TX 缓冲会在第一帧后填满（驱动 `has_tx_report=1`，C2H 走 RX 路径）。

## 5. 下一轮实验（按顺序，每个都是"一次 boot 定性"）

1. **H2C media-status（首选）**：在 `wireless-drivers/rtl8xxxu/ports/rtos/rtl8xxxu_diag.c`
   加 `-Wl,--wrap=rtl8xxxu_gen2_report_connect`（导出符号，wrap 有效），打印
   `macid/role/connect`；关联后从 diag 单元读回 8188e 的 H2C 邮箱寄存器
   （`REG_HMTFR`/C2H evt，见 `wireless-drivers/rtl8xxxu/regs.h`）与 media-status 位。
   若调用发生了且邮箱被消费 → 走第 2/3 条；若无调用/写失败 → 修 H2C 路径。
2. **队列/端点 A/B**：把第二帧数据强行送 MAN 队列（q=18 路径已证明能上空口）：
   最省事的做法是首方 wrap `rtl8xxxu_queue_select`（静态，不可 wrap）→ 改为在 case 侧
   用 `lkpi_netdev_xmit` 直接发一帧手搓以太网帧并观察空口；或对比 `skb->priority`
   映射到 BE(q=0)→EP3 的行为。
3. **TX page/C2H**：diag 单元读 `REG_TXPKT_EMPTY`/页寄存器（如果 regs.h 有），
   统计驱动 `c2hcmd_work` 是否在跑。
4. **交叉对照（见 §6）**：把同一颗 8188EU 换到 RK3568 上跑它的 C5/C6，抓同一组
   描述符/H2C/空口证据做 A/B；这是区分"MCU 端口问题"与"芯片/固件状态问题"的最快路径。

## 6. RK3568 对照臂（用户提的 8723BU 思路）

RK3568 上已有 **同一驱动 `rtl8xxxu`** 的两条可用参照线（都在这条仓的历史里）：
- `rk3568-rtl8xxxu-8188eu-*`：**同驱动 + 同芯片**，C4/C5/C6T 都过了（见 `docs/STATUS.md`、`runs/2026-11-14-rtl8xxxu-8188eu-sta/`）。这是最强对照：同芯片在 RK 上能收能发。
- `rk3568-rtl8723bu-c3…c6`：同驱动、**不同芯片**（8723BU，固件 `rtlwifi/rtl8723bu_nic.bin` 35.0/0x5301；串口识别见 `docs/CONSOLE-EVIDENCE.md` 的 RTL8188EUS/8723BU 一节）。可作为"驱动数据路径正常"的第二参照，并用来对比 8723BU 与 8188EU 在关联后的 H2C/描述符序列差异。

建议的对照方法（同场景、同控制变量）：
1. RK3568 + 8188EU 跑 C5（或 C6），在 case 里把 `rtl8xxxu_debug` 打开到
   CHANNEL|TX|USB（同 F429 的做法），抓：关联后的描述符字段（q/rate/macid/txdw0）、
   H2C media-status 调用、首帧数据是否上空口（AP `station dump` + monitor）。
2. 同一颗 dongle 回到 F429 跑同一组记录，**逐字段 diff**：如果 RK 上单播数据能发而 F429 不能，
   且描述符字段一致，则问题在 F429 的 USB/URB 引擎或时序（全速 12 Mbit/s、2 EP）；
   如果 RK 上 H2C 有而 F429 没有，直接命中假设 1。
3. 8723BU 那条用来验证"驱动的关联后序列"是不是 8188EU 特有（8723BU 走 gen2 report_connect 吗？
   看 `wireless-drivers/rtl8xxxu/8723bu.c` 的 fops），从而判断 8188EU 的丢帧是否只与它的固件有关。

RK3568 板控一律用 `skills/rk3568-board`（上电/串口/TFTP/断电），不要手工敲 hub REST。
注意两颗 rtl8xxxu dongle 同场时的串口身份：固件行必须按 `request_firmware` 名字匹配
（`rtlwifi/rtl8188eufw.bin` 28.0/0x88e1 vs `rtlwifi/rtl8723bu_nic.bin` 35.0/0x5301），
不能取第一条 `Firmware revision`；MAC 也只用 case 自己打的 `mac=`/`bssid=`（`%pM` 不可用）。

## 7. F429 侧环境速查

- 板：野火挑战者 V2（IGT6，1 MB flash / 192 KB SRAM / 32 MB FMC SDRAM），
  OTG_HS 全速主机（PB14/PB15，J74–J76 在 HOS 侧）；AP：Kali Pi `labwifi-ar-ht20s`
  ch5/2432 HT20 open（`192.168.99.1`），板子静态 `192.168.99.2/24`。
- 硬件工具在仓外 `mculab`：`mcu flash <bin> --bin-base 0x08000000 --erase sector --reset`
  （**必须 BIN**，ELF >1 MB 会被拒）；串口 115200。
- **板子被卡死镜像占着时，默认 chip 擦除会报 `PROBE_ERROR ... expected DAP_LED / DAP_SWJ_SEQUENCE`**，
  改 `--erase sector` 一次就好（`mcu doctor` 仍会报 `target_ok: true`，别被误导）。
- 抓干净的单 boot 串口：先 `mcu serial status --json` 取 `next` 偏移，再
  `mcu serial expect --regex <锚点> --since <偏移>`，然后 `mcu serial tail` 读；
  串口日志文件跨 boot 累积，不做偏移会串台（本轮踩过）。
- 平台契约：任何链接 `liblinux80211` 的 MCU 镜像必须 `configUSE_TICK_HOOK 1` +
  `vApplicationTickHook` 调 `lkpi_tick_advance()`，否则 `jiffies=0`、MLME 定时器 1 ms
  钳位空转、`authenticated` 后 assoc 永不发（W5 的根因，已修）。

## 8. 代码与提交现状

- freewificam 仓（分支 `linux-dev`）最新提交：`2e7549c`（fast heartbeat URB 台账）；
  根仓最新：`360e9a1`（H2C 假设与下一实验）。两仓干净、**未推送**（按仓规约 agent 只提交）。
- 关键新文件：`freewificam/docs/F429-WIFI.md`（平台/天梯/坑/串口锚点）、
  `tests/board/rtos/app/cases/f429_wifi.c`（W1–W6 case，含端口契约探针与 W5/W6 seam）、
  `platform/board/stm32f429-challenger/fmc_sdram.c`、`platform/arch/armv7m/stm32f429.ld`（SDRAM 与组件注册表 KEEP）、
  `wireless-drivers/rtl8xxxu/ports/rtos/rtl8xxxu_diag.c`（描述符 dump/census）。
- 待验证假设一律不要写成结论；W6 未声称，吞吐未声称。W6 的 gate 设计是
  ICMP 20×56 + 20×1400 零丢包；吞吐只记录不设门（`CONFIG_F429_WIFI_TP=y` 的四个相位已备好）。

## 9. 停损（W6 再跑两轮没有新观测面就换策略）

- 若 H2C + 队列/端点 + TX page 三条都排除，而 RK3568 同芯片对照证明是 F429 端口问题，
  下一档策略是**换控制器路径**：把 8188EUS 接到 SPI/SDIO 适配板，或换另一颗 USB 主机
  （例如经 hub 或换 EHCI 板），把"全速 OTG_HS + CherryUSB 引擎"这个变量摘掉。
- 任何新结论必须落 `runs/<date>-<topic>/<boot>/` 四件套 + 更新 `docs/F429-WIFI.md` 与
  `CONSOLE-EVIDENCE.md`；不要把"编译过/链接过"写成"能传"。
