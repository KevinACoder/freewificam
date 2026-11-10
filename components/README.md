# components

Code written in this repository.

| Component | Contents | State |
|---|---|---|
| `liblinux80211/` | the Linux kernel API surface, the bus engines, the seams; `net/mac80211` + `net/wireless` land next | **landed**: builds as `liblinux80211.a`, proven on the board by `rk3568-kpi-live` (real root complex) and `rk3568-kpi-mock` (synthetic host/card, null egress) |

Not yet proven: the SDIO engine's real backend. The adapter exists
(`third-party/fsl_sdmmc/ports/lkpi_sdio_fsl.c`) but no image compiles it: its
host type is the SDIO socket's, and the line that drives one arrives with the
wireless SDIO driver. The seam itself is exercised by the mock card, and the
card stack under it is exercised by the eMMC acceptance image - two halves,
not yet one wire.

## The component's boundary

`liblinux80211` is the part that must be movable to another OS. It owns:

- the Linux kernel-facing surface the stack compiles against (headers and
  bodies), because that is what the stack's source expects to find;
- the bus engines: the Linux-facing PCI/SDIO records and calls, with the host
  reached only through a narrow backend seam;
- the network-device face and the seam the egress plugs into.

It does **not** own the OS (threads, timers, locks are supplied by the OS port
through the CMSIS-RTOS2 facade), the controllers (a backend implements the bus
seam), or the network stack (an egress implements the network seam).

## Ports rule

Adapters live with the module whose interface they implement: the component's
own `ports/` for the adapters that implement the component's seams
(`ports/pci-rk3568/`), the third-party tree's `ports/` for the ones that
implement that tree's ops (`third-party/fsl_sdmmc/ports/` for the SDIO card
stack, `third-party/lwip/ports/` for the egress), and the board directory for
the BSP. **There is no top-level `ports/`.**
