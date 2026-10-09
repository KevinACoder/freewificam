# patches/rtw89 - index

Local patches replayed onto the vendored rtw89 submodule
(`third-party/rtw89/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by
`tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtw89.git (branch `linux-7.3-rc2`) |
| Base (= pin) | `79e12cc959d80b20f9b8499f885faf62cee1ade1` (branch tip: the single export commit) |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design |
| Digests | `MANIFEST.sha256` - `sha256` of every tracked file, measured in this checkout and cross-checked against the source kernel tree's blobs (107/107 equal) |

Why zero patches:

* The driver is consumed as mainline wrote it. Everything this project needs that the
  driver does not carry - the mac80211/cfg80211 header face, the firmware registry, the
  USB class hook, the net80211 bridge - lives in first-party translation units and touches
  the driver only through header interfaces, so no upstream byte has to move.
* Two predecessor workarounds are known but deliberately not carried as patches this
  round: un-staticing the `usb_driver` object in `rtw8851bu.c` (the USB entry must be
  registered explicitly when the port grows an entry unit) and a submit-failure cleanup
  pairing in `usb.c` (the host USB engine is this project's own implementation and its
  completion semantics are re-proven by host tests rather than assumed from the
  predecessor's finding). Each becomes a patch here only when a first-party consumer
  needs it, written in `git format-patch` form.
* If a real defect in the driver turns out to block the port, the fix is written here in
  `git format-patch` form (English, one concern, `Signed-off-by`) so it can be sent to the
  driver's maintainers; the extracted bytes stay untouched.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/rtw89.provenance.md`.
