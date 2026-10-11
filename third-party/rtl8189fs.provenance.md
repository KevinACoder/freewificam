# third-party/rtl8189fs - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/rtl8189ES_linux.git (branch `rtl8189fs`) - the fork named for this driver line. The same commit is also the tip of `refs/heads/rtl8189fs` on the upstream `jwrdegoede/rtl8189ES_linux`. The fork's `master` branch is the older RTL8189E generation (`hal/` carries only `rtl8188e`, `Kconfig` names `RTL8189ES`) and cannot drive this part |
| Pin | commit `13bbdbc750bfaa37ff47dff063edcc857fad0584` (branch `rtl8189fs` tip). Unlike the rtw88/rtw89 exports this is the fork's own history, not an orphan single commit |
| License | **GPL-2.0-only**. The tree carries no SPDX line anywhere and no separate license file; every one of the 518 driver `.c`/`.h` files states the Realtek "version 2 of the GNU General Public License" block at the top. Two files are kernel-derived GPL-2.0 (`os_dep/linux/rhashtable.{c,h}`, Herbert Xu / Thomas Graf / Patrick McHardy), two carry no license header (`hal/phydm/phydm_regtable.h`, `include/rtw_version.h`) and ten are build metadata or scripts (`.github/workflows/build.yml`, `Kconfig`, `Makefile`, `clean`, `ifcfg-wlan0`, `runwpa`, `wlan0dhcp`, `hal/phydm/{ap_makefile,phydm,sd4_phydm_2_kernel}.mk`). No second license form is present. The tree contains ISO-8859 bytes, so the license scan must use `grep -a`. Distribution obligations: see `LICENSES.md` |
| Vendoring | git submodule; the gitlink is locked to the commit above and the tree is consumed byte-identical. `.gitmodules` records `branch = rtl8189fs` on purpose: the fork's `master` is the older RTL8189E generation and an unpinned `submodule update --remote` must not be able to jump to it |
| Fetched | 2026-10-12 from https://github.com/KevinACoder/rtl8189ES_linux.git (`git ls-remote ... refs/heads/rtl8189fs` returns the pin) |
| Local differences | none in the tree (zero patch; `patches/rtl8189fs/` carries only its index and the digest manifest, and `tools/patches/replay.sh` prints the vendored tree itself). No build shell was added: the tree's own `Makefile` is used as-is by passing the configuration (`CONFIG_RTL8188F` and the SDIO bus selection) on the make command line |
| Compiled set | 131 translation units - 37 `core/` (including the three `core/mesh/` units and `core/rtw_mp.c`), 16 `os_dep/` (one shared plus fifteen Linux-specific), 29 `hal/` (chip, HCI, LED and eFuse units), 48 `hal/phydm/` (including the three `txbf/` units and the two `rtl8188f/` register-image units) and 1 `platform/` - are compiled by the host static gate (`tests/host/rtl8189fs-port-build`) against the first-party cfg80211/netdev/WEXT face and the linuxkpi SDIO engine. The set is the ground-truth module link table `8189fs.mod` of the lab's own build of this pin |
| Consumers | the first-party cfg80211/netdev/WEXT/osdep face, the linuxkpi SDIO engine (`components/linuxkpi/sdio/`), the host compile gate, and later the board image |
| Reason | the RTL8189FTV (RTL8188F) is the SDIO wireless line and the first driver in this repository to reach the chip over SDIO rather than USB. The part's Linux support lives only out of tree, so the vendor driver is the only way to drive it; the driver is a vendor FullMAC stack with its own MLME and a cfg80211/WEXT consumption face, the same species as the predecessor project's 8821cu vendor line. Pinning the tree as a submodule keeps the GPL unit byte-auditable and separate from first-party units (different translation units, header interfaces only) |

Notes:

* The driver is byte-faithful to the pin: all 532 tracked files were compared against the
  lab's own checkout of the same commit - their git blob identities match file by file
  (532/532, zero differences) and their `sha256` digests, measured in this checkout, equal
  the digests of the same files in that tree (532/532). The manifest is
  `patches/rtl8189fs/MANIFEST.sha256`.
* The firmware is not a separate blob: the chip's FW_NIC image (20,306 bytes) is an
  embedded C array (`hal/rtl8188f/hal8188f_fw.{h,c}`), so no filesystem, registry or
  binary-firmware obligation follows from this tree.
* Why zero patches: the driver is consumed as the vendor wrote it. Everything this project
  needs that the driver does not carry - the cfg80211/netdev/WEXT header face, the SDIO
  engine, the runtime cfg80211 substitute - lives in first-party translation units and
  touches the driver only through header interfaces.
* The chip is reached over SDIO (function 1 of the `024c:f179` card). The bus itself, the
  card service and the power-on sequence are first-party work in
  `components/linuxkpi/sdio/` and `ports/fsl_sdmmc/`; this tree supplies only the driver's
  own HCI, HAL and MLME.
