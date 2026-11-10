# freewificam — linux-dev

本仓是 **Linux 无线栈搬到 RTOS** 那条线的代码仓。
与老仓 `project_rea_wcam/freewificam`（`master` / `dev_v2`，302 个提交）**没有历史关系**：
本分支从零开始，按需从老仓搬文件并记录来源。

## 这条分支做什么

把 `net/mac80211` + `net/wireless`（cfg80211）**原样**搬上 RTOS，
只替换总线（CherryUSB / PCIe）与网络出口（lwIP）。
配套的 linuxkpi 从"够驱动用"扩到"够整个 MAC 层用"。

**不做什么**：不做 net80211 适配。那是老仓的线，两条线并行。

## 状态

**S0 spike 阶段。** 当前只回答一件事：mac80211 能不能对着本仓的 linuxkpi 编过、链上。
判据分层见 `docs/STATUS.md`；C0/C1 与 C2–C6 永不合并。

## 目录

| 路径 | 内容 |
|---|---|
| `components/linuxkpi/` | 内核 API 面（本仓主要工作量） |
| `docs/COMMIT-CONVENTION.md` | 提交信息格式（沿用老仓） |
| `docs/PROVENANCE.md` | 每个搬进来的文件的来源 commit |
| `docs/DIVERGENCES.md` | 与来源的偏离 |
| `docs/STATUS.md` | 当前阶段与判据 |
| `tools/check-commit-msg.sh` | 提交信息检查器 |

## 许可证

**本仓是 GPL-2.0 派生。** 它承载 `net/mac80211` / `net/wireless`（`GPL-2.0-only`）。
单向：可以从老仓搬进来，**不许搬回老仓**——老仓的 rtw88/rtw89 两个 profile
还要保持 BSD 可用。详见老仓外层的 `docs/LICENSES.md`。

## 提交

```
<area>: (<label>) <lowercase imperative summary>
```

area / label 见 `docs/COMMIT-CONVENTION.md`。装钩子：

```bash
tools/install-hooks.sh    # 本仓暂无，需要时从老仓搬
```
