# patches/rtl8xxxu - index

Local patches replayed onto the vendored rtl8xxxu submodule
(`third-party/rtl8xxxu/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by
`tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtl8xxxu.git (branch `linux-7.3-rc2`) |
| Base (= pin) | `639697082494da0d1063a6259a9659c3e3789246` (branch tip: the export commit plus the branch README) |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design |

Why zero patches:

* The driver is consumed as mainline wrote it. Everything this project needs that the
  driver does not carry - the mac80211/cfg80211 header face, the firmware registry, the
  USB class hook, the net80211 bridge - lives in first-party translation units and touches
  the driver only through header interfaces, so no upstream byte has to move.
* The link closure is a build-set question, not a source question: `core.c` references the
  other chip units' `fileops` symbols, so the board image will compile all nine chip units
  rather than trim the id table. Trimming the table by patch stays the fallback if the
  image budget demands it, and it would then be written here as an upstreamable patch.
* If a real defect in the driver turns out to block the port, the fix is written here in
  `git format-patch` form (English, one concern, `Signed-off-by`) so it can be sent to the
  driver's maintainers; the extracted bytes stay untouched.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/rtl8xxxu.provenance.md`.