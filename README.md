# freewificam

A portable RTOS WiFi stack: the 802.11 core, the Linux SoftMAC drivers and a
supplicant, running on an RTOS kernel through a single abstraction.

**Plug a commodity WiFi card into an RTOS and use it.** That is the point. An
off-the-shelf USB WiFi dongle - the same one you would plug into a PC - with no
vendor SDK and no vendor blob: the mainline Linux driver, taken unchanged,
running on ThreadX or FreeRTOS.

**An open-source port of the rtw88 and rtw89 SoftMAC drivers to an RTOS.** The
rtw88 and rtw89 drivers are the mainline Linux drivers for a large family of
Realtek WiFi chips. To our knowledge this is the first open-source port of
these two drivers to an RTOS: the driver bytes are unchanged, and the
RTOS-facing interface is provided underneath them.

The end goal is a camera whose data streams over WiFi for long, unattended runs
- a wireless IP camera on an RTOS. This snapshot covers the wireless side; the
camera side is not started.

## Why this project

Open-source 802.11 support on RTOS kernels has, in practice, been tied to
vendor SDKs and binary blobs, without the reusable, vendored-and-ported stack
that BSD and Linux enjoy. Contributing that missing piece - a portable 802.11
stack with per-OS ports that other RTOS projects can adopt - is the point of
this project.

The stack is deliberately not tied to one board. The components, the ports and
the contract tests must not assume an MMU, 64-bit or a large memory, so the
same code can run on a small MCU and on a 64-bit application processor. Two
kernels are supported (ThreadX and FreeRTOS) behind one CMSIS-RTOS2
abstraction, and the abstraction is enforced: the components must build and
link on both, and a gate rejects a foreign kernel call in a component.

## What runs today

Three USB WiFi lines are brought up on the reference board. "Full chain" means
the card is enumerated, its firmware loaded, it scans, it joins a WPA2 network,
it gets an address over DHCP, it answers ping, and TCP data flows. It does not
mean the throughput is good.

| Line | Chip | Board state |
|---|---|---|
| `rtl8xxxu` | RTL8188EUS | Full chain. |
| `rtw88` | RTL8821CU | Full chain, including a boot-time CD-ROM to WLAN mode switch. |
| `rtw89` | RTL8851BU | **Experimental**: full chain reached, join intermittent, link poor. |
| SDIO | RTL8189FTV | Design only; no hardware evidence. |

### Throughput

Host to board, single TCP stream, measured on the board. The Linux column is
the same card under Linux on the same board, on the same channel and bandwidth;
it is left pending because that control run is not finished. Older Linux
figures taken on a different band are not comparable and are not used here.

| Line | RTOS (host to board) | Linux, same card, same channel |
|---|---|---|
| RTL8188EUS | 4.00 / 4.66 Mbit/s | **30.5 / 32.9 / 43.1 Mbit/s** (ch1, same BSSID) |
| RTL8821CU | 3.65 Mbit/s | pending (control run) |
| RTL8851BU | 136 / 173 Kbit/s | pending (control run) |

On the RTL8188EUS line both sides are measured on channel 1, against the same
access point: the RTOS figure is about one eighth of the Linux figure. The
other two lines' Linux figures are pending.

The gap to Linux is large, and it is the current open problem. It is stated
here rather than hidden: the value of the port so far is in the structure, not
in the numbers. See [docs/known-limitations.md](docs/known-limitations.md).

### The camera line

The USB camera (UVC) line is the second headline milestone and is in progress.
It has no board evidence yet: the USB host stack in this tree does not include
an isochronous transfer path, and the camera class is not ported.

## How it is built

```
  app/                    entry per OS and flavour
  os/                     RTOS ports and the CMSIS-RTOS2 abstraction
  arch/  board/           CPU layer and per-board BSP
  components/             drivers and protocol stacks
    net80211/             the 802.11 core (vendored)
    linuxkpi/             the Linux driver-facing interface
    skbuff/  tlsf/  ...   supporting units
  ports/                  first-party glue for the imported units
    net80211/             osal / bus / net port + the driver bridge
    cherryusb/  lwip/     bus and network backends
  third-party/            pinned upstream trees (Linux drivers, RTOS kernels)
  tests/host/  tests/board/   the two test tiers
```

The wireless core is the authority and the driver aligns to it. The bridge
presents the contract the Linux driver was written against; it is not relaxed
to accommodate a particular chip. Where a chip needs something the interface
normally supplies, the driver is given that state and the chip's own logic
does the rest. See [docs/net80211.md](docs/net80211.md).

## Build and test

The build is driven from the repository root:

```sh
make -C freewificam mock           # host mock suites (Gate1; seconds to minutes)
make -C freewificam gates          # static gates (local knowledge, build shape)
make -C freewificam board-images   # build every board image
```

A single suite: `freewificam/tools/mock/run.sh <suite>` (`--list` shows the
registry). The board tier is driven by `freewificam/tools/gates/run-gate2.sh`.
The two tiers and what they guarantee are described in
[docs/bus-and-threading.md](docs/bus-and-threading.md) and
[CONTRIBUTING.md](CONTRIBUTING.md).

## Related projects

This stack stands on other people's work and says so: the net80211 core
(NetBSD lineage), CherryUSB, CherrySH, lwIP, the ThreadX and FreeRTOS kernels,
CMSIS, and the mainline Linux drivers. Each is vendored or imported with its
license recorded in [LICENSES.md](LICENSES.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

First-party code is BSD-3-Clause (see [LICENSE](LICENSE)). Every vendored tree,
imported unit and binary blob keeps its upstream license, recorded component by
component in [LICENSES.md](LICENSES.md), including the distribution obligations
for the one copyleft unit in the tree.

---

# freewificam（中文）

**可移植的** RTOS WiFi 栈：802.11 核心 + Linux SoftMAC 驱动 + supplicant，
经一层统一抽象跑在 RTOS 内核上。

**把随手可得的 WiFi 卡插到 RTOS 上就能用**——这就是本项目的要点。一张市面
上的 USB 无线网卡（就是你插到电脑上的那种），不需要厂商 SDK、不需要厂商二进制
固件：直接跑**未经改动的主线 Linux 驱动**，运行在 ThreadX 或 FreeRTOS 上。

**rtw88 / rtw89 SoftMAC 驱动的开源 RTOS 移植。** rtw88 与 rtw89 是主线 Linux
里一大票 Realtek 无线芯片的驱动。据我们所知，这是这两个驱动在 RTOS 上的首个
开源移植：驱动源码一字未改，RTOS 侧的接口在它下面提供。

终局目标 = **摄像头数据经无线长时间打流**（RTOS 上的无线 IP 摄像头）。本次快照
只覆盖无线侧；摄像头侧尚未开始。

## 为什么做这个

RTOS 内核上的开源 802.11 支持长期依赖厂商 SDK 与二进制固件，缺少 BSD/Linux
那样的可复用栈。本项目要补的就是这块：一套可移植的 802.11 栈 + 每 OS 一个端口，
供其他 RTOS 项目直接采用。

栈**不绑定单一板卡**：组件、端口与契约测试都不假设 MMU、64 位或大内存，同一份
代码既能跑小 MCU、也能跑 64 位应用处理器。内核双主线（ThreadX 与 FreeRTOS）经
一层 CMSIS-RTOS2 抽象缝合，且这层抽象是**强制门禁**：组件必须在两个内核下都能
编译链接，门禁会拒绝组件里的外来内核调用。

## 当前跑通的东西

参考板上有三条 USB 无线线跑通。"全链" = 认卡 → 下固件 → 扫描 → WPA2 关联 →
DHCP 得址 → 网关 ping → TCP 传数；它**不代表**吞吐好。

| 线 | 芯片 | 板上状态 |
|---|---|---|
| `rtl8xxxu` | RTL8188EUS | 全链通过 |
| `rtw88` | RTL8821CU | 全链通过（含冷启动光盘模式→网卡模式切换） |
| `rtw89` | RTL8851BU | **实验性**：全链摸到过，但关联间歇、链路差 |
| SDIO | RTL8189FTV | 只有设计，无板上证据 |

### 吞吐

宿主→板、单 TCP 流、板上实测。Linux 列是同一张卡在同一块板、同信道同带宽下的
数字，**留空待补**——那条公平对照还没跑完。频段不同的旧数字不可比，此处不用。

| 线 | RTOS（宿主→板） | Linux 同卡同信道 |
|---|---|---|
| RTL8188EUS | 4.00 / 4.66 Mbit/s | **30.5 / 32.9 / 43.1 Mbit/s**（ch1，同一 BSSID） |
| RTL8821CU | 3.65 Mbit/s | 待对照轮 |
| RTL8851BU | 136 / 173 Kbit/s | 待对照轮 |

8188 线两边都在 ch1、对着同一个 AP 测：RTOS 约为 Linux 的 **1/8**。另两条的 Linux 数字待补。

与 Linux 的差距很大，这是当前待解的问题。写在这里而不是藏着：目前这条线的价值
在结构，不在数字。详见 [docs/known-limitations.md](docs/known-limitations.md)。

### 摄像头线

USB 摄像头（UVC）线是第二个主里程碑，**进行中、尚无板上证据**：本树的 USB 主机
栈还没有等时（iso）传输通路，摄像头类也尚未移植。

## 怎么构建与测试

从仓库根目录驱动：

```sh
make -C freewificam mock           # 宿主 mock 套件（Gate1，秒到分钟级）
make -C freewificam gates          # 静态门禁（本地知识、构建形态）
make -C freewificam board-images   # 构建全部板镜像
```

单套件：`freewificam/tools/mock/run.sh <suite>`（`--list` 列注册表）。板测层由
`freewificam/tools/gates/run-gate2.sh` 驱动。两层各自的保证见
[docs/bus-and-threading.md](docs/bus-and-threading.md) 与
[CONTRIBUTING.md](CONTRIBUTING.md)。

## 关联项目

本栈站在别人的工作上，并且如实标注：net80211 核心（NetBSD 血统）、CherryUSB、
CherrySH、lwIP、ThreadX 与 FreeRTOS 内核、CMSIS，以及主线 Linux 驱动。每一件都
以 vendored 或导入单元形式进仓，许可登记在 [LICENSES.md](LICENSES.md)。

## 贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可

首方代码为 BSD-3-Clause（见 [LICENSE](LICENSE)）。所有 vendored 树、导入单元与
二进制固件保留各自上游许可，逐件登记在 [LICENSES.md](LICENSES.md)，含树内唯一
copyleft 件的分发义务。
