# components/net80211 - divergences

Deviations of this snapshot from the upstream pin. Empty at import time.

| # | File | Change | Reason | Round |
|---|---|---|---|---|
| - | - | none | - | - |

Import state (2026-10-07): all 134 files are byte-identical to
`9770b1ad7093f43a0f2ab2dc8d980067fb0f62a1` (`MANIFEST.sha256`, 0 mismatches);
no patch is applied.

Rules (inherited from the library's `IMPORT-INFO.md` and the project patch policy):

* Prefer an upstreamable patch under `patches/net80211/` over an in-tree edit.
* If an in-tree edit is unavoidable, keep it minimal, mark it with
  `/* NET80211_PORT(L): <what and why> */` and add a row here naming the file,
  the reason and the round that needed it.
