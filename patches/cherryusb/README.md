# patches/cherryusb

CherryUSB 上游差异的**唯一真值源**（submodule 工作树禁改；差异只以 patch 形式存 here，
`make modules` 构建期重放进 `third-party/cherryusb/`）。

## pin

- 上游 URL：<https://github.com/cherry-embedded/CherryUSB.git>（.gitmodules）
- pin = **`6f096b14`**（dev 分支，v1.6.1-97-g6f096b14，Apache-2.0）
- 选 pin 的理由（IMPORT-INFO 同款论证）：首个带上游 multi-HC 注册
  `usbh_register_hc_driver`（8b7e58c6）与 kill_urb hcpriv 校验修复（6c40a283）的 dev 提交。

## ⚠️ pin 对象上游已丢（2026-10-01 实测）

CherryUSB 的 dev 分支头习惯性命名 "temp" 且**强推**：上游 dev 当前 tip 已是 `c7a1e75c`，
`6f096b14` 在 GitHub 上**不再可达**。pin 对象的权威副本 = 前作镜像
`~/workspace/project_web_camera/freewebcamera/third-party/cherryusb`（其物化提交
`91d55e37` 的父即本 pin，逐字节同源）。

**新环境播种配方**（`git submodule update --init` 会因对象缺失而失败）：

```sh
git -C third-party/cherryusb fetch \
  ~/workspace/project_web_camera/freewebcamera/third-party/cherryusb \
  'refs/heads/*:refs/mirror/*'
git -C third-party/cherryusb checkout --detach 6f096b143273591904969f828a3771416b0b65eb
```

## patches

| patch | 内容 |
|---|---|
| `0001-ehci-clear-iaa-worklist-flag-with-the-qh-urb.patch` | EHCI IAA worklist 修复（ehci_qh_free 清 remove_in_iaad/killed + IRQ 扫描条件加 qh->urb）。注意：前作 NOTICE.md:282 记载该思路被社区定性不对（600s 丢完成病根另寻），本仓仅随栈携带供 P5c 对照。 |
| `0002-xhci-host-controller-port.patch` | **仅新增两文件** `port/xhci/usb_hc_xhci.c`（2650 行）+ `usb_hc_xhci.h`（386 行）：NetBSD xhci.c rev 1.188.2.4 寄存器序列 × vendored usb_hc_ehci.c 异步 URB 契约，HS/FS only。 |

## 重放（构建期，Makefile `modules` target 同款）

```sh
git -C third-party/cherryusb apply --check "$PWD/patches/cherryusb/<p>.patch" \
  && git -C third-party/cherryusb apply "$PWD/patches/cherryusb/<p>.patch"
```

物化态（应用后的工作树）**不入 submodule 提交**；物化后对拍锚点：`usb_hc_xhci.c` 2650
行、`.h` 386 行，且与前作 `third-party/cherryusb/port/xhci/` 逐字节等同
（2026-10-01 P5-a 落地时 cmp 实测 IDENTICAL）。

## sparse-checkout（`make modules` 自动设置，非 cone）

`/core/ /common/ /class/hub/ /port/ehci/ /port/xhci/ /osal/`，
`!/port/xhci/*/` 排除一级子目录（phytium 闭源 blob，clean-room 纪律）；
demo/tests/docs（~81MB）与 bundled third_party/ 不检出。
