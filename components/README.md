# components — 本仓自己的代码

| 组件 | 内容 | 状态 |
|---|---|---|
| `linuxkpi/` | 内核 API 面：够 mac80211 用的那部分 | 待建（S2 阶段） |

## 注意

老仓的 `components/net80211/` **不搬过来**——本仓不用 net80211。
老仓的替身 mac80211（`tests/board/linux-ko/ports/mac80211/`）也不搬：它与真 mac80211
符号冲突，同一镜像里只能有一份。
