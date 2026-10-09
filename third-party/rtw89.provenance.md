# third-party/rtw89 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtw89.git (branch `linux-7.3-rc2`) - an orphan export of the mainline driver directory, created so the driver can be pinned as a submodule and diffed against nothing. The branch was pushed to the fork on 2026-10-09 for this project; before that it existed only in local checkouts |
| Pin | commit `79e12cc959d80b20f9b8499f885faf62cee1ade1` (branch `linux-7.3-rc2` tip; the branch is a single commit - `git rev-list --count HEAD` is 1 and the commit has no parent) |
| License | **GPL-2.0 OR BSD-3-Clause** - all 107 files carry that single SPDX form as their first line (61 `.c` + 44 `.h` + `Kconfig` + `Makefile`), and the tree root carries no separate license file. Each bus unit states `MODULE_LICENSE("Dual BSD/GPL")`. This project takes the **BSD-3-Clause option** (project ruling for the rtw88/rtw89 family); copyright stays with the driver's authors. Distribution obligations: see `LICENSES.md` |
| Vendoring | git submodule; the gitlink is locked to the commit above and the tree is consumed byte-identical. `.gitmodules` records `branch = linux-7.3-rc2` on purpose: the fork's `main` branch is the older lwfinger out-of-tree backport generation (no USB surface) and an unpinned `submodule update --remote` must not be able to jump generations silently |
| Fetched | 2026-10-09 from https://github.com/KevinACoder/rtw89.git (`git ls-remote ... refs/heads/linux-7.3-rc2` returns the pin) |
| Local differences | none in the tree (zero patch; `patches/rtw89/` carries only its index and the digest manifest, and `tools/patches/replay.sh` prints the vendored tree itself). No build shell was added: the extracted `Makefile` is used as-is by passing the configuration (`CONFIG_RTW89=m` and the chip/bus selection) on the make command line |
| Compiled set | 21 translation units - 15 core units (`core.c`, `mac80211.c`, `mac.c`, `phy.c`, `fw.c`, `cam.c`, `efuse.c`, `regd.c`, `sar.c`, `coex.c`, `ps.c`, `chan.c`, `ser.c`, `acpi.c`, `util.c`), 4 chip units (`rtw8851b.c`, `rtw8851b_table.c`, `rtw8851b_rfk.c`, `rtw8851b_rfk_table.c`) and 2 bus units (`usb.c`, `rtw8851bu.c`) - are compiled by the host static gate (`tests/host/rtw89-port-build`) against the first-party mac80211 header face. They come to 86,396 lines. The remaining files are carried byte-identical but not compiled: the PCI bus units, the Wi-Fi 7 (BE generation) units, the other chips' units, and the power-management/debug/LED units the chip selection does not reach |
| Consumers | the first-party mac80211 header face and the host compile gate; later the USB class hook, the net80211 bridge and the board image |
| Reason | the 8851BU is the wireless milestone's second driver line and the project's first one whose hardware is in hand. rtw89 is the mainline SoftMAC driver for this family, so it talks to the mac80211 surface the project already grew for rtl8xxxu and reaches net80211 through the same seam; pinning the extracted directory as a submodule keeps the copyleft unit byte-auditable and separate from first-party units (different translation units, header interfaces only) |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/realtek/rtw89/`. All 107 files were compared twice: their git
  blob identities match that kernel tree's objects file by file (107/107, zero
  differences), and their `sha256` digests, measured in this checkout, equal the digests
  of the same kernel tree's blobs (107/107). The manifest is `patches/rtw89/MANIFEST.sha256`.
* The branch's layout is flat (sources at the tree root), the same layout as the fork's
  `main` generation, so the driver's quoted includes resolve in place; no include path
  needs to be configured for the host gate.
* Why the export exists at all: diffing a driver against a kernel checkout makes the
  import unreviewable and unstable; a dedicated branch whose commit is exactly the
  extracted directory can be pinned, manifested and patched like any other vendored tree.
* Line-count erratum: the 21-unit subset measures 86,396 lines here and the whole tree
  312,813. Earlier notes recorded 86,414 / 312,831. The difference is exactly 18 lines and
  comes from the predecessor checkout those notes were taken on, which carried two
  uncommitted work-in-progress edits (one in `rtw8851bu.c`, one in `usb.c`) that are not
  part of this branch. The numbers above are the branch's own.
* RTL8851BU is reachable from this tree without any patch: the driver's id table matches
  the device's vendor-specific interface directly (no mode switch), and the firmware file
  the driver asks for is loaded through the project's firmware registry rather than a
  filesystem.
