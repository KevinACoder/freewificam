# third-party/rtw88 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtw88.git (branch `linux-7.3-rc2`) - an orphan export of the mainline driver directory, created so the driver can be pinned as a submodule and diffed against nothing. The branch was first pushed to the fork by the predecessor project on 2026-10-05; that snapshot carried forensics prints and was replaced this round (see "Retired snapshot" below) |
| Pin | commit `8fcd0b704502fe7c30ee1c5404d6da4bd5c87b37` (branch `linux-7.3-rc2` tip; the branch is a single commit - `git rev-list --count HEAD` is 1 and the commit has no parent) |
| License | **GPL-2.0 OR BSD-3-Clause** - 96 `.c`/`.h` files carry that single SPDX form as their first line (56 `//` form + 40 `/* */` form) and the `Makefile` carries it too, so 97 of the 98 tracked files are dual-licensed. The one exception is `Kconfig`, whose first line is `# SPDX-License-Identifier: GPL-2.0-only`; it is build metadata and is not compiled. The tree root carries no separate license file, and each bus unit states `MODULE_LICENSE("Dual BSD/GPL")`. This project takes the **BSD-3-Clause option** (project ruling for the rtw88/rtw89 family); copyright stays with the driver's authors. Distribution obligations: see `LICENSES.md` |
| Vendoring | git submodule; the gitlink is locked to the commit above and the tree is consumed byte-identical. `.gitmodules` records `branch = linux-7.3-rc2` on purpose: the fork's `master` branch is the older lwfinger out-of-tree backport generation and an unpinned `submodule update --remote` must not be able to jump generations silently |
| Fetched | 2026-10-11 from https://github.com/KevinACoder/rtw88.git (`git ls-remote ... refs/heads/linux-7.3-rc2` returns the pin) |
| Local differences | none in the tree (zero patch; `patches/rtw88/` carries only its index and the digest manifest, and `tools/patches/replay.sh` prints the vendored tree itself). No build shell was added: the extracted `Makefile` is used as-is by passing the configuration (`CONFIG_RTW88_CORE` and the chip/bus selection) on the make command line |
| Compiled set | 20 translation units - 16 core units (`main.c`, `mac80211.c`, `util.c`, `debug.c`, `tx.c`, `rx.c`, `mac.c`, `phy.c`, `coex.c`, `efuse.c`, `fw.c`, `ps.c`, `sec.c`, `bf.c`, `sar.c`, `regd.c`), 3 chip units (`rtw8821c.c`, `rtw8821c_table.c`, `rtw8821cu.c`) and 1 bus unit (`usb.c`) - are compiled by the host static gate (`tests/host/rtw88-port-build`) against the first-party mac80211 header face. They come to 29,746 lines. The remaining files are carried byte-identical but not compiled: the PCI and SDIO bus units, the other chips' units, and the shared `rtw8723x` unit the RTL8821C selection does not pull in |
| Consumers | the first-party mac80211 header face and the host compile gate; later the USB class hook (mode switch), the net80211 bridge and the board image |
| Reason | the 8821CU is the wireless milestone's third driver line and the one with the thickest RTOS board evidence from the predecessor project (it reached WPA2/DHCP/ping on the RTOS side). rtw88 is the mainline SoftMAC driver for this family, so it talks to the mac80211 surface the project already grew for rtl8xxxu and rtw89 and reaches net80211 through the same seam; pinning the extracted directory as a submodule keeps the copyleft unit byte-auditable and separate from first-party units (different translation units, header interfaces only) |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/realtek/rtw88/`. All 98 files were compared twice: their git
  blob identities match that kernel tree's objects file by file (98/98, zero
  differences), and their `sha256` digests, measured in this checkout, equal the digests
  of the same kernel tree's blobs (98/98). The manifest is `patches/rtw88/MANIFEST.sha256`.
* Retired snapshot: the branch's previous tip was
  `b057bfd807f3341a3289e22f345503089ebd554f` (also an orphan single commit, pushed
  2026-10-05). It was **not** verbatim: five files (`fw.c`, `mac.c`, `mac80211.c`, `tx.c`,
  `usb.c`) carried 118 lines of the predecessor's forensics prints, because that import was
  taken from a working tree rather than from git objects. The predecessor's own commit
  message claimed "verbatim", which the bytes contradicted. This round re-imported the
  directory from the kernel tree's git blobs and force-updated the same branch name, so the
  five files are back to mainline; the old commit remains in the fork's object store and in
  the predecessor backup and is not used as a pin.
* The branch's layout is flat (sources at the tree root), the same layout as the fork's
  `master` generation, so the driver's quoted includes resolve in place; no include path
  needs to be configured for the host gate.
* Why the export exists at all: diffing a driver against a kernel checkout makes the
  import unreviewable and unstable; a dedicated branch whose commit is exactly the
  extracted directory can be pinned, manifested and patched like any other vendored tree.
* The 8821CU is reachable from this tree only after a mode switch: the dongle powers up as
  a CD-ROM (`0bda:1a2b`, interface 08/05/50) and only exposes the driver's vendor-specific
  interface (`0bda:c820`, 08/ff/ff/ff) after an eject. The driver's id table matches
  `0bda:c820` directly and carries no mode-switch handling; that belongs to the board
  round's USB class hook, not to the vendored bytes.
