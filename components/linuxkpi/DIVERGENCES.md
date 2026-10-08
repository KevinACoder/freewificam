# components/linuxkpi - divergences

Deviations of this import from its source blobs. Empty at import time.

| # | File | Change | Reason | Round |
|---|---|---|---|---|
| 1 | `usb/linuxkpi_usb.c` | The controller-library include set (`usbh_core.h`, `usb_osal.h`, `usb_errno.h`, `tx_api.h`, `generic_timer.h`) is replaced by the project's own narrow backend seam (`usb/lkpi_usb_backend.h`) plus the CMSIS-RTOS2 facade (`os/cmsis2/cmsis2.h`); the five controller call sites (submit x2, kill, control x2) route through the seam dispatchers | Gate 1 builds without the interface library; the seam is the component's port boundary (the target adapter maps it onto the production controller driver). The three `tx_time_get()` uses die with the diagnostics strip below; the CMSIS tick replaces them | linuxkpi-usb-test R1 |
| 2 | `usb/linuxkpi_usb.c` | The backend transfer type changes from the interface library's urb structure to the seam's `lkpi_usb_be_urb`; the per-endpoint and synchronous-lane storage keep their one-per-endpoint / four-slot shapes | Same as 1; the storage shapes are the port's own design and do not depend on which adapter is installed | linuxkpi-usb-test R1 |
| 3 | `usb/linuxkpi_usb.c` | The board diagnostics surface is removed from this build: the capture ring, the per-endpoint counter ledger, the gap/wedge reports, the rx-hold experiment gate, the ctrl-staging stage counters and the board-specific emergency dump; `usb/linuxkpi_usb.h` keeps only the engine's own entry points and the allocation-seam counters | No consumer in the host mock build (the board line owns that surface in its own tree); the mock's observation face is the controller model's capture plus the seam counters | linuxkpi-usb-test R1 |
| 4 | `usb/linuxkpi_usb.c` | The 64-byte-aligned fixed-slot DMA stage pool is replaced by a passthrough hook with a grant counter; control setup and data pointers go to the seam as the caller handed them in | The alignment requirement belongs to one controller adapter, not to the engine; a host transfer has no DMA. An adapter that needs staged buffers installs them behind the seam | linuxkpi-usb-test R1 |
| 5 | `usb/linuxkpi_usb.c` | The allocation seam grows: a live-urb registry, a released-pointer ring, and named counters (`n_urb_alloc`, `n_urb_free`, `n_coherent_alloc`, `n_coherent_free`, `alloc_fail`, `double_free`, `stray_free`, `uaf_write`); the coherent pair joins it. The engine's `usb_free_urb` and the submit/kill guards consult the ring before touching any field | The memory contracts of the mock tier need named, observable counters; a double free or a write through a released pointer must be refused and named, not corrupt the heap | linuxkpi-usb-test R1 |
| 6 | `usb/linuxkpi_usb.c` | The engine's error map gains one case: the seam's short-read code maps to `-EREMOTEIO` (the Linux error contract for a control transfer that could not fill its length); a cancelled transfer maps to `-ENOENT` | The short read is part of the Linux control-transfer contract; the mock device models it by answering fewer bytes than requested | linuxkpi-usb-test R1 |
| 7 | `usb/linuxkpi_usb.h` | Rewritten: the capture-ring / claim-plumbing declarations of the board line are replaced by the engine's own entry points plus the allocation-seam counters | Follows 3 | linuxkpi-usb-test R1 |
| 8 | `linuxkpi_core.h` | Untouched; the host build supplies the service bodies it declares (allocation, logging, sleep, mutexes, DAIF spin sections, wait queues) in the mock test directory, and the critical-section extension header now lives beside the CMSIS-RTOS2 facade | The core implementation belongs to the later linuxkpi body; the mock round owns only the services its consumers call | linuxkpi-usb-test R1 |

| 9 | `include/linux/usb.h` | Three changes to the driver-visible USB face: `struct usb_device.dev` becomes an embedded `struct device` (it was a pointer), `struct usb_driver` gains `no_dynamic_id`, and two helpers the drivers call are added (`usb_endpoint_type` as an inline, `usb_init_urb` as a declaration whose body belongs to the engine) | The embedded device is what the interface has, and drivers take its address: with a pointer member, the 81 `&udev->dev` uses of the first compiled driver would pass a `struct device **`. The two members and helpers are simply missing from the face and the driver's table and probe path need them. The pointer member was never assigned anywhere in this tree, so no consumer changes | rtl8188eus-port R1 |

Import state (2026-10-07): the nine imported files were byte-identical to their
source blobs on the previous line's `feat/lkpi-stabilize-r1` branch (blob IDs in
`PROVENANCE.md`) at import.  Rows 1-7 above are the engine-adoption round's
entries; `PROVENANCE.md`'s manifest keeps the original blob IDs as the import
anchor, and re-checking those blobs against the working tree now shows exactly
the entries listed here.

Known convergence work that will produce the first entries (recorded here so it is
not rediscovered later):

* `skbuff/skbuff_pool.h` declares the Linux-shaped skbuff contract face that
  `linuxkpi_core.h` includes, while the pool's source of truth is the verbatim
  TizenRT unit in `components/skbuff/`. The two faces converge in the memory
  subsystem / consumer rounds; **the pool body landed in the pool-body R1 round:
  its eight convergence entries and the one face addition
  (`lkpi_skb_pool_configure`) are recorded in `skbuff/DIVERGENCES.md`**.
* `usb/lkpi_usbh_class.c` and `usb/usbkpi_cmds.c` stay vendored but outside the
  host build (class hooks and diagnostics of the board line). Adopting them
  later - if a consumer appears - produces one entry per change.

Rules: prefer an upstreamable patch over an in-tree edit; keep every edit minimal
and register it here with file, reason and round.
