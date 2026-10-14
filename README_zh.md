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
| SDIO | RTL8189FTV | 全链通过（走 SDIO 总线而非 USB；vendor 驱动自带 MLME，对接本仓的 cfg80211 与 netdev 面） |
| `iwlwifi` | Intel Wireless-AC 7260 | 全链通过（走 PCIe 消息中断（GICv3 ITS）；驱动的站点管理帧与数据帧经其发送队列路由，端口侧提供线程化中断所需的两个独立上下文） |
| UVC | 罗技 Pro 9000 | 摄像头认卡、打开，并在其等时端点上出流 MJPEG 640x480（见[摄像头线](#摄像头线)） |

### 吞吐

宿主→板、单 TCP 流、板上实测。Linux 列是同一张卡在同一块板、干净上游构建、同信道、
同一个 AP 下的数字。注意**带宽口径**：Linux 在这个 AP 上协商到 HT40，而 RTOS 口跑
20 MHz legacy，所以 Linux 数字带一份「更宽信道」的红利。频段不同的旧数字不可比，此处不用。

| 线 | RTOS（宿主→板） | Linux 同卡同信道 |
|---|---|---|
| RTL8188EUS | 4.00 / 4.66 Mbit/s | **42.6 / 43.9 / 45.3 Mbit/s** |
| RTL8821CU | 3.65 Mbit/s | **63.7 / 66.0 / 68.2 Mbit/s** |
| RTL8851BU | 136 / 173 Kbit/s | **23.9 / 25.3 / 26.5 Mbit/s** |
| RTL8189FTV（SDIO） | 10.6 / 1.18 / 10.9 Mbit/s | **45.0 / 46.4 Mbit/s** |
| Intel Wireless-AC 7260（PCIe） | 0.24 Mbit/s | **167 Mbit/s** |

五线都在 ch1（2412 MHz）、对着同一个 AP（BSSID `14:a3:2f:18:07:2c`）测；Linux 每格是
3 次 30 s 的 min/avg/max，RTL8189FTV 一格是实验室台账记的同卡真值范围，7260 一格是
同款 iperf 会话单次 20 s（RTOS 一格为单次 31 s）。RTOS 五个数字
分别约为 Linux 的 **1/10、1/18、1/170、1/4、（PCIe 线）1/700**。SDIO 线最接近 Linux，也最不稳：三次里两次
稳在 10.6 / 10.9 Mbit/s，一次塌到 1.18 Mbit/s——这是驱动默认省电态的形态，Linux 那条线
正是靠关掉省电才止住 TCP 崩。Linux 五线都做了反向（板→宿主）；RTOS 反向尚未测。

与 Linux 的差距很大，这是当前待解的问题。写在这里而不是藏着：目前这条线的价值
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
