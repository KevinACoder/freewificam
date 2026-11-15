# HANDOFF — STM32F429 + RTL8188EUS：下一轮做 AP 模式

本文件是给下一轮（AP 角色）的入口。上一轮的结论：F429 上 **W6（STA 数据面）
已通、四相位吞吐已测（report-only）、hub 多网卡已打通、relay 冷启动已可用**；
下一轮把 **8188EUS 变成 2.4 GHz AP**，判据镜像 RK3568 的 8188EU AP 臂
（`runs/2026-11-14-rtl8xxxu-8188eu-ap/`，A1–A3 2/2）。

## 1. 目标与判据（预注册，先照抄再看）

| 门 | 判据 | 见证 |
|---|---|---|
| A1 BSS 起来 + beacon 上空口 | `set_iface rc=0 type=3`、`start rc=0`、`link freq=2432`；板端 beacon 在空口按 100 TU 节拍出现 | 客户端（Pi MT7921AU STA）扫描到 BSS + `RTL8812AU 的 monitor 抓包`（见 §5：**不要用 MT7921AU 当 monitor**，它看不到 F429 的帧） |
| A2 对端关联 | Pi 的 MT7921AU STA 首试加入，`Connected to <板 MAC>`；板端 `sta n=1` | mac80211 的 AP station 表 `lkpi_nl80211_sta_count`（无 netlink 时的权威），对端 `iw link` |
| A3 双向数据面 | 56 B 与 1400 B ICMP 各 20/20，双向 0 丢包 | 板端 `rx_frames/tx_fail` 计数 + 对端 ping 输出 + AP 侧 `tcpdump -i wlan2 icmp`（44/44 型的账） |
| 验收 | **同一镜像 2/2 relay 冷启动**都过；两 boot 的 `image.sha256` 相同 | `runs/<date>-f429-wifi-ap/` |
| 出界（本轮不做） | A4 吞吐、WPA2、多客户端、5 GHz | 8188EU 是 2.4 GHz 1T1R；WPA2 要有单独预注册 |

## 2. 上一轮已证明的事实（可直接依赖）

- **STA 数据面**：`stm32f429-wifi-c6` 单 boot `PASS W6`（ICMP 20×56 + 20×1400
  各 20/20，AP 侧 44 请求/44 回包），census 镜像 2/2、交付镜像 1/2
  （第 2 boot 撞 nRST 不清 dongle 的坑，现已由 relay 冷启动解决）。
- **驱动树已带 AP 补丁**：`wireless-drivers/rtl8xxxu/patches/`
  `0001`（`supports_ap` + `max_macid_num`）、`0002`（v3 txdesc 的 station macid）、
  `0003`（**host 重发 beacon**，8188E 芯片不重发 host 下载的 beacon）。
  补丁由导入脚本重放，**F429 镜像自动带上**；`import-driver.py --check` 校验。
- **bench/平台**：relay（`192.168.0.44`）= F429 冷启动
  （`skills/rk3568-board/scripts/power.sh cycle --confirm`）；console 打印锁已修
  （armv7m 关中断，否则高优先级打印者会把整机饿死）；全速 USB 12 Mbit/s、
  32 MB SDRAM、1 MB flash（**当前 c6 镜像 726 KB，离上限只有 ~290 KB**）。
- 8188EUS：`0bda:8179`、MAC `20:f4:1b:52:8f:01`、FW `rtlwifi/rtl8188eufw.bin`
  28.0/0x88e1；FS 主机上 12 Mbit/s。

## 3. RK3568 的 8188EU AP 臂（镜子，先读）

`runs/2026-11-14-rtl8xxxu-8188eu-ap/RUN.md` + `PREREG.md`：

- **A1 的坑**：ap-1 里客户端靠 probe response 能连上，但 **beacon 只发 1 个**
  （monitor 抓包 0 个 beacon）。诊断 boot 读芯片：`BCN_VALID=1`、`EN_BCNQ_DL=1`、
  `BEACON_FUNCTION_ENABLE=1`，host 恰好组了 1 个 beacon；否证臂（清
  `REG_BCN_CTRL` bit4）仍 1 个。**修法 = patch 0003 host 重发**，之后 beacon
  get 计数随 A2/A3 涨到 59/255，monitor 抓到 144/141 个、间隔 ~102 ms。
- **A1 的见证**：`hostap start` 锚点前移 + 20 s 窗口；monitor 按 BSSID/SSID/
  channel/interval/DTIM 判。
- **A2/A3** 2/2；无 GT 分母（GT 内核的 rtl8xxxu 也不支持 8188EU AP），vendor
  驱动是唯一 AP 参照。
- **未做**：A4 吞吐、WPA2（hostapd 有 `--wpa2` 开关，驱动 `set_key` 未证）、
  多客户端（`ra_info` 单份；多站要 RFC v3 的三个 per-station RA 补丁）、5 GHz。

## 4. F429 的两条实现路径（本轮 A0 先选路）

AP MLME 的职责（beacon 组合、auth/assoc 应答、开受控口）在 RK 臂上由
**NXP hostapd 导入**（`third-party/wpa_supplicant-rtos`，`CONFIG_HOSTAP_AP=y`，
`components/liblinux80211/hostap_ap.h` 是入口）承担。F429 有两条路：

**Path A —— 照抄 RK：把 hostapd 导入链进 F429 镜像**
- 新增 `stm32f429-wifi-ap` profile（c6 + `CONFIG_HOSTAP_AP=y`）+ 一个
  `f429_wifi_ap` case（把 `rtl8xxxu_ap.c` 的流程接到 F429 的 case 框架上）。
- 风险：**镜像大小**。RK 的 AP 镜像是 1.77 MB，F429 只有 1 MB flash，c6 已
  726 KB。hostapd 导入（含 eloop/event 队列）多半加 200–400 KB → 很可能超。
- 先做 A0-size：只加 `CONFIG_HOSTAP_AP=y` 编一个空 case 的镜像，量 size；>950 KB
  就改写 Path B，或先裁（open AP、去掉 WPA2/EAPOL 的全部代码）。

**Path B —— 第一方最小 AP MLME（推荐先试，因为 shim 已经齐）**
`components/liblinux80211/include/linux80211/nl80211_probe.h` 已有 AP 面：
- `lkpi_nl80211_set_interface(dev, NL80211_IFTYPE_AP)`；
- `lkpi_nl80211_start_ap(dev, ssid, freq, width, bcn_int, dtim, head, head_len,
  tail, tail_len, privacy)`——case 自己组 36 字节 beacon head + IE tail，
  mac80211 插 TIM；
- `lkpi_nl80211_mgmt_tx`（发 probe response/auth/assoc response）、
  `lkpi_nl80211_new_station` + `lkpi_nl80211_sta_authorized`（建站/开受控口）、
  `lkpi_nl80211_stop_ap`；
- 见证：`lkpi_nl80211_sta_count` / `iftype` / `link_freq`。
- 缺口：**入站管理帧**。hostapd 靠 `hostap_rx_wrap.c`（`cfg80211_rx_mgmt_ext`
  的 `--wrap`）把帧喂进 port 队列；第一方 case 要决定是复用同一个 wrap
  自己消费（`lkpi_hostap_poll` 的队列），还是在 wrap 里分出一条第一方回调。
  这是 Path B 的唯一真正未知项，A0 时先读 `hostap_rx_wrap.c` 和
  `linuxkpi_nl80211_probe.c` 的 `lkpi_nl80211_start_ap` 实现。

**A0 决策实验（先做，半轮）**：`make.rtos image PROFILE=stm32f429-wifi-ap-empty`
（c6 + `CONFIG_HOSTAP_AP=y` + 空 case）→ 记录 size 与能否 boot/链接；
同时读 Path B 的三个文件，把"入站帧谁消费"写成一句话。选路后再开 A1。

## 5. F429 特有约束（每一条都踩过）

1. **复位 = relay 冷启动**：`power.sh cycle --confirm`。MCU nRST 不重置 USB 设备
   （卡死的 dongle 会让下一 boot 所有 RX bulk IN 失败、`rx_err=32`），
   所以 AP 轮每 boot 之间必须冷启动。
2. **console 打印锁已修**（armv7m 关中断）。AP 路径上别加 per-frame 打印：
   beacon 100 TU、assoc/beacon 路径的每帧格式化会吃掉毫秒（8189 AP 轮和
   GT AP 都踩过）；只留 firmware/start 白名单和限流错误行。
3. **空口见证的卡位**：本台架 **MT7921AU 当 monitor 看不到 F429 的帧**
   （W6B 实测：AP 侧 44/44，MTK monitor 0 帧）。AP 轮的 monitor 必须是
   **RTL8812AU（wlan2）**（`pi-ap.sh` 会把 wlan2 变 monitor、wlan1 变 STA），
   或者干脆用客户端自己的扫描/关联输出当 A1 见证。
4. **AP 侧 IP 级账**：`tcpdump -i wlan2 icmp` 的 44/44 型账仍是最硬的数据面见证。
5. **USB 拓扑**：A1–A3 **直连 P2**（不要先上 hub）。hub 是 W6 之后才验的
   （自供电 hub 可用；总线供电不可靠），AP 轮一次只动一个变量。
6. **时间戳不可信**：本镜像的 console 相位时间戳不单调，判据只认
   `rc=`/`bytes`/`kbit` 字段与行序，不认时间戳。
7. **Pi 侧的收尾**：`tools/rtos/pi-ap.sh`（停 lab-ap、wlan2→monitor、wlan1→STA
   `192.168.99.2`）；跑完 trap 恢复 `lab-ap`。对端恢复失败时下一轮 STA 会被
   ARP/邻居表污染——每轮开始先 `ip neigh show 192.168.99.2` 核对板 MAC。

## 6. 起手清单（下一轮按顺序）

1. 读 §3/§4 的文件与 RK 的 AP 记录；跑 A0（size + 入站帧归属）。
2. 定 profile 名：Path A = `stm32f429-wifi-ap`，Path B = `stm32f429-wifi-ap-fp`；
   case 名 `f429_wifi_ap`（case 框架、锚点风格抄 `f429_wifi.c` 的 STA 侧：
   `stage=`、`usb backend ready`、`wlan0 present=`、`set_iface`、`start`、
   `link freq=`、`sta n=`）。
3. A1 先只做"BSS 起来 + beacon 上空口"：`link freq=2432` + monitor 按
   BSSID/interval 判；beacon 只 1 个就是 patch 0003 没生效（看 `host_beacon_repeat`
   的 diag 计数）。
4. A2：Pi MT7921AU STA 加入（`pi-ap.sh`），板端 `sta_count=1`。
5. A3：双向 ICMP 20/20（56 B + 1400 B），AP 侧 `tcpdump icmp` 对账。
6. 2/2 冷启动后收口：run 目录、`docs/F429-WIFI.md`、`docs/CONSOLE-EVIDENCE.md`、
   `docs/HANDOFF-F429-AP.md` 自身更新（回填实测）。

## 7. 停损与重开条件

- A0 超过半轮没有"size + 入站帧归属"的结论 → 停，先出两条路的预算单。
- A1 两轮没有 beacon 上空的独立见证 → 停，先修见证（客户端扫描 + RTL8812AU
  monitor + 驱动 diag 三选二），不猜芯片。
- 镜像 >950 KB 且裁剪不下去 → 换 Path B，不硬塞 hostapd。
- WPA2/多客户端/5 GHz 不并入本轮判据；各自需要预注册。

## 8. 证据落点

`runs/<date>-f429-wifi-ap/<boot>/`：`flash.json`（或 `relay-cycle.txt`）、
`serial.log`/`serial-boot.log`、`image.sha256`、monitor pcap、
AP 侧 `tcpdump -i wlan2 icmp`、`station-after.txt`。可读转写进
`docs/CONSOLE-EVIDENCE.md`，事实进 `docs/F429-WIFI.md`，本文件只保留入口与判据。
