# components/net80211 — PROVENANCE

S8 net80211-adapt spec FR-1：vendored verbatim + 偏差台账（不用 submodule——
部分裁剪树无上游可独立 pin）。

## 来源树

- 仓库：`~/workspace/project_web_camera/netbsd-src`（fork `KevinACoder/netbsd-src`，
  本地分支 `netbsd-11`）
- 基线 commit：`111525e242df`（"rtw8189f: program SDIO transfer-page geometry
  before firmware download"，NetBSD 11.0_STABLE，`__NetBSD_Version__ 1100000000`）
- 获取日期：2026-10-04（S8-1 实施轮）
- 与前作对照：fwc-net80211-lkpi 实验线的 `third-party/net80211/` 为同一 pin 的
  逐字快照 + 两处偏差（见 DIVERGENCES.md），本仓直接采用了该两件的成形副本。

## 目录与逐文件来源

| 目录 | 文件 | 来源（相对 netbsd-src 根） |
|---|---|---|
| `net80211/` | 11 .c + 12 .h | `sys/net80211/`（编入 9 .c + 随树不编 tkip/wep + 全部 12 .h） |
| `crypto/aes/` | aes_bear/ccm/ccm_mbuf/ct/ct_dec/ct_enc/impl .c + 头 | `sys/crypto/aes/`（AES 最小子集，S8-Q6 定案 = bear 路线：aes_impl.c 默认后端零偏差；br_aes_ct_* 原语在 aes_ct*.c，bear 是其封装——两者都必需） |

RCS 版本头（`$NetBSD: ...`）随文件携带，逐文件可溯。

### 变更记录（feat/refactor-layout-r1 S4，2026-10-05）

- 删 `urtwn/` 五件（if_urtwn.c / if_urtwnreg.h / if_urtwnvar.h /
  rtwnreg.h / rtwn_data.h，~8.4k 行）——S8-2 只 vendored 未编入的验证
  驱动，全仓零符号引用，用户裁决移除。
- `net80211/ieee80211_netbsd.c` → `net80211/ieee80211_cmsis2.c`（CMSIS2
  本地化改名；头 `ieee80211_netbsd.h` 保留上游原名，避免扩大对
  netbsd-src 基线的 divergence）。

## 许可

net80211 与 AES 子集均为 BSD 许可（文件头 BSD-2-Clause/BSD-4-Clause 按件），整链
BSD = trunk 合入底线（S8 spec 分支裁决）。

## 验收对拍（S8-1 判据 2 留痕）

`cmp` 全量对拍 2026-10-04：编入 9 .c + 12 .h + AES + urtwn 五件中，仅
`net80211/ieee80211_netbsd.c` 与 `crypto/aes/aes_ccm_mbuf.c` 与基线不同——
均已在 `DIVERGENCES.md` 记账（各一条、可逐条指认），其余逐字一致。
