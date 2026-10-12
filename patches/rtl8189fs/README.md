# patches/rtl8189fs - index

Local patches replayed onto the vendored rtl8189fs submodule
(`third-party/rtl8189fs/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by
`tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtl8189ES_linux.git (branch `rtl8189fs`) |
| Base (= pin) | `13bbdbc750bfaa37ff47dff063edcc857fad0584` (branch tip) |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design |
| Digests | `MANIFEST.sha256` - `sha256` of every tracked file, measured in this checkout and cross-checked against the lab's own checkout of the same commit (532/532 equal) |

Why zero patches:

* The driver is consumed as the vendor wrote it. Everything this project needs that the
  driver does not carry - the cfg80211/netdev/WEXT header face, the SDIO engine, the
  runtime cfg80211 substitute, the netif and wpa docking - lives in first-party translation
  units and touches the driver only through header interfaces, so no upstream byte has to
  move.
* The driver's own build configuration is passed on the make command line (the Kconfig in
  the tree is vestigial - it still declares a USB dependency - so it is not used), which is
  how the compiled set is selected without editing a byte.
* If a real defect in the driver turns out to block the port, the fix is written here in
  `git format-patch` form (English, one concern, `Signed-off-by`) so it can be sent to the
  driver's maintainers; the vendored bytes stay untouched.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/rtl8189fs.provenance.md`.
