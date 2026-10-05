# third-party/ports/patches/threadx — 上游差异重放

本目录放对 `third-party/threadx`（pin 44d7c95c）的本地差异，`git format-patch`
产物 + 每片一段动机说明。**当前为空：S3/T1 引入时预计零 patch**（前作对该 pin
逐字节原样编译，所有适配都在 os/threadx/ 适配层，spec FR-1）。

重放流程（物化）：

1. 在 submodule 内以 pin 为父提交落本地改动，`git format-patch <pin>` 输出到本目录；
2. `git submodule update` 后在 submodule 内 `git am third-party/ports/patches/threadx/*.patch`
   生成 fwc 分支，gitlink 指向 fwc 头；
3. 在本 README 登记每片 patch 的动机与影响面；对应 spec（specs/threadx-up-boot）
   修订留痕。

纪律：物化态不入 trunk 跟踪（submodule gitlink 指向的 fwc 头仅本地存在，
与 [S5] §3.3 "构建期 patches 重放" 模型一致）；禁止直接改 submodule 工作树
而不留 patch。
