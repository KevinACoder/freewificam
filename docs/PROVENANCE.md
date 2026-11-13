# PROVENANCE

Files in this repository are either **written here** or **imported**. An
imported file gets a row in the table below and one line in
`IMPORT-MANIFEST.tsv` (source commit, path, blob hash).

## The import

| | |
|---|---|
| Source repository | the previous line's `freewificam`, branch `master` |
| Source commit | `fdceccb95dc85c70229709973ffcd150e643c7cd` |
| Method | per-file `cp` from a clean working tree at that commit; every file's blob hash equals the source commit's blob (`IMPORT-MANIFEST.tsv`) |
| Direction | one way (see `LICENSES.md`) |

The build files are the previous tree's as a blueprint, then trimmed to the
worlds this line carries; every edit they received is in `DIVERGENCES.md`.

## What was imported

| Destination | Source path | Files | License | Byte-identical |
|---|---|---|---|---|
| `tests/board/rtos/platform/arch/aarch64/` | same | 17 | BSD-3-Clause | yes |
| `tests/board/rtos/platform/board/rk3568-itx/` | same | 43 | BSD-3-Clause | yes |
| `tests/board/rtos/platform/os/{freertos,baremetal,cmsis2}/` | same | 23 | BSD-3-Clause | yes |
| `tests/board/rtos/platform/{build_defs.h,*/sources.mk,board/Kconfig}` | same | 6 | BSD-3-Clause | `build_defs.h`, `board/Kconfig` edited |
| `tests/board/rtos/app/` | same, case subset | 22 | BSD-3-Clause | `cases.h`, `runner.c`, `Kconfig` edited |
| `tests/board/rtos/{Kconfig,config.mk}` | same | 2 | BSD-3-Clause | both edited |
| `tests/board/rtos/build/` | same, minus `wlan.mk`, `iwlwifi.mk`, `rtw88*.mk`, `rtl8xxxu.mk`, `rtw89.mk`, `aic8800.mk`, `uvc.mk`, `sdio_mci.mk` | 3 | BSD-3-Clause | `Makefile` rewritten (trim), `usb.mk`/`fw.mk` byte-identical |
| `tools/build/` | same, minus the CMake/parity/SDK tools | 11 | BSD-3-Clause | 8 edited or added |
| `tools/kconfig/` | same | 5 | ISC | yes |
| `tools/rtos/judge.py` | same | 1 | BSD-3-Clause | yes |
| `third-party/FreeRTOS-Kernel/` | same, compiled closure | 33 | MIT | yes |
| `third-party/CMSIS_6/` | same, RTOS2/Core/Driver headers | 8 | Apache-2.0 | yes |
| `third-party/CMSIS-FreeRTOS/` | same, `CMSIS/RTOS2/FreeRTOS` | 5 | Apache-2.0 AND MIT | yes |
| `third-party/lwip/` | same, `src/` + `ports/` + `patches/` | 327 | BSD-3-Clause style | yes (the tree carries its patches applied) |
| `third-party/cherryusb/` | same, `core/ class/{hub,msc}/ port/{xhci,ehci}/ common/ osal/ ports/osal/cmsis2/ patches/` | 66 | Apache-2.0 | yes (patches applied) |
| `third-party/fsl_sdmmc/` | same, whole tree | 27 | BSD-3-Clause | yes |
| `third-party/{printf,tlsf,ulog}/` | same | 17 | MIT / BSD-3 / MIT | yes |
| `LICENSE` | same | 1 | BSD-3-Clause | yes |
| `make.rtos`, `Makefile` | same | 2 | BSD-3-Clause | both edited |

`third-party/*.provenance.md` travel with their trees and carry the upstream
pin, the patch records and the license; they are the authority for the trees,
this table is the summary.

## The liblinux80211 component import (2026-11-10)

The kernel-API layer and the PCI/SDIO engines the mac80211 line needs were
imported from the same source commit, into `components/liblinux80211/`.

| Destination | Source path | Files | Byte-identical |
|---|---|---|---|
| `components/liblinux80211/include/` | `components/linuxkpi/rtos/include/` | 154 | yes |
| `components/liblinux80211/{linuxkpi_core,linuxkpi_compat,linuxkpi_osdep,linuxkpi_netdev,linuxkpi_platform_target}.c`, `linuxkpi_impl.h`, `linuxkpi_txq_audit.h`, `lkpi_census.h` | `components/linuxkpi/rtos/` (same names) | 8 | yes |
| `components/liblinux80211/skbuff/skbuff_pool.{c,h}` | `components/linuxkpi/rtos/skbuff/` | 2 | yes |
| `components/liblinux80211/bus/pci.c` | `components/linuxkpi/rtos/pci/linuxkpi_pci.c` | 1 | yes (renamed) |
| `components/liblinux80211/bus/linuxkpi_pci.h` | `components/linuxkpi/rtos/pci/linuxkpi_pci.h` | 1 | yes |
| `components/liblinux80211/bus/sdio.c`, `sdio_be.c` | `components/linuxkpi/rtos/sdio/linuxkpi_sdio.c`, `lkpi_sdio_be.c` | 2 | yes (renamed) |
| `components/liblinux80211/bus/linuxkpi_sdio.h` | `components/linuxkpi/rtos/sdio/linuxkpi_sdio.h` | 1 | yes |
| `components/liblinux80211/include/linux80211/lkpi_{pcie,sdio}_backend.h`, `lkpi_sdio_rx_probe.h` | `components/linuxkpi/rtos/{pci,sdio}/` | 3 | yes |
| `components/liblinux80211/ports/pci-rk3568/adapter.{c,h}`, `SOURCE.md` | `components/linuxkpi/rtos/pci/lkpi_pcie_port.*` | 3 | yes |

The file names that the Linux face reaches with a relative include
(`../../linuxkpi_core.h`, `skbuff/skbuff_pool.h`) keep their names and their
depth: the core header sits beside `include/`, and the skb pool stays under
`skbuff/`. Renames are limited to the two engine bodies and their headers.

Written here (not imported): the component's `Kconfig`, `sources.mk`,
`README.md`, `include/linux80211/{linux80211.h,net.h}`, `net_egress.c`, the
`kpi`/`kpi_live` cases, the two profiles and `tests/board/rtos/build/kpi.mk`.

## What was deliberately not imported

| Not imported | Why |
|---|---|
| `components/net80211/` | this line replaces the stack with mac80211; the net80211 line stays in the previous repository as the control arm |
| `components/linuxkpi/` | its mac80211/cfg80211 substitutes (`linuxkpi_80211*.c`, `linuxkpi_txq.c`, `linuxkpi_wiphy_work.c`, `linuxkpi_cfg80211.c`) would collide with the real stack. The driver-facing part lands in `components/liblinux80211/` with the mac80211 work |
| `wireless-drivers/*` | no driver in this round's images; they land with `liblinux80211` |
| `firmware/` | no driver to feed; the firmware blobs land with their drivers |
| `components/shell/`, `third-party/cherrysh/` | the console shell is not part of the four acceptance images |
| firefly/ThreadX/Zephyr platform directories | this line targets RK3568 + FreeRTOS; the CMSIS-RTOS2 facade stays so another backend is additive |
| `tests/board/linux-ko/`, `dist/`, the CMake entry | not part of this round (see `STATUS.md`) |

## Rules

1. An import records source, commit, path, license and whether the bytes match.
2. Byte-identical files do not appear in `DIVERGENCES.md`. Any edit does.
3. Imports move in one direction only; see `LICENSES.md`.


## The wireless-stack import (2026-11-10, S0)

The Linux stack itself, taken verbatim from the GT kernel checkout at
`63a2a8ab9cd31a9821f5c86c05cbb0be88b82e7b` (`docs/MANIFEST.md` pins the tree):

| Destination | Source | Files | License | Byte-identical |
|---|---|---|---|---|
| `components/liblinux80211/stack/mac80211/` | `linux-upstream/net/mac80211/` | 44 `.c` (42 `mac80211-y` + `wbrf.c` + `rc80211_minstrel_ht.c`) + 26 `.h` | GPL-2.0-only | yes |
| `components/liblinux80211/stack/wireless/` | `linux-upstream/net/wireless/` | 18 `.c` (`cfg80211-y`, incl. `nl80211.c`) + 8 `.h` | GPL-2.0-only (`radiotap.c` is GPL-2.0 OR BSD-3-Clause upstream; the BSD arm is not taken here) | yes |
| `components/liblinux80211/include/net/{mac80211,cfg80211,ieee80211_radiotap}.h` | `linux-upstream/include/net/` | 3 | GPL-2.0-only | yes; **replaces** the old driver-view faces |
| `components/liblinux80211/include/linux/ieee80211.h` + 9 `ieee80211-*.h` | `linux-upstream/include/linux/` | 10 | GPL-2.0-only | yes; replaces the old face |
| `components/liblinux80211/include/uapi/linux/{nl80211,rfkill,netlink,const,limits,sysinfo,if_vlan,if_ether,ethtool,pkt_sched,pkt_cls,kernel}.h` | `linux-upstream/include/uapi/linux/` | 12 | GPL-2.0-only with syscall exception / dual as upstream states | yes |
| `components/liblinux80211/include/{net,linux,crypto,keys,kunit,asm}/…` | `linux-upstream/include/…` | the 42-header batch + its direct closure | GPL-2.0-only | yes; the face headers written for this port are BSD-3-Clause and listed in DIVERGENCES |

`components/liblinux80211/stack/IMPORT.tsv` is the per-file record (source path,
destination, sha256, lines, kind, SPDX). `tools/build/import-stack.py --check`
re-derives it and fails on drift; the S0 round used it before every compile run.

**License consequence**: the component now contains GPL-2.0-only sources. Its own
code stays BSD-3-Clause, but every artifact that links the stack is GPL-2.0
(`docs/LICENSES.md` §3). The stack subtree is not linked into any image by this
build; S1 adds the link behind its own profile.

## The S1a face closure (2026-11-10, S0 run-20..run-50)

S1a closed the 62-TU compile with **zero changes under `stack/`**
(`import-stack.py --check`: 62 TUs + 34 headers byte-identical to `63a2a8ab9cd3`).
What changed is the face layer. Three upstream files were vendored whole:

| Destination | Source | Files | License | Byte-identical |
|---|---|---|---|---|
| `components/liblinux80211/include/uapi/linux/genetlink.h` | `linux-upstream/include/uapi/linux/genetlink.h` | 1 | GPL-2.0-only with syscall exception | yes |
| `components/liblinux80211/include/uapi/linux/mpls.h` | `linux-upstream/include/uapi/linux/mpls.h` | 1 | GPL-2.0-only with syscall exception | yes |
| `components/liblinux80211/include/net/dropreason-core.h` | `linux-upstream/include/net/dropreason-core.h` | 1 | GPL-2.0-only | yes |

The rest of the S1a work is BSD-3-Clause face code written in this component
(one definition per name, used surface only), listed per file in
`docs/DIVERGENCES.md` rows 27-35:

- **rewritten** (were oversized vendored copies): `include/linux/{idr,gcd,u64_stats_sync,bitmap}.h`,
  `include/net/{sock,sch_generic,arp,inet_connection_sock}.h`;
- **new faces**: `include/linux/{align,rculist,irq_work,rhashtable}.h`,
  `include/net/{pkt_sched,inet_ecn}.h`, `include/linux80211/stack_config.h`;
- **grown**: `linuxkpi_core.{h,c}`, `skbuff/skbuff_pool.{h,c}`,
  `include/linux/{netdevice,device,debugfs,ethtool,rfkill,moduleparam,sysfs,ipv6,module}.h`,
  `include/net/{net_namespace,genetlink,netlink,iw_handler}.h`.

No conflicts with the pin: the import guard is the authority for `stack/`, and the
faces are not imports.

## The USB engine import (2026-11-13, rtw89)

The linuxkpi USB layer's *face* (`include/linux/usb.h`) came in with the
original component import; what was missing was its body. The rtw89 line needs
it, so the previous line's USB engine, its CherryUSB class hook and its adapter
were imported from the same source commit, with the same byte-identical rule
(the adapter's three include lines are the only edit and are recorded in
`DIVERGENCES.md`).

| Destination | Source path (previous line, `fdceccb9`) | Files | License | Byte-identical |
|---|---|---|---|---|
| `components/liblinux80211/bus/usb.c` | `components/linuxkpi/rtos/usb/linuxkpi_usb.c` | 1 | BSD-3-Clause | yes (renamed) |
| `components/liblinux80211/bus/linuxkpi_usb.h` | `components/linuxkpi/rtos/usb/linuxkpi_usb.h` | 1 | BSD-3-Clause | yes (renamed) |
| `components/liblinux80211/include/linux80211/lkpi_usb_backend.h` | `components/linuxkpi/rtos/usb/lkpi_usb_backend.h` | 1 | BSD-3-Clause | yes (moved to the seam header's directory) |
| `components/liblinux80211/bus/lkpi_usbh_class.{c,h}` | `components/linuxkpi/rtos/usb/` | 2 | BSD-3-Clause | yes (renamed) |
| `components/liblinux80211/bus/lkpi_usbh_modeswitch.{c,h}` | `components/linuxkpi/rtos/usb/` | 2 | BSD-3-Clause | yes (renamed) |
| `components/liblinux80211/ports/usb-rk3568/adapter.c` | `components/linuxkpi/rtos/usb/lkpi_usb_cherryusb.c` | 1 | BSD-3-Clause | **no**: three include paths rewritten for this component's layout (`DIVERGENCES.md`) |
| `components/liblinux80211/ports/usb-rk3568/lkpi_usb_cherryusb.h` | `components/linuxkpi/rtos/usb/lkpi_usb_cherryusb.h` | 1 | BSD-3-Clause | yes (renamed) |
| `components/liblinux80211/lkpi_rxtrace.{c,h}`, `lkpi_tcp_trace.{c,h}` | `components/linuxkpi/rtos/` | 4 | BSD-3-Clause | yes (moved to the component root, beside `lkpi_census`) |

The two trace units are observation-only: their bodies compile to nothing
unless `CONFIG_LKPI_RXTRACE` / `CONFIG_WLAN_TCP_TRACE` is on, which no image
selects. `lkpi_rxtrace`'s delivery hook had its caller in the previous line's
net80211 receive helper, which this line does not carry; the hook is therefore
present but uncalled, and that is recorded rather than wired to a second place.

`docs/IMPORT-MANIFEST.tsv` carries the per-file rows; the adapter's row holds
the imported blob and `DIVERGENCES.md` records why the current bytes differ.

## The host tier import (2026-11-10, K1)

| Destination | Source (same source commit) | Files | License | Byte-identical |
|---|---|---|---|---|
| `third-party/FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/` | upstream V11.3.1 pin, via the previous line's tree | `port.c`, `portmacro.h`, `utils/wait_for_event.{c,h}` | MIT | yes |
| `tests/board/rtos/platform/host_common_defs.h`, `host_freertos_defs.h` | previous line's tree at `fdceccb95dc85c70229709973ffcd150e643c7cd` | 2 | BSD-3-Clause | yes |
| `tests/board/rtos/app/port/clock_host.c` | same | 1 | BSD-3-Clause | yes |

`tests/host/kpi/` (bench, cases, host stubs) and `tools/build/{import-stack.py,
s0-compile.sh}` are written here; they are not imports.

## The iwlwifi import (2026-11-10, S1b)

The AC 7260 driver tree was imported verbatim from this line's GT pin into
`wireless-drivers/iwlwifi/` (294 files), the same flat discipline as the stack
import. Per-file digests: `wireless-drivers/iwlwifi/IMPORT.tsv`; re-check with
`tools/build/import-driver.py --driver iwlwifi --check`. The compiled set (72
TUs) is re-derived from the driver's own Makefiles under
`include/linux80211/driver_config.h`; `wireless-drivers/iwlwifi/PROVENANCE.md`
is the tree's own record (pin, licence mix, why 72 and not 74).

## The tracked firmware inputs (2026-11-11; RTL8851BU added 2026-11-13)

The driver firmware images used to be build products of the GT tree, which
made a clean clone of this repository unable to build a wireless image. They
are now tracked inputs of this repository, together with their licence texts:

| File | Size | SHA-256 | Origin | License |
|---|---|---|---|---|
| `components/liblinux80211/fw/iwlwifi-7260-17.ucode` | 1,049,340 | `5d81a6003df0228a497ad27f916ba2c979614b4c439b0f45a5f2873dc0607fe8` | GT line's initramfs at pin `63a2a8ab9cd3`, unmodified | Intel binary firmware licence (`LICENCE.iwlwifi_firmware` beside it) |
| `components/liblinux80211/fw/LICENCE.iwlwifi_firmware` | 2,046 | — | Intel's redistribution terms as distributed by linux-firmware | — |
| `components/liblinux80211/fw/rtw89/rtw8851b_fw-1.bin` | 1,408,706 | `1c4ccb5fa06bf5649146025e95993db66876a1fbc0bd766ec05686587f69c1b7` | GT line's initramfs `lib/firmware/rtw89/` and the previous line's tracked `firmware/rtw89/` copy (same bytes), unmodified | Realtek binary redistribution terms (`LICENCE.realtek_firmware` beside it) |
| `components/liblinux80211/fw/LICENCE.realtek_firmware` | 2,120 | — | Realtek's redistribution terms as distributed by linux-firmware | — |

The generated units `components/liblinux80211/fw/iwl7260_fw_blob.c` and
`components/liblinux80211/fw/rtw8851b_fw_blob.c` are build products and stay
untracked; `tools/rtos/fw-blobs.py` regenerates them from the tracked images
and pins each SHA-256. `components/liblinux80211/fw/PROVENANCE.md` is the
tree's own record.

## third-party/wpa_supplicant-rtos (2026-11-12)

| item | fact |
|---|---|
| upstream | `https://github.com/nxp-mcuxpresso/wpa_supplicant-rtos` (NXP MCUXpresso SDK middleware: the FreeRTOS port of hostap, both the STA supplicant and hostapd's AP MLME) |
| pin | branch `release/25.06.00`, commit `c21bbad35c072d53cec51f268029109893846a14` (2025-06-10) |
| imported | the whole tree except `port/`: 897 files, upstream layout unchanged |
| not imported | `port/` (22 files of mbedtls/KSDK hardware-crypto glue, Apache-2.0); this port builds `CONFIG_WPA_SUPP_CRYPTO=0` with hostap's internal crypto and references nothing in it |
| verification | `third-party/wpa_supplicant-rtos/patches/MANIFEST.sha256` holds the sha256 of all 897 files **as upstream**; `patches/0001..0008` are the port's only changes, and applying them to a pristine tree of the pin reproduces this tree byte for byte (checked 2026-11-12) |
| build face | the port's own OSA/eloop/l2_packet bottom and its `wpa_driver_lkpi_ops` (see `docs/decisions/2026-11-12-ap-mlme-port.md`) |

## The WPA2 round (2026-11-12, written here)

The G1 WPA2-PSK/CCMP round added code, it did not import a tree.  The new
`components/liblinux80211/linuxkpi_ccm.c` is written in this repository
(BSD-3-Clause): AES-128 follows FIPS-197 and CCM follows RFC 3610 (L=2,
M=authsize); the provider is shaped for mac80211's `crypto_aead_*` API and
`sg[0]=AAD / sg[1]=data / sg[2]=MIC` request layout.  No mbedtls, no hostap
crypto object and no copied implementation was added.

The same round edited two imported files; both edits are recorded in
`DIVERGENCES.md`:

- `components/liblinux80211/linuxkpi_netdev.c`: `eth_type_trans()` now calls
  `skb_reset_mac_header()` (the missing line Linux's `net/ethernet/eth.c`
  has).  Without it `skb_mac_header()` fell back to `skb->data` and
  mac80211's control-port destination rewrite overwrote the EAPOL PDU.
- `components/liblinux80211/linuxkpi_nl80211_probe.c`: the AP key helpers
  (`NEW_KEY`/`SET_KEY`/`DEL_KEY`) and `START_AP`'s privacy/control-port
  attributes; these are port code, not imported kernel files.

`components/liblinux80211/hostap/{driver_lkpi.c,hostap_ap.c,hostap_nl80211.c}`
and the `linux80211/*.h` seams are port code and were extended in place; the
imported hostapd tree (`third-party/wpa_supplicant-rtos`) is unchanged.
