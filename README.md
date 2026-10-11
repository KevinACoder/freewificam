# freewificam

A portable RTOS webcam stack: boot, OS, drivers, components and application. Two headline
milestones — wireless (Wi-Fi) and the USB camera (UVC) — with the end goal of streaming
camera data over Wi-Fi for long, unattended runs. Dual-kernel mainline: ThreadX and
FreeRTOS behind a CMSIS-RTOS2 abstraction, with a host-side mock test tier for
pre-hardware verification.

Tier-1 targets are MCU-class parts (STM32 and similar), where most RTOS deployments live,
and 64-bit ARM (aarch64) boards such as the RK3568 ITX — the latter also being the
convenient vehicle for porting Linux drivers. Components, ports and contract tests avoid
MMU, 64-bit and large-memory assumptions so that both tiers can use the same sources.

**Status**: repository bootstrap. Mainline branch: `master`. The build skeleton, upstream
component pins and the mock tier are landed milestone by milestone; design notes appear
under `docs/` as the corresponding lines complete.

## Why this project

Wireless is the core milestone, and the end goal is streaming camera data over Wi-Fi for
long, unattended runs. Open-source 802.11 support on RTOS kernels has, in practice, been
tied to vendor SDKs and binary blobs, without the reusable, vendored-and-ported stack that
BSD and Linux enjoy. Contributing that missing piece — a portable 802.11 stack with
per-OS ports that other RTOS projects can adopt — is the point of this project. The
wireless stack is therefore structured as a library with three port layers (OS adaptation,
bus backend, network-stack presentation) rather than as a firmware-private tree.

## Planned layout

| Path | Purpose |
|---|---|
| `arch/` | CPU / architecture layer (aarch64, Cortex-M) |
| `os/` | RTOS ports and the CMSIS-RTOS2 abstraction |
| `board/` | Board support, one directory per target |
| `components/` | Drivers and protocol stacks, plus the imported units (`net80211/`, `linuxkpi/`, `skbuff/`, `cherrysh/`) with their provenance ledgers |
| `ports/` | The project's port layers for the imported units (OS adaptation, bus backend, network presentation) |
| `app/` | Application entry points (one per OS / flavor) |
| `third-party/` | Vendored upstream sources (pinned) |
| `patches/` | Local patches replayed onto upstream pins |
| `configs/`, `build/` | Build configuration and artifacts |
| `tools/`, `tools/insight/` | Host tools and diagnostic commands |
| `docs/` | Design documentation |

## Related projects

Upstream projects this stack is built on or contributes to (original links; per-component
licenses are recorded as components are vendored):

- net80211 portable library for RTOS environments — https://github.com/KevinACoder/net_80211
- wpa_supplicant port for RTOS environments — https://github.com/KevinACoder/wpa_supplicant-rtos
- Embox (RTOS; a consumer of the net80211 port) — https://github.com/embox/embox
- ThreadX — https://github.com/eclipse-threadx/threadx
- FreeRTOS — https://github.com/FreeRTOS/FreeRTOS-Kernel
- CherryUSB — https://github.com/cherry-embedded/CherryUSB
- CherrySH (embedded console shell) — https://github.com/cherry-embedded/CherrySH
- lwIP — https://savannah.nongnu.org/projects/lwip/
- NetBSD (source of the imported 802.11 stack and chip drivers) — https://github.com/NetBSD/src
- TizenRT (source of the imported skbuff unit) — https://github.com/Samsung/TizenRT

## Contributing

Contribution flow and commit conventions: see `CONTRIBUTING.md` (added with the
bootstrap milestone). Upstream projects are vendored as pinned submodules or, for the
imported units under `components/`, as verbatim snapshots with a provenance ledger
(`PROVENANCE.md`) and a divergence ledger next to them; local changes are replayed from
`patches/`.

## License

First-party code is BSD-3-Clause. Every vendored tree, imported unit and binary blob keeps
its upstream license, recorded component by component in [`LICENSES.md`](LICENSES.md) —
including the distribution obligations that come with the one copyleft unit in the tree
(the GPL-2.0-only mainline WiFi driver under `third-party/rtl8xxxu`). The top-level
`LICENSE` file is added with the bootstrap milestone.

---

# freewificam（中文）

**可移植的** RTOS 摄像头固件栈（引导 + OS + 驱动 + 组件 + 应用）。两个重要
里程碑 —— 无线（WiFi）与 USB 摄像头（UVC）—— 终点是让摄像头数据经无线长时间稳定
打流（无线 IP 摄像头形态）。OS 双主线 ThreadX 与 FreeRTOS，经 CMSIS-RTOS2 抽象缝合，
并配套宿主（Linux）mock 测试层，用于不上板验证。

本项目**不绑定单一板卡**：Tier-1 目标为 MCU（STM32 一类，RTOS 部署的主体）与 64 位 ARM
（aarch64，如 RK3568 ITX —— 也是移植 Linux 驱动最方便的载体）。组件、port 与契约测试
不假设 MMU / 64 位 / 大内存，两类目标共用同一份代码。

**状态**：仓库自举期。主线分支 `master`；构建骨架、上游组件 pin 与 mock 层按里程碑
逐步落地，设计文档随对应分线进入 `docs/`。贡献流程与提交规范见 `CONTRIBUTING.md`
（随自举里程碑加入）；许可待定。

**项目意义**：无线是核心里程碑，终点是让摄像头数据经无线长时间稳定打流。RTOS 内核上的
开源 802.11 支持长期依赖厂商 SDK 与二进制固件，缺少 BSD/Linux 那样的可复用栈——本项目
的目标就是把这块补上：无线栈按"可移植库 + 每 OS 一个 port（OS 适配 / 总线后端 / 网络栈
表示层）"的结构交付，供其他 RTOS 项目复用。关联项目（原链接）：net_80211 可移植库、
wpa_supplicant-rtos、Embox、ThreadX、FreeRTOS、CherryUSB、CherrySH、lwIP、NetBSD、TizenRT
（`components/skbuff/` 导入单元的来源）。
