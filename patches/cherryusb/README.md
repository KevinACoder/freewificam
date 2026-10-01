# patches/cherryusb

CherryUSB 上游差异的**唯一真值源**（submodule 工作树禁改；差异只以 patch 形式存 here，
`make modules` 构建期重放进 `third-party/cherryusb/`）。

## pin

- 上游 URL：<https://github.com/cherry-embedded/CherryUSB.git>（.gitmodules）
- pin = **`323ade2f`（origin/master tip，2026-10-01 用户裁决 re-pin）**。
  原 pin `6f096b14`（dev，v1.6.1-97）被上游 dev 强推丢弃且不会随 dev→master 合并
  回归（issues/2026-10-01-cherryusb-pin-upstream-force-pushed.md 有完整验证）；
  用户裁决 dev 已消失不需要，直接落 GitHub 主路——新环境 `git submodule update
  --init` 直连可用，**不再需要任何镜像播种**。
- 等价性证据：master 相对旧 pin 在宿主栈消费面（usbh_core/common/class/hub/
  osal 除 idf）内容零差异，重建镜像与 P5-d 验收镜像逐字节相同（sha256
  bbc83b7e…，crc32 3dc607a5），板上冷启全锚点复现（boot-125830-10dd）。
- **EHCI 注意（P5c 打开 CONFIG_USBHOST_EHCI 前必读）**：master 的
  d14c4aa5 把 port low-level api 从 weak 改 extern——集成者必须实现
  `usb_hc_low_level_deinit`（本仓 usbh_glue.c 目前只实现了 init，P5c 需补）。

## patches

| patch | 内容 |
|---|---|
| `0001-ehci-clear-iaa-worklist-flag-with-the-qh-urb.patch` | EHCI IAA worklist 修复（ehci_qh_free 清 remove_in_iaad/killed + IRQ 扫描条件加 qh->urb）。注意：前作 NOTICE.md:282 记载该思路被社区定性不对（600s 丢完成病根另寻），本仓仅随栈携带供 P5c 对照；在 master 基座上验证可干净应用（2026-10-01）。 |
| `0002-xhci-host-controller-port.patch` | **仅新增两文件** `port/xhci/usb_hc_xhci.c`（2650 行）+ `usb_hc_xhci.h`（386 行）：NetBSD xhci.c rev 1.188.2.4 寄存器序列 × vendored usb_hc_ehci.c 异步 URB 契约，HS/FS only。 |

## 重放（构建期，Makefile `modules` target 同款）

```sh
git -C third-party/cherryusb apply --check "$PWD/patches/cherryusb/<p>.patch" \
  && git -C third-party/cherryusb apply "$PWD/patches/cherryusb/<p>.patch"
```

物化态（应用后的工作树）**不入 submodule 提交**；物化后对拍锚点：`usb_hc_xhci.c`
2650 行、`.h` 386 行。

## sparse-checkout（`make modules` 自动设置，非 cone）

`/core/ /common/ /class/hub/ /port/ehci/ /port/xhci/ /osal/`，
`!/port/xhci/*/` 排除一级子目录（phytium 闭源 blob，clean-room 纪律）；
demo/tests/docs（~81MB）与 bundled third_party/ 不检出。

## 历史注记：旧 pin 6f096b14 的播种配方（2026-10-01 re-pin 前适用，留档）

当时上游 dev 强推丢弃旧 dev 头，pin 对象仅存前作镜像，新环境需：
`git -C third-party/cherryusb fetch ~/workspace/project_web_camera/freewebcamera/third-party/cherryusb 'refs/heads/*:refs/mirror/*'` 后 checkout 该 pin。re-pin 到
master 后此配方不再需要。
