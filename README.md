# freewebcam

An RTOS webcam stack for the RK3568 ITX platform: boot, OS, drivers, components and
application. Two headline milestones — wireless (Wi-Fi) and the USB camera (UVC) — with
the end goal of streaming camera data over Wi-Fi for long, unattended runs. Dual-kernel
mainline: ThreadX and FreeRTOS behind a CMSIS-RTOS2 abstraction, with a host-side mock
test tier for pre-hardware verification.

**Status**: repository bootstrap. Mainline branch: `master`. The build skeleton, upstream
component pins and the mock tier are landed milestone by milestone; design notes appear
under `docs/` as the corresponding lines complete.

## Planned layout

| Path | Purpose |
|---|---|
| `arch/` | CPU / architecture layer (aarch64) |
| `os/` | RTOS ports and the CMSIS-RTOS2 abstraction |
| `board/` | Board support (RK3568 ITX) |
| `components/` | Drivers and protocol stacks |
| `app/` | Application entry points (one per OS / flavor) |
| `third-party/` | Vendored upstream sources (pinned) |
| `patches/` | Local patches replayed onto upstream pins |
| `configs/`, `build/` | Build configuration and artifacts |
| `tools/`, `tools/insight/` | Host tools and diagnostic commands |
| `docs/` | Design documentation |

## Contributing

Contribution flow and commit conventions: see `CONTRIBUTING.md` (added with the
bootstrap milestone). Upstream projects are vendored as pinned submodules; local changes
are replayed from `patches/`.

## License

TBD (added with the bootstrap milestone).

---

# freewebcam（中文）

RK3568 ITX 平台上的 RTOS 摄像头固件栈（引导 + OS + 驱动 + 组件 + 应用）。两个重要
里程碑 —— 无线（WiFi）与 USB 摄像头（UVC）—— 终点是让摄像头数据经无线长时间稳定
打流（无线 IP 摄像头形态）。OS 双主线 ThreadX 与 FreeRTOS，经 CMSIS-RTOS2 抽象缝合，
并配套宿主（Linux）mock 测试层，用于不上板验证。

**状态**：仓库自举期。主线分支 `master`；构建骨架、上游组件 pin 与 mock 层按里程碑
逐步落地，设计文档随对应分线进入 `docs/`。贡献流程与提交规范见 `CONTRIBUTING.md`
（随自举里程碑加入）；许可待定。
