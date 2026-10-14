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

参考板上有三条 USB 无线线、一条 SDIO 无线线、一条 PCIe 无线线跑通，USB 摄像头线也在同一块板上采帧出流。
"全链" = 认卡 → 下固件 → 扫描 → WPA2 关联 → DHCP 得址 → 网关 ping → TCP 传数；它**不代表**吞吐好。

| 线 | 芯片 | 板上状态 |
|---|---|---|
| `rtl8xxxu` | RTL8188EUS | 全链通过 |
| `rtw88` | RTL8821CU | 全链通过（含冷启动光盘模式→网卡模式切换） |
| `rtw89` | RTL8851BU | **实验性**：全链摸到过，但关联间歇、链路差 |
| SDIO | RTL8189FTV | 全链通过（走 SDIO 总线而非 USB；vendor 驱动自带 MLME，对接本仓的 cfg80211 与 netdev 面）。**它还能自己当开放热点**：对端关联到板上，两个方向都有数（见[吞吐](#吞吐)） |
| `iwlwifi` | Intel Wireless-AC 7260 | 全链通过（走 PCIe 消息中断（GICv3 ITS）；驱动的站点管理帧与数据帧经其发送队列路由，端口侧提供线程化中断所需的两个独立上下文） |
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
**约 1/4 推到 下行 ≈1/1.7、上行 ≈1/1.9**，五线里最接近。它的**反方向也在同一台测量机上首次测出**
（板当 iperf 客户端），所以上表的"未测"是逐线的状态、不是笼统的"反向没测"。后来打通的
**热点模式**（板当 AP、对端关联上来）实测下行 19.8–20.2 / 上行 18.3 Mbit/s。其余各线的差距照旧：
1/10（8188EUS）、1/18（8821CU）、1/170（8851BU）、1/700（7260）。

与 Linux 的差距在多数线上仍然很大，这是当前待解的问题。写在这里而不是藏着：目前这条线的价值
在结构，不在数字。详见 [docs/known-limitations.md](docs/known-limitations.md)。

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
