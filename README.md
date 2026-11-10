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
| mac80211 + cfg80211 + iwlwifi | next stage (S0/A) |

`docs/STATUS.md` states each stage as what it is; `docs/BUILD.md` records the
build's design and how a new world is added.

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
