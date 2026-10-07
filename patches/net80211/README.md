# patches/net80211 - index

Local patches replayed onto the vendored net80211 component
(`components/net80211/`). Empty at import time: the mapping is zero patch and the
snapshot is byte-identical to the pin.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/net_80211.git |
| Base (= pin) | `9770b1ad7093f43a0f2ab2dc8d980067fb0f62a1` (tag `netbsd-shape-freeze`) |
| Patch list | none |
| Submission status | n/a |

Rules (project patch policy):

* A needed library-body fix is written here as an upstreamable patch
  (`git format-patch` style: one concern per patch, English, `Signed-off-by`, no
  local knowledge) and submitted upstream; the vendored snapshot itself stays
  byte-identical, so a patch is applied only through this channel and is recorded
  in `components/net80211/DIVERGENCES.md` while it is in force.
* The library's own change rule also applies (`components/net80211/IMPORT-INFO.md`):
  if an import file must be touched, keep the edit minimal and mark it with
  `/* NET80211_PORT(L): ... */`.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `components/net80211/PROVENANCE.md` and `DIVERGENCES.md`.
