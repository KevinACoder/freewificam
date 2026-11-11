# freewificam

Run the Linux wireless stack on an RTOS.

`freewificam` takes `net/mac80211` and `net/wireless` **verbatim** and runs them
on top of a small kernel-API layer, with the bus backend and the network egress
replaced. The MAC layer — MLME, aggregation, rate control, encryption, channel
management — is the Linux code, unmodified. What changes is what sits below it
(the bus) and above it (the network stack).

```
   lwIP                                     network egress, replaced
   ────────────────────────────────────
   net/mac80211 + net/wireless              upstream, verbatim
   ────────────────────────────────────
   components/liblinux80211                 kernel API + bus engines
   ────────────────────────────────────
   CherryUSB / PCIe / SDHCI host + driver   bus, replaced
```

## Where the line is

This repository is the successor of the previous net80211 line. It starts by
bringing the **RTOS platform itself** up in this tree, because the mac80211 work
needs a board that boots before it needs a stack that links:

| Step | State |
|---|---|
| RTOS baseline builds and boots (`rk3568-tick`) | done, 2/2 |
| Four acceptance images build and boot (network, PCIe/NVMe, eMMC, USB storage) | done, 2/2 each, judged |
| `components/liblinux80211` (linuxkpi + PCI/SDIO engines + seams) | landed; `rk3568-kpi-live`/`-mock` 2/2 |
| mac80211 + cfg80211 + iwlwifi | C0-C6 reached: register, attach, scan, join, data plane (2/2 cold boots each) |
| C6 throughput / stability | measured on the AC 7260 bench; numbers below |
| A0/A1 AP role | the AC 7260 becomes an AP and its beacons reach the air (2/2); association waits on a userspace AP MLME - `../runs/2026-11-11-iwl7260-ap/` |
| AP role (RTOS as AP, Pi 4B as STA) | next stage |

`docs/STATUS.md` states each stage as what it is; `docs/BUILD.md` records the
build's design and how a new world is added.

## Measured on the AC 7260 bench

The bench AP is the RTL8812AU `labwifi-ar-ht20s` on channel 5 (2432 MHz,
**2.4 GHz**, HT20, open, WMM).  The same AC 7260 sits on the RK3568.  Both
arms use iperf2 and the receiver-side number, two cold boots averaged
(2026-11-11):

| | GT (`63a2a8ab9cd3`) | this port |
|---|---|---|
| PHY link (`iw link`) | 144.4 Mbit/s, MCS15, short GI, 2x2 | not printed; the 60 Mbit/s TCP downlink already requires HT |
| TCP downlink | 42.4 Mbit/s | **60.5 Mbit/s** |
| TCP uplink | 24.85 Mbit/s | 23.18 Mbit/s (93% of GT) |
| UDP 40 M downlink | 41.4 Mbit/s, 0% loss | 37.9 Mbit/s, 0% loss |

**144.4 Mbit/s is the PHY rate, not throughput.**  It is two spatial streams at
72.2 Mbit/s on a 20 MHz channel with short GI, on 2.4 GHz channel 5 - not 5 GHz.
The TCP numbers are what the stack actually moves.  The downlink is higher than
the GT arm; the uplink was 78.8% of GT until the skb data pool went from 64 to
128 slots (+18.4%, a one-variable arm).  On the same image a 30-minute TCP
downlink moved 13.95 GB at the adapter (peer 60.4 Mbit/s), ten reconnects took
0.7-0.9 s each, and no boot reset.

Reproduce: build `PROFILE=rk3568-iwl7260-c6-pool128` (or `-c6` for the clean
baseline) and drive `tools/rtos/c6-throughput.sh`; the GT arm is the same
`Image-gt-63a2a8ab9cd3` booted by the project's GT runner; `tools/rtos/c6-report.py`
applies the 80% / 1% / three-source gates.

## Why

Porting a Linux wireless driver to an RTOS is well-trodden: the driver talks to
the kernel through a kernel-API compatibility layer, and the layer is small
because a driver uses a bounded slice of the kernel. Porting the *stack* is the
part that is usually avoided — a stack leans on scheduling, memory lifetime and
locking models that an RTOS does not have, and reimplementing it drifts from the
code being tested.

This repository tests the other split: keep the stack, replace the two edges.

## Requirements

- **Single core.** The RTOS side is a uniprocessor FreeRTOS port. Every
  read-copy-update primitive the stack uses is a plain load/store there, which
  is equivalent rather than approximate *because* there is no other core to be
  concurrent with. This is a precondition of the design, not an implementation
  detail; a multiprocessor build needs the whole RCU discipline re-done.
- **A bus backend** for the device you target (USB host stack, a PCIe host, an
  SDHCI/eMMC host).
- **An egress** to your network stack (lwIP is the intended one).

## Layout

| Path | Contents |
|---|---|
| `tests/board/rtos/platform/` | RK3568 board port, AArch64 boot layer, FreeRTOS/CMSIS-RTOS2 runtime |
| `tests/board/rtos/app/` | the case runner and the acceptance cases |
| `tests/board/rtos/build/` | the platform Makefile and the per-world fragments |
| `tests/board/rtos/configs/` | the profiles (one defconfig each) |
| `third-party/` | FreeRTOS, CMSIS, lwIP, CherryUSB, fsl_sdmmc, printf, tlsf, ulog (compiled closures, pins in `docs/PROVENANCE.md`) |
| `tools/` | the Kconfig generator, the build gates, the judge, the commit checker |
| `components/` | first-party components; `liblinux80211` is the next one |
| `docs/` | provenance, divergences, licenses, build design, status |

## Build and boot

```
AARCH64_CROSS_PATH=<xpack prefix> ./make.rtos list
AARCH64_CROSS_PATH=<xpack prefix> ./make.rtos image PROFILE=rk3568-emmc
```

The image is `tests/board/rtos/build/build/rtos-freertos.bin`: a flat raw image
linked at `0xa000000`, loaded over TFTP and entered with `go 0xa000000`. The
board-side control (serial, power, TFTP, evidence) lives in the project's
`skills/rk3568-board`.

## License

The platform, the build and the tools written here are `BSD-3-Clause`. The
imported trees keep their own terms (MIT, Apache-2.0, BSD-3-Clause, ISC); see
`docs/LICENSES.md`.

When `net/mac80211` and `net/wireless` land in `components/liblinux80211`, every
binary that links them becomes a `GPL-2.0` combined work. That is why the
component is a separate build unit and a separate profile family.
