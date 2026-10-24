# patches/rtl8xxxu - index

Local patches replayed onto the vendored rtl8xxxu tree
(`third-party/rtl8xxxu/`). The vendored tree itself is never edited: every
difference would live here and be applied at build time.

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/realtek/rtl8xxxu/` |
| Base (= pin) | `e71d9a28760f605082308c2e1114627d406b4a29` |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design |

Why zero patches:

* The driver is consumed as mainline wrote it. Everything this project needs
  that the driver does not carry - the mac80211/cfg80211 header face, the
  net80211 bridge, the module entry and the USB registration glue - lives in
  first-party translation units and touches the driver only through header
  interfaces, so no upstream byte has to move.
* The link closure is a build-set question, not a source question: `core.c`
  references the other chip units' `fileops` symbols, so the module compiles
  all nine `.c` files rather than trimming the id table.
* If a real defect in the driver turns out to block the port, the fix is
  written here in `git format-patch` form (English, one concern,
  `Signed-off-by`) so it can be sent to the driver's maintainers; the
  extracted bytes stay untouched.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/rtl8xxxu.provenance.md`.
