# patches/rtw88 - index

Local patches replayed onto the vendored rtw88 submodule
(`third-party/rtw88/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by
`tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtw88.git (branch `linux-7.3-rc2`) |
| Base (= pin) | `8fcd0b704502fe7c30ee1c5404d6da4bd5c87b37` (branch tip: the single export commit) |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design |
| Digests | `MANIFEST.sha256` - `sha256` of every tracked file, measured in this checkout and cross-checked against the source kernel tree's blobs (98/98 equal) |

Why zero patches:

* The driver is consumed as mainline wrote it. Everything this project needs that the
  driver does not carry - the mac80211/cfg80211 header face, the firmware registry, the
  USB class hook, the net80211 bridge - lives in first-party translation units and touches
  the driver only through header interfaces, so no upstream byte has to move.
* The predecessor's three patches on this driver are deliberately not carried. The one
  that un-statics the `usb_driver` object in `rtw8821cu.c` (the USB entry must be
  registered explicitly when the port grows an entry unit) is not needed this round: the
  driver registers through `module_usb_driver`, which this project's linuxkpi maps into the
  `.lkpi_module` section the way it already does for rtw89. The one that kills and re-arms
  the receive URBs after a chip power cycle is a candidate for the board round, and the
  first thing to try there is fixing this project's own USB engine rather than porting the
  workaround. The third was a temporary diagnostic print set and is not carried.
* If a real defect in the driver turns out to block the port, the fix is written here in
  `git format-patch` form (English, one concern, `Signed-off-by`) so it can be sent to the
  driver's maintainers; the extracted bytes stay untouched.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/rtw88.provenance.md`.
