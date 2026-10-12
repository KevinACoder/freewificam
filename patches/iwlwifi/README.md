# patches/iwlwifi - index

Local patches replayed onto the vendored iwlwifi submodule
(`third-party/iwlwifi/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by
`tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/iwmwifi.git (branch `linux-7.3-rc2`) |
| Base (= pin) | `ff5d8ea02690a7dac276f4b0c07cbf357b40de71` (branch tip: the single export commit) |
| Patch list | (empty) |
| Submission status | no patches to submit - the import is zero-patch by design |
| Digests | `MANIFEST.sha256` - `sha256` of every tracked file, measured in this checkout (294/294 verify) |

Why zero patches:

* The driver is consumed as mainline wrote it. Everything this project needs that the
  driver does not carry - the PCI/DMA header face, the interrupt-request surface, the
  mac80211/cfg80211 header face, the firmware registry, the net80211 bridge - lives in
  first-party translation units and touches the driver only through headers.
* The units the 7260 path does not reach are excluded by not compiling them, not by
  editing them: the host gate lists its 63 translation units explicitly
  (`tests/host/iwlwifi-port-build`), and the board image compiles the same list.
* A defect that turns out to need a driver change is written here as a
  `git format-patch`-style patch and replayed at build time - never as an edit to the
  submodule work tree.
