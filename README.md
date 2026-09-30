# freewebcam

基于 RTOS 的 webcam 项目主仓（OS + 驱动 + 组件 + 应用），aarch64（RK3568）起步。
开发方法论 = spec 先行：任务来源于 `specs/<topic>/SPEC.md`，实现后按 spec 验收；
纲领与纪律见工作区根目录 `AGENTS.md` / `ROADMAP.md`（不在本仓内）。

## 目录约定

| 目录 | 内容 |
|---|---|
| `arch/` | CPU 架构移植层（aarch64、armv7m）——只放架构组件（start/cache/mmu/GIC/timer） |
| `board/` | 板级支持（rk3568-itx、stm32f429）——SoC 外设驱动、板参真值 |
| `os/` | RTOS 内核层（baremetal、cmsis2、freertos、threadx） |
| `components/` | 直接导入的第三方源码（带 license 记录与适配层） |
| `third-party/` | git submodule 专用（本地差异走 patches 重放，禁改 submodule 工作树） |
| `app/` | 应用（hello_world、cherryusb_enum、lwip_lan、lwip_wlan、webcam） |
| `tools/` | 宿主工具 |

## 构建

```sh
make CONFIG=<名> all      # 多配置构建，配置见 configs/
make CONFIG=<名> deploy   # objcopy raw bin + sha256 对拍 + 拷 TFTP 根
make gates                # cleanroom-scan + 打印纪律扫描
```

产物为平铺 raw bin，加载地址 = 链接地址 = 0xa000000，U-Boot `go` 启动（本板禁 bootelf）。
