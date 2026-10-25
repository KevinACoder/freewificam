# ports/linux/rtl8189f - first-party RTL8189FTV front-end

The kernel-side front-end for the RTL8189FTV. It calls the imported vendor tree
(`third-party/rtl8189fs/`) for the HAL, SDIO and embedded firmware, and owns the
802.11 behaviour that the vendor stack used to provide. The tree bytes stay
untouched; this directory is GPL-2.0-only, the same licence as the vendor HAL.

| File | Purpose |
|---|---|
| `selftest.c` | module entry: registers the SDIO driver for `024c:f179`; probe attaches the adapter and the mac80211 front-end |
| `mac80211.c` | stock-mac80211 skeleton (R2): wiphy/netdev, start/stop/add/config, sw_scan channel walk, tx drop |
| `init_hw_mlme_ext.c` | first-party replacement for the vendor MLME symbol that `rtw_hal_init()` calls (three register/state operations) |
| `mlme_stubs.S` | diagnostic zero-return stubs for the vendor MLME/OS symbols the HAL-only keep-set still references; replaced one by one |
| `mlme_stubs_data.c` | the OUI/cipher constants and module-parameter globals the keep-set references |

## Build

```sh
MODE=frontend tests/board/linux-driver/build.sh
```

The build mirrors the vendor tree and this directory, compiles the HAL-only
keep-set (88 vendor translation units: chip, `phydm`, SDIO HCI, eFuse, LED and
the IO/datapath helpers) plus these files, and leaves the vendor MLME, AP, P2P,
TDLS, mesh, RM, MP, debug, bridge-extension and cfg80211/WEXT units out of the
link. The stub file is a scaffold: every symbol in it is on the R1 closure list
and must be replaced or dropped before the mac80211 front-end claims the card.

The mac80211 skeleton depends on `mac80211.ko` and `libarc4.ko`; on the lab
image they are not under `/lib/modules`, so load them from `/tmp` first:
`insmod libarc4.ko; insmod mac80211.ko; insmod rtl8189f_selftest.ko`.

## Status

R1 link/probe: the module builds with no unresolved symbol outside the kernel's
exports and binds `mmc0:0001:1` on the board. Adapter init, firmware download,
MAC read and channel programming are the next step; the vendor MLME object is
not linked.
