# third-party/rtl8xxxu - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtl8xxxu.git (branch `linux-7.3-rc2`) - an orphan export of the mainline driver directory, created for this project so the driver can be pinned as a submodule and diffed against nothing |
| Pin | commit `639697082494da0d1063a6259a9659c3e3789246` (branch `linux-7.3-rc2` tip; the branch's first commit is the export, the second adds a README) |
| License | **GPL-2.0-only** - every extracted file carries `SPDX-License-Identifier: GPL-2.0-only` as its first line (13/13, no second SPDX form in the tree). Copyright stays with the driver's authors. Distribution obligations: see `LICENSES.md` |
| Vendoring | git submodule; the gitlink is locked to the commit above and the tree is consumed byte-identical. `.gitmodules` records `branch = linux-7.3-rc2` on purpose: the fork's other branch (`main`) is a separate out-of-tree backport generation, and an unpinned `submodule update --remote` must not be able to jump generations silently |
| Fetched | 2026-10-08 from https://github.com/KevinACoder/rtl8xxxu.git (`git ls-remote ... refs/heads/linux-7.3-rc2` returns the pin) |
| Local differences | none in the tree (zero patch; `patches/rtl8xxxu/` is empty and `tools/patches/replay.sh` prints the vendored tree itself). On the branch, one file is the project's own rather than an extracted byte: `README.md`, which records the source commit, the out-of-tree build command and the license line. No build shell (`Makefile`/`Kbuild`) was added: a second build file would shadow the extracted `Makefile`, whose module list is used as-is by passing `CONFIG_RTL8XXXU=m` on the make command line |
| Compiled set | `core.c` and `8188e.c` - the RTL8188EU translation units - are compiled by the host static gate (`tests/host/rtl8xxxu-port-build`) against the first-party header face under `components/linuxkpi/include/`. The other seven chip units are carried byte-identical but not compiled this round; the link closure (which needs those units' `fileops` symbols, plus the mac80211 face bodies) belongs to the bridge round |
| Consumers | the first-party mac80211 header face and the host compile gate; later the USB class hook, the net80211 bridge and the board image |
| Reason | the wireless milestone starts from a mainline Linux SoftMAC driver instead of a vendor or BSD tree: mainline is the most mature of the three, and the project already carries the linuxkpi surface such a driver talks to. Pinning the extracted directory as a submodule keeps the copyleft unit byte-auditable and separate from first-party units (different translation units, header interfaces only) |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/realtek/rtl8xxxu/`. The extraction copied the thirteen files
  without changing a byte; their digests are kept in `patches/rtl8xxxu/MANIFEST.sha256`
  and were measured twice - in the source kernel tree at extraction time and in this
  checkout afterwards.
* The branch's layout is flat (sources at the tree root), the same layout as the fork's
  `main` generation, so the driver's quoted includes resolve in place; no include path
  needs to be configured for the host gate.
* Why the export exists at all: diffing a driver against a kernel checkout makes the
  import unreviewable and unstable; a dedicated branch whose first commit is exactly the
  extracted directory can be pinned, manifested and patched like any other vendored tree.
* RTL8188EU is reachable from this tree without any patch: the driver's id table matches
  the device's vendor-specific interface directly (no mode switch), the 8188e unit
  rejects cut-I silicon on its own, and the firmware file the driver asks for is loaded
  through the project's firmware registry rather than a filesystem.