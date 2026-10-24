# patches/iwlwifi - index

Local patches replayed onto the vendored iwlwifi tree
(`third-party/iwlwifi/`). The vendored tree itself is never edited: every
difference would live here and be applied at build time.

| Item | Value |
|---|---|
| Upstream | mainline Linux `drivers/net/wireless/intel/iwlwifi/` |
| Base (= pin) | `e71d9a28760f605082308c2e1114627d406b4a29` |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design |

Why zero patches:

* The driver is consumed as mainline wrote it. Everything this project needs
  that the driver does not carry - the PCI/DMA/interrupt surface, the
  mac80211/cfg80211 header face, the net80211 bridge, the module entry and the
  PCI registration glue - lives in first-party translation units and touches
  the driver only through headers.
* The units the 7260 path does not reach are excluded by **not compiling**
  them, not by editing them: the kernelmod build lists its translation units
  explicitly (`tests/board/linux-ko/build.sh`, `DRIVER=iwlwifi`).
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
  both in `third-party/iwlwifi.provenance.md`.
