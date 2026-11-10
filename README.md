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
   components/linuxkpi                      this repository
   ────────────────────────────────────
   CherryUSB / PCIe host + driver           bus, replaced
```

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
- **A bus backend** for the device you target (USB host stack, or a PCIe host).
- **An egress** to your network stack (lwIP is the intended one).

## Layout

| Path | Contents |
|---|---|
| `components/linuxkpi/` | the kernel API surface the stack needs |
| `docs/` | build notes, provenance of imported files, conventions |
| `tools/` | helpers (commit-message checker, import scripts) |

Drivers and the wireless stack itself are not vendored here as of this writing;
`docs/PROVENANCE.md` records what is imported and from where once that starts.

## License

`net/mac80211` and `net/wireless` are `GPL-2.0-only`, so anything that links
them is `GPL-2.0`. The kernel-API layer written here is BSD-3-Clause on its own
and becomes GPL-2.0 as part of a linked image. See `docs/LICENSES.md` for the
per-component breakdown.

## Status

Early. The stack is not yet built against this repository; `docs/STATUS.md`
tracks which stage has been reached, and states each stage as what it is —
"compiles" and "links" are not "runs".
