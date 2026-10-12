# third-party/iwlwifi - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/iwmwifi.git (branch `linux-7.3-rc2`) - an orphan export of the mainline driver directory, created so the driver can be pinned as a submodule and diffed against nothing. The branch was pushed to the fork on 2026-10-12 for this project. Note the repository name (`iwmwifi`) differs from the driver name (`iwlwifi`) by the project owner's choice; the URL is the authority |
| Pin | commit `ff5d8ea02690a7dac276f4b0c07cbf357b40de71` (branch `linux-7.3-rc2` tip; the branch is a single commit - `git rev-list --count HEAD` is 1 and the commit has no parent) |
| License | mixed, and the most copyleft-heavy unit in the tree: 241 files carry `SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause`, 48 carry `GPL-2.0-only`, 3 carry plain `GPL-2.0` and 1 carries no SPDX line (measured in this checkout). The `GPL-2.0-only` group is not avoidable by electing BSD per file: it includes `mvm/rs.{c,h}` and `iwl-debug.h`, which the mvm path this project compiles consumes. The whole tree is therefore treated as **GPL-2.0-only** under the project's three-condition rule (see `LICENSES.md`), and `MODULE_LICENSE("GPL")` is what the driver states. Copyright stays with the driver's authors |
| Vendoring | git submodule; the gitlink is locked to the commit above and the tree is consumed byte-identical. `.gitmodules` records `branch = linux-7.3-rc2` so an unpinned `submodule update --remote` cannot jump generations silently |
| Fetched | 2026-10-12 from https://github.com/KevinACoder/iwmwifi.git (`git ls-remote ... refs/heads/linux-7.3-rc2` returns the pin); the export was made in this session from `~/workspace/rk3568_lab/os/linux-upstream/drivers/net/wireless/intel/iwlwifi/` at `e71d9a28760f` and pushed as a single orphan commit |
| Local differences | none in the tree (zero patch; `patches/iwlwifi/` carries only its index and the digest manifest, and `tools/patches/replay.sh` prints the vendored tree itself). No build shell was added: the extracted `Makefile`/`mvm/Makefile` are used as the translation-unit authority, and the host gate lists the 63 units explicitly |
| Compiled set | 63 translation units - 9 core (`iwl-drv.c`, `iwl-debug.c`, `iwl-io.c`, `iwl-nvm-parse.c`, `iwl-nvm-utils.c`, `iwl-phy-db.c`, `iwl-trans.c`, `iwl-utils.c`, `iwl-dbg-tlv.c`), 11 firmware (`fw/{img,notif-wait,rs,dbg,dbg-old,pnvm,dump,regulatory,paging,smem,init}.c`), 9 PCIe (`pcie/{ctxt-info,ctxt-info-v2,drv,utils}.c` and `pcie/gen1_2/{rx,tx,trans,trans-gen2,tx-gen2}.c`), 1 configuration (`cfg/7000.c`, the AC 7260 family) and 33 mvm (`mvm/*.c` minus the DEBUGFS/LEDS/PM_SLEEP/IWLMEI-conditional units). They are compiled by the host static gate (`tests/host/iwlwifi-port-build`) against the first-party linuxkpi PCI/DMA/mac80211 face. The remaining files are carried byte-identical but not compiled: the `dvm/` generation, `mei/`, the `tests/` units, the non-7260 configuration families and the conditional mvm units |
| Consumers | the first-party linuxkpi PCI/DMA header face and the PCI engine (through the backend seam), the mac80211 header face and the net80211 bridge; later the board image `threadx-rk3568-iwlwifi` |
| Reason | the AC 7260 (Intel Wireless 7260, `8086:08b1`) is the wireless milestone's PCIe driver line and the first consumer of the linuxkpi PCI face. iwlwifi is the mainline driver for this family, so it talks to the mac80211 surface the project already grew for the USB and SDIO lines and reaches net80211 through the same bridge; pinning the extracted directory as a submodule keeps the copyleft unit byte-auditable and separate from first-party units (different translation units, header interfaces only) |

Notes:

* The driver is byte-faithful to mainline Linux 7.3.0-rc2, commit
  `e71d9a28760f605082308c2e1114627d406b4a29`, directory
  `drivers/net/wireless/intel/iwlwifi/`. The export was compared twice: a recursive
  `diff -r --exclude=.git` against the source directory is empty, and the
  `sha256` digests of all 294 tracked files, measured in this checkout, are recorded in
  `patches/iwlwifi/MANIFEST.sha256` (294/294 verify).
* The branch's layout is flat (the driver directory's contents at the tree root), the same
  layout as the fork's other driver exports, so the driver's quoted includes resolve in
  place.
* Why the export exists at all: diffing a driver against a kernel checkout makes the
  import unreviewable and unstable; a dedicated branch whose commit is exactly the
  extracted directory can be pinned, manifested and patched like any other vendored tree.
* The 7260 reaches its firmware through the project's firmware registry rather than a
  filesystem, and the file it asks for is `iwlwifi-7260-17.ucode`; the registry entry and
  the blob land with the runtime round, and the binary's redistribution terms are kept at
  `components/linuxkpi/fw/iwl7260_fw.LICENCE.txt`.
