# third-party/ports/patches/rtw88 — submodule 本地差异（重放补丁）

## submodule pin

- 上游 URL：`git@github.com:KevinACoder/rtw88.git`，分支 **`linux-7.3-rc2`**
- pin：`b057bfd807f3341a3289e22f345503089ebd554f`（快照提交，2026-10-05 推送）
- 快照内容：mainline linux-upstream `e71d9a28760f`（7.3-rc2）的
  `drivers/net/wireless/realtek/rtw88/` 全目录 verbatim（98 文件，含本镜像
  不编译的 Kconfig/Makefile/pci/sdio/wow 等——编译选集由顶层 Makefile
  `RTW88_SRCS` 锁定 21 TU）。
- **master 分支（a56bcd2）是 downstream 仓（同步 wireless-next），与本项目
  基线不同源，禁作 pin**（2026-10-05 用户裁决：推快照分支、master 不动）。
- 无 sparse-checkout（仓本身只有驱动目录，~2MB）。

## 重放

```
make modules          # 或手动：
git submodule update --init third-party/rtw88
git -C third-party/rtw88 apply ../patches/rtw88/0001-....patch   # 逐张
```

`make modules` 的泛化重放循环（`patches/*/*.patch` → `third-party/$comp`）
自动覆盖本目录；已应用的 patch 报 "kept"。

## 补丁清单（与 DIVERGENCES.md 台账对应）

| patch | 内容 | 台账 |
|---|---|---|
| `0001-rtw8821cu-export-usb-driver-D1.patch` | rtw8821cu.c usb_driver 对象去 static（显式注册纪律） | D-1 |
| `0002-usb-rx-rearm-d3-diag-D2-D3.patch` | usb.c：rtw_usb_start rx 队列 kill+重臂（功能性）+ D-3 取证（rx 分类计数/TX 存档/DA 精确计数） | D-2 + D-3 |
| `0003-d3-forensics-rx-tx-D3.patch` | fw.c/main.c/mac80211.c/rx.c/tx.c 的 D-3 取证打印组（c2h/txrpt/ops_tx/bss_info 回读/比较器失谐/swseq） | D-3 |

D-3 整组为"单播 RX 断链"结案前临时取证面，**结案后撤除方式 = 删除
0002/0003 重放重生成**（D-1 的 0001 永久保留）；撤除时同步销台账条目。

## 重放等价性证据

2026-10-05：三张 patch 依序 apply 到快照 `b057bfd8` 后，与重构前 vendored
副本（`feat/rtw88-r4` @ c1bb6bc 的 `components/rtw88`）60 个共享文件
（20 TU 源 + 40 头）**逐字节 cmp 一致（60/60, mismatch=0）**。
vendored 的 PROVENANCE.md/DIVERGENCES.md 迁出为本目录台账；
`fw_rtw8821c.c`（生成物）迁 `third-party/ports/rtw88/`。

## 固件

`fw_rtw8821c.c` 由 `tools/gen_firmware_array.py` 生成（rtw8821c_fw.bin
139,472B，sha256 `2ef409bc…`，登记值见原 PROVENANCE），不属上游源码，
放 `third-party/ports/rtw88/` 随 port 层走。
