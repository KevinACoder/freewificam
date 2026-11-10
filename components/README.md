# components

Code written in this repository.

| Component | Contents | State |
|---|---|---|
| `liblinux80211/` | the kernel API surface, the bus engines and (later) `net/mac80211` + `net/wireless`, behind explicit OS / bus / network seams | not started; landing next |

## The component's boundary

`liblinux80211` is the part that must be movable to another OS. It owns:

- the Linux kernel-facing surface the stack compiles against (headers and
  bodies), because that is what the stack's source expects to find;
- the bus engines: the Linux-facing PCI/SDIO/USB records and calls, with the
  host reached only through a narrow backend seam;
- the network-device face and the seam the egress plugs into.

It does **not** own the OS (threads, timers, locks are supplied by the OS
port through one facade), the controllers (the backend implements the bus
seam), or the network stack (the egress implements the network seam).

Adapters live with the module whose interface they implement: the component's
own `ports/` for the adapters that implement the component's seams, the
third-party tree's `ports/` for the ones that implement that tree's ops
(`third-party/lwip/ports/`, `third-party/fsl_sdmmc/ports/`), and the board
directory for the BSP. There is no top-level `ports/`.
