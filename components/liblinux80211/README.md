# liblinux80211

The part of this repository meant to be movable to another OS: the Linux kernel
API surface a wireless stack compiles against, the bus engines that reach the
host through narrow seams, and — next — `net/mac80211` and `net/wireless`.

```
  mac80211 + cfg80211        (next stage; the stack and the drivers compile against
   ────────────────────       the Linux face below)
  linuxkpi bodies            kpi/
  ────────────────────
  bus engines                bus/        (Linux records over the backend seams)
  ────────────────────
  OS / bus / egress ports    supplied by the image, not by this component
```

## What it owns, and what it does not

| Owns | Where |
|---|---|
| the Linux face (`linux/`, `net/`, `asm/`, `uapi/`) | `include/` |
| the kernel-API bodies (allocation, workqueues, timers, locks, netdev, skb) | `kpi/` |
| the PCI and SDIO engines | `bus/` |
| the seams | `include/linux80211/` |

| Does not own | Who supplies it |
|---|---|
| threads, locks, timers, delays | the OS port, through the CMSIS-RTOS2 facade |
| the controllers, their registers, DMA and interrupts | a bus backend implementing the seam |
| the network stack | an egress module implementing `lkpi_netdev_rx()` |

## Seams

- **OS**: the bodies include `<cmsis_os2.h>` through the OS port's include
  path.  A FreeBSD port implements the same facade over its own primitives.
- **Bus**: `lkpi_pcie_backend.h`, `lkpi_sdio_backend.h` — ports & adapters;
  the engine is adapter-neutral and a second backend cannot fork it.
- **Net**: `net.h` — draft until mac80211's net-device path compiles.

Adapters live where the module that owns the interface keeps its ports: this
component's `ports/` for the ones that implement its seams (the RK3568 PCI
adapter), `third-party/fsl_sdmmc/ports/` for the SDIO card stack's,
`third-party/lwip/ports/` for the egress. There is no top-level `ports/`.

## License

The component's own code is BSD-3-Clause. `include/` carries the Linux
headers the surface mirrors; they are the face, not the stack.

**Since 2026-11-10 the stack is in the tree**: `stack/mac80211/` (44 TUs + 26
headers) and `stack/wireless/` (18 TUs + 8 headers) are verbatim GPL-2.0-only
imports from the GT pin `63a2a8ab9cd3` (`stack/IMPORT.tsv`). The component is
therefore a mixed-license tree: its own code stays BSD-3-Clause, the stack
subtree is GPL-2.0-only, and every artifact that links the stack is GPL-2.0
(`docs/LICENSES.md` §3). The stack is compiled by the S0 inventory target and
not linked by this build; S1 adds the link behind its own profile.

As of 2026-11-10 (S1a) all **62 stack TUs compile** against these faces
(`runs/S0/run-50/`, C0 only). The faces that grew for it are the used-surface
ones under `include/linux/` and `include/net/` (idr/sock/sch_generic/arp/
inet_connection_sock/rhashtable/rculist/irq_work/align/pkt_sched/inet_ecn),
`include/linux80211/stack_config.h` (the stack's feature identity:
`CONFIG_CFG80211`/`CONFIG_MAC80211` and the minstrel defaults), and the
sk_buff geometry/vlan/csum fields in `skbuff/`. No stack byte changed.
