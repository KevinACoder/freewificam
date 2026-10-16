# freewificam（中文）

[English](README.md) | **中文**

**可移植的** RTOS WiFi 栈：802.11 核心 + Linux SoftMAC 驱动 + supplicant，
经一层统一抽象跑在 RTOS 内核上。

**把随手可得的 WiFi 卡插到 RTOS 上就能用**——这就是本项目的要点。一张市面
上的 USB 无线网卡（就是你插到电脑上的那种），不需要厂商 SDK、不需要厂商二进制
固件：直接跑**未经改动的主线 Linux 驱动**，运行在 ThreadX 或 FreeRTOS 上。

**主线 Linux SoftMAC 驱动的开源 RTOS 移植。** rtw88 与 rtw89 是主线 Linux
里一大票 Realtek 无线芯片的驱动；iwlwifi（mvm 一代）是 Intel 的 Wireless-AC
系列主线驱动。据我们所知，这是这组驱动在 RTOS 上的首个开源移植：驱动源码
一字未改，RTOS 侧的接口在它下面提供。

终局目标 = **摄像头数据经无线长时间打流**（RTOS 上的无线 IP 摄像头）。半边已在树里：
无线侧在板上跑通，USB 摄像头线也在同一块板上采帧出流。

## 为什么做这个

RTOS 内核上的开源 802.11 支持长期依赖厂商 SDK 与二进制固件，缺少 BSD/Linux
那样的可复用栈。本项目要补的就是这块：一套可移植的 802.11 栈 + 每 OS 一个端口，
供其他 RTOS 项目直接采用。

栈**不绑定单一板卡**：组件、端口与契约测试都不假设 MMU、64 位或大内存，同一份
代码既能跑小 MCU、也能跑 64 位应用处理器。内核双主线（ThreadX 与 FreeRTOS）经
一层 CMSIS-RTOS2 抽象缝合，且这层抽象是**强制门禁**：组件必须在两个内核下都能
编译链接，门禁会拒绝组件里的外来内核调用。

## 当前跑通的东西

参考板上有三条 USB 无线线、一条 SDIO 无线线、一条 PCIe 无线线和板载千兆网口跑通，USB 摄像头线也在同一块板上采帧出流。
"全链" = 认卡 → 下固件 → 扫描 → WPA2 关联 → DHCP 得址 → 网关 ping → TCP 传数；它**不代表**吞吐好。

| 线 | 芯片 | 板上状态 |
|---|---|---|
| `rtl8xxxu` | RTL8188EUS | 全链通过 |
| `rtw88` | RTL8821CU | 全链通过（含冷启动光盘模式→网卡模式切换） |
| `rtw89` | RTL8851BU | **实验性**：全链摸到过，但关联间歇、链路差 |
| SDIO | RTL8189FTV | 全链通过（走 SDIO 总线而非 USB；vendor 驱动自带 MLME，对接本仓的 cfg80211 与 netdev 面）。**它还能自己当开放热点**：对端关联到板上，两个方向都有数（见[吞吐](#吞吐)） |
| `iwlwifi` | Intel Wireless-AC 7260 | 全链通过（走 PCIe 消息中断（GICv3 ITS）；驱动的站点管理帧与数据帧经其发送队列路由，端口侧提供线程化中断所需的两个独立上下文） |
| GMAC（有线） | RK3568 MAC + RTL8211F PHY | 全链通过：DHCP、ping，以及对着树莓派对端的双向单 TCP 流（走板载 RJ45）——本板**最接近 Linux** 的一条线（见[吞吐](#吞吐)） |
| UVC | 罗技 Pro 9000 | 摄像头认卡、打开，并在其等时端点上出流 MJPEG 640x480（见[摄像头线](#摄像头线)） |

### 吞吐

单 TCP 流、**收方口径**、对着同一个 AP（ch1）测。Linux 列是同一张卡在同一块板、干净上游
构建的数字。**档位跨冷启会漂**，所以每格都注明测时的带宽档，同一行的 RTOS 与 Linux 取同档。

| 线 | RTOS 下行（宿主→板） | RTOS 上行（板→宿主） | Linux 同卡同信道 |
|---|---|---|---|
| RTL8188EUS | 4.00 / 4.66 Mbit/s（HT40） | 未测 | **42.6 / 43.9 / 45.3 Mbit/s** |
| RTL8821CU | 3.65 Mbit/s | 未测 | **63.7 / 66.0 / 68.2 Mbit/s** |
| RTL8851BU | 136 / 173 Kbit/s | 未测 | **23.9 / 25.3 / 26.5 Mbit/s** |
| RTL8189FTV（SDIO） | **18.1（HT20）/ 20.5（HT40）** Mbit/s | **15.1–16.8（HT20）** Mbit/s | **31.4 下行 / 30.7 上行（HT20）**、44.2 / 50.5（HT40） |
| Intel Wireless-AC 7260（PCIe） | 0.24 Mbit/s | 未测 | **167 Mbit/s** |

各线都对着同一个 AP（BSSID `14:a3:2f:18:07:2c`）测；Linux 每格是 3 次 30 s 的 min/avg/max
（7260 一格是单次 20 s）。**RTL8189FTV 是已经动过刀的那条线**：先把收侧测准，找到并采纳两个杠杆——
持续服务忙卡的泵节拍（+45%）与卡侧接收聚合的超时字节（HT20 下 +42%）——把差距从 Linux 的
**约 1/4 推到 下行 ≈1/1.7、上行 ≈1/1.9**，无线五线里最接近。它的**反方向也在同一台测量机上首次测出**
（板当 iperf 客户端），所以上表的"未测"是逐线的状态、不是笼统的"反向没测"。后来打通的
**热点模式**（板当 AP、对端关联上来）实测下行 19.8–20.2 / 上行 18.3 Mbit/s。之后的轮次换到了
项目自有的**受控台架**——一块树莓派 AP，对端行为从此可控——在那里下行测得 **29–30 Mbit/s**，
同卡 Linux 为 43.8（0.67×）；该台架还把剩余缺口定位到了**卡自己的交付节拍**：约每 2.9 ms 放一批
8 帧，与对端 16 帧的块应答窗互锁——空口从来不是约束（MCS7 150 Mbit/s、零重传）。在该台架上实测
并**否掉了两个栈形态杠杆**（栈的输入路径改为调用者线程执行、把卡侧聚合长度阈值调小），均有板面
证据。其余各线的差距照旧：1/10（8188EUS）、1/18（8821CU）、1/170（8851BU）、1/700（7260）。

与 Linux 的差距在多数线上仍然很大，这是当前待解的问题。写在这里而不是藏着：目前这条线的价值
在结构，不在数字。详见 [docs/known-limitations.md](docs/known-limitations.md)。

### 有线以太网（GMAC）

板载千兆 MAC（RK3568 GMAC + RTL8211F PHY），不是无线线：单 TCP 流，板与树莓派对端之间经
板载 RJ45 对打。同板 Linux、同对端：**上行 940 / 下行 941 Mbit/s**；同一驱动下的 UDP 测得
下行 955 / 上行 957，即这条线在该帧长下的物理上限。

| 方向 | RTOS，单 boot 四 sheet（2026-10-20） | Linux，同板同对端 |
|---|---|---|
| 上行（板→对端） | **894.5 – 919.2，中位 900.0 Mbit/s** | 940 Mbit/s |
| 下行（对端→板） | 684.0 – 760.6，中位 742.4 Mbit/s | 941 Mbit/s |

上行达到 Linux 构建的 95.7%，是本板第一个站上该线 900 Mbit/s 验收线的数字；下行在 79%，是
尚未收口的半边。把它推到这里的是两件事：其一，**入口级中断普查**按原始状态字给每个中断入口
分类，顺带证伪了一个错误假设——被计时账记成"5% 核时"的 odd 中断家族实测只有 0.1–0.2%，
真正要付的入口成本是 1.9 µs；其二，**C 库改为直接用工具链的 newlib**，替掉镜像自带的拷贝
实现——手写的 1514 字节 memcpy 每轮循环只搬 8 字节、每帧 1.5 µs（线速下约合单核 13%），而
newlib 的实现按尺寸分支、单指令搬 16 字节（LDP/STP）。该替换现已覆盖全部板镜像：镜像自带的 C 库单元已整树退役。

### 摄像头线

USB 摄像头（UVC）线是第二个主里程碑，**已在参考板上出流**。板块 USB 2.0 主机上的罗技
Pro 9000：认卡 → 跑 probe/commit 交换 → 选 alternate setting → MJPEG 640x480 帧从等时 URB 环
取出、按 payload 头判界装配 —— **冷启多轮，每个 20 s 窗口 267–275 帧，约 13.5 fps / 1.9 Mbit/s**。
同板同相机在 Linux 下给出真值（采集时是暗场景：4.69 fps / 372 kbit/s）；验收底线是该真值的
三分之二，实际跑在其数倍之上。

被移植的 USB 栈有两处需要补齐，各自作为一份**按可提交社区的形式**写的补丁：栈声明却未定义的
高速等时传输引擎，以及它视频类里缺失的「运行中的流」那一半（取流引擎——按设备自报选 alternate
setting、payload 头判界装配帧、帧池——不在开源层里）。详见
[patches/cherryusb/](patches/cherryusb/README.md)，以及
[docs/known-limitations.md](docs/known-limitations.md) 记的「测了什么、没测什么」。

## 怎么搭起来的

```
  app/                    各 OS 与形态的入口
  os/                     RTOS 端口与 CMSIS-RTOS2 抽象
  arch/  board/           CPU 层与逐板 BSP
  components/             驱动与协议栈
    net80211/             802.11 核心（vendored）
    linuxkpi/             面向 Linux 驱动的接口层
    skbuff/  tlsf/  ...   支撑单元
  ports/                  导入单元的首方黏合
    net80211/             osal / bus / net 端口 + 驱动桥
    cherryusb/  fsl_sdmmc/   总线后端（USB 主机、SDIO 主机）
    lwip/                 网络栈后端
  third-party/            钉住版本的上游树（Linux 驱动、RTOS 内核）
  patches/                构建期重放到 vendored 树上的补丁
  tests/host/  tests/board/   两层测试
```

**核心是权威，驱动向核心对齐。** 桥呈现的是 Linux 驱动写作时所对着的契约；它不会为迁就
某颗芯片而放宽。芯片需要接口通常提供的状态时，就把该状态给足驱动，由芯片自己的逻辑完成
其余。见 [docs/net80211.md](docs/net80211.md)。

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
