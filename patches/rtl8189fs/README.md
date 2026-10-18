# patches/rtl8189fs - index

Local patches replayed onto the vendored rtl8189fs submodule
(`third-party/rtl8189fs/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by
`tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtl8189ES_linux.git (branch `rtl8189fs`) |
| Base (= pin) | `13bbdbc750bfaa37ff47dff063edcc857fad0584` (branch tip) |
| Patch list | `0001-sdio-xmit-free-a-frame-whose-coalesce-fails.patch` (transmit frame + skb leaked when a coalesce fails after the first frame of a buffer) · `0002-sdio-recv-drop-the-frame-when-the-copy-fails.patch` (the clone fallback under `CONFIG_SDIO_RX_COPY` hands the whole read buffer to the stack and its bytes change under it) · `0003-sdio-recv-bound-the-retry-loop-holding-the-claim.patch` (`sd_recv` retried forever when no read had yet succeeded, with the host claim held) |
| Submission status | not submitted; all three are candidate upstream fixes (one concern each) |
| Digests | `MANIFEST.sha256` - `sha256` of every tracked file, measured in this checkout and cross-checked against the lab's own checkout of the same commit (532/532 equal) |

Why the pin itself is otherwise unpatched:

* The driver is consumed as the vendor wrote it. Everything this project needs that the
  driver does not carry - the cfg80211/netdev/WEXT header face, the SDIO engine, the
  runtime cfg80211 substitute, the netif and wpa docking - lives in first-party translation
  units and touches the driver only through header interfaces, so no upstream byte has to
  move for the port to exist.
* The driver's own build configuration is passed on the make command line (the Kconfig in
  the tree is vestigial - it still declares a USB dependency - so it is not used), which is
  how the compiled set is selected without editing a byte.
* The three patches above fix defects in the driver itself, found by the 8189 throughput
  round; they are written in `git format-patch` form (English, one concern,
  `Signed-off-by`) so they can be sent to the driver's maintainers as-is.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/rtl8189fs.provenance.md`.
