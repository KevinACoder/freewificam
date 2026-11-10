# DIVERGENCES

Imported files are byte-identical by default. Every edit is recorded here with
its reason; the source commit and blob hash of each file are in
`IMPORT-MANIFEST.tsv`.

| # | File | Change | Reason |
|---|---|---|---|
| 1 | `tests/board/rtos/build/Makefile` | rewritten as a trim of the source: FreeRTOS-only backend section, RK3568-only board section, no host tier, no wireless/CMake/SDK worlds; the liblinux80211 component groups and archive added | this line carries four bus/network images and one platform; the mechanisms (COMPILE_C, flag stamps, per-world flags, linker gates, libs) are kept byte-for-byte in shape |
| 2 | `tests/board/rtos/Kconfig` | `BACKEND_THREADX/ZEPHYR` and their derived defaults removed; ThreadX-only `TX_*` knobs removed | only the FreeRTOS backend is imported; a backend that the build rejects must not be offered |
| 3 | `tests/board/rtos/platform/board/Kconfig` | firefly board symbols and `UVC_USB_BUS` removed | only the RK3568 board is imported |
| 4 | `tests/board/rtos/app/Kconfig` | WLAN/netio/lwIP-window knobs removed, `CASES` kept | those worlds are not imported; the case list is the one symbol every image needs |
| 5 | `tests/board/rtos/app/cases.h` | case declarations trimmed to tick, cmsis2, net, emmc, pcie, usb_xhci, usb_ehci | same reason as 4; `usb_ehci` was imported on 2026-11-10 when the bench's disk was found on the EHCI side |
| 6 | `tests/board/rtos/app/runner.c` | case table trimmed to the same seven; the wireless big-stack exception removed | same reason as 4 |
| 7 | `tests/board/rtos/config.mk` | default profile `rk3568-tick` | the previous default (`firefly-v2-rtw88-sdio`) is not carried |
| 8 | `tests/board/rtos/platform/build_defs.h` | added the eMMC case's `SDMMC_PORT_SDIO_HOST_TYPE`/`_HOST_ID` | the port header requires an image to name its controller; the eMMC case had no profile in the previous tree, so the case-derived line never existed there |
| 9 | `tools/build/kconfig-gen.py` | `KCONFIG_FILES` trimmed to the carried tree; `CASES` trimmed (seven cases); `legacy_macros` trimmed to the macros the imported sources read | the symbol set must describe this line, not the previous one |
| 10 | `tools/build/rtos.mk` | SDK/CMake/host targets removed; `images` aliased to `image`; default matrix is this line's profiles | those targets' worlds are not imported |
| 11 | `tools/build/check-no-d.sh` | default profile list is this line's | same reason as 10 |
| 12 | `tools/build/ci.sh` | one board; firefly targets and the SDK gate removed | same reason as 10 |
| 13 | `tools/build/dump-vars.py` | added (not in the original import group) | `check-build-contract.sh` calls `make.rtos dump-vars`; without it the contract gate cannot run |
| 14 | `make.rtos` | usage text trimmed | the targets it named no longer exist |
| 15 | `Makefile` | forwards `rk3568_ci` only | same reason as 10 |
| 16 | `tests/board/rtos/app/cases/usb_msc.c` | window follows the disk (`min(32 MiB, blocks - 4 MiB)`, 1 MiB aligned), sweep command count follows the window, and a disk smaller than the old window gets the identity arm (`usbh: msc ident-only`) | the bench's disk is 7.8 MiB, not the 29 GiB one the geometry was sized for; the old geometry is unchanged for a disk that reaches it |
| 17 | `tools/rtos/judge.py` | `CASES` trimmed to the seven carried cases; the USB storage check accepts the `ident-only` arm | the judge must judge this line's images, and an arm the image announces as identity-only is not missing its throughput anchors |

## Rules

- Prefer a patch that upstream could accept over an in-tree edit.
- When an in-tree edit is unavoidable, keep it minimal, mark it with
  `/* FREEWIFICAM: <what and why> */`, and add a row above.

| 18 | `include/net/{mac80211,cfg80211,ieee80211_radiotap}.h` | replaced by the upstream files at pin `63a2a8ab9cd3` | the real stack needs the full local/sta_info/sub_if_data and wiphy records; the old 88K/84K faces were the driver-view minimum and cannot express them. The old faces remain reachable in git history. |
| 19 | `include/linux/ieee80211.h` + 9 `ieee80211-*.h`, `include/uapi/linux/{nl80211,rfkill,netlink,const,limits,sysinfo,if_vlan,if_ether,ethtool,pkt_sched,pkt_cls,kernel}.h` | added from upstream (same pin) | pure protocol/data headers the real stack includes by name; the uapi files are the wire contract. |
| 20 | `include/linux/{bug,net,netlink,refcount,refcount_types,log2,stddef,hrtimer,hrtimer_types,capability}.h`, `include/net/scm.h`, `include/asm/{barrier,div64,types,bug}.h` | new face headers written in the component's style | the upstream files would drag asm/generated/kernel internals; each face cites the interface line it replaces and carries declarations only. |
| 21 | `include/linux/{rfkill,nl80211,net_namespace,socket,ip,skbuff,ethtool,semaphore,types,errno,netdevice}.h`, `linuxkpi_core.h` | de-duplicated against the upstream headers / extended for the stack | one definition per name: the enums moved to the uapi files, `possible_net_t`/`NETDEV_ALIGN`/`msg flags`/`__aligned_u64`/`clockid_t` added, `skb_network_header()`/`skb_network_offset()` moved to a body in `skbuff/skbuff_pool.c`, `struct_group`/`DEFINE_CLASS`/`DEFINE_GUARD`/`get_unaligned`/`fls64`/`at_least`/`__must_be_array`/`u64_stats_t` added. |
| 22 | `skbuff/skbuff_pool.c` | added `skb_network_header()`/`skb_network_offset()` bodies and a declaration of `ieee80211_hdrlen()` | the skb face needs the network-header derivation; the stack owns the header-length body (S1), so the unit declares it and the current images drop the caller with `--gc-sections`. |
| 23 | `linuxkpi_osdep.c` | removed the two driver-face `nla_put_*` stubs; added `down()`, `down_trylock()`, `down_timeout()` | the real `include/net/netlink.h` now carries the attribute builders (one definition), and the semaphore face declared the take family without a body. |
| 24 | `linuxkpi_core.c` | real `cancel_work_sync`; BH consumer (`lkpi-bh` queue, `local_bh_*`, `tasklet_disable/enable`, `in_softirq`); RCU reader count + grace period + deferred reclaim; `get_random_bytes`; `synchronize_net` now waits | `docs/decisions/2026-11-10-execution-context-model.md`; K1/K2 cases are the evidence. |
| 25 | `tests/board/rtos/build/{Makefile,kpi.mk}`, `tools/build/rtos.mk`, `make.rtos` | S0 compile world and `host-test` target | S0 measures the imported stack without linking it; the host target builds the bench outside the image. `KPI_INCLUDES` gained `include/uapi` (kernel-style `<linux/*.h>` uapi resolution). |
| 26 | `tests/host/kpi/**`, `tools/build/{import-stack.py,s0-compile.sh}` | new files written here | the bench and the S0 harness; not imports. |
