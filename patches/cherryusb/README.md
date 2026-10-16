# patches/cherryusb - index

Local patches replayed onto the vendored CherryUSB submodule
(`third-party/cherryusb/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by `tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Base (= pin) | `51fef88da1382a395f7e6c0e907626d9e205abdd` (SHA-only; no tag at this commit) - the pin the patches below were generated against, per `third-party/cherryusb.provenance.md`; the earlier carrier-tree commit `5019680e` is not an ancestor of it |
| Patch list | `0001-xhci-host-controller-port.patch` - adds `port/xhci/usb_hc_xhci.c` and `.h` (the host controller driver for the RK3568's xHCI instances); `0002-ehci-iaad-qh-lifecycle.patch` - keeps the iaad flag consistent with the qh slot lifecycle (the four hunks that stop the pool walk from replaying a completion on a slot that is freed or rebound); `0004-uvc-host-streaming.patch` - adds `class/video/usbh_uvc_stream.c` and `.h`, the running-stream half of the video class behind the calls the upstream host-UVC demos make (`usbh_video_stream_create/start/stop/dequeue/enqueue`: the demos ship without the engine; the start path re-arms the streaming interface when the camera refuses its probe, the state the board camera entered after every stream); `0005-ehci-control-in-cache-invalidate.patch` - the completion path invalidates control-IN buffers from the setup packet's direction (endpoint zero's address is 0x00, so the old endpoint-address test skipped every control read); `0003-ehci-isochronous-transfer.patch` - adds `port/ehci/usb_hc_ehci_iso.c`, the high-speed isochronous transfer engine behind the three symbols the port declares (`ehci_iso_urb_init` / `ehci_kill_iso_urb` / `ehci_scan_isochronous_list`: the call sites existed under `CONFIG_USB_EHCI_ISO` but no definition did, so the switch could not link), plus two small hunks in `usb_hc_ehci.c` (the iso submit branch returns the engine's result directly - the shared tail waits on a queue head, and there is none here; the iso kill branch invokes the completion callback the way the other endpoint types' kill path does) and two frame-cursor fields in `struct ehci_hcd` (`usb_hc_ehci.h`: the tiling cursor lives with the port's per-bus state); `0006-ehci-kill-iaa-wait.patch` - the kill handshake's wait for the IAA becomes a real time budget (rounds plus OS-tick naps) instead of 200000 counted loop iterations, and stops burning the core while it waits (every caller of `usbh_kill_urb` is a thread, never an interrupt handler) |
| Submission status | not submitted upstream (`0001` is a new-file port for a platform upstream does not carry; `0002` is a bug fix for the kill/reap path, written to be sendable as-is; `0003` fills the port's own declared-but-undefined isochronous entry points and is written to be sendable as-is - high-speed only: full/low-speed isochronous needs the split-transaction descriptor, and periods above one frame would need frame-scheduled descriptors, both rejected explicitly; `0004` fills the host video class's missing streaming half the way the upstream demos call it, written to be sendable as-is; `0005` is the control-IN invalidate fix, written to be sendable as-is; `0006` is the kill-wait fix, written to be sendable as-is) |

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible; the xHCI port is exactly that shape.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/cherryusb.provenance.md`.

Note on `0003`'s shape: the engine fills the descriptor quantities this pin
already carries but never used - `struct ehci_itd_hw`'s `start_frame` /
`mf_unmask` / `mf_valid` / `pkt_idx[]` / `dir_in`, and the per-slot
`struct ehci_iso_hw` / `g_ehci_hcd.ehci_iso_used` dimensioning (two transfers
in flight, `CONFIG_USB_EHCI_ITD_NUM` descriptors each).  Transfers are tiled
on the frame list (a submission starts after the frames earlier transfers on
the bus reserved, since two transfers of one endpoint cannot share a
micro-frame), with the cursor in the per-bus driver state.  The register encoding
follows `usb_ehci_reg.h` (the iTD TSCL transaction fields and the buffer
pointer page list, EHCI specification paragraph 3.3) and the micro-frame period
table follows the USB 2.0 high-speed period encoding (chapter 9.6.6).  The
scheduling core touches no register, which is what lets the host suite
(`ehci-iso-schedule-test`) drive it against a controller in RAM.

Disposal record for the pre-existing local-vs-upstream patches found in the
frozen carrier tree (the ruling is in the import spec's decision record):

| Candidate patch | Disposal |
|---|---|
| `USBH_URB_ZERO_PACKET` (bulk OUT zero-length-packet semantics) | not carried - revisit only if the mass-storage consumer proves it is needed, then as its own patch through this channel |
| a second multi-HCD dispatch mechanism (`struct usbh_hcd_ops` + `CONFIG_USBHOST_MULTI_HCD`) | not carried - the pin already ships the register-driver API (`usbh_register_hc_driver`), and a second mechanism for the same job is a design defect, not a feature |
| EHCI worklist-flag fix (a single `qh->urb` guard in the iaad walk) | not carried - the guard alone was judged off-target for the long-run stall and that stall's root cause was never established |
| EHCI kill/iaad slot-lifecycle fix (four hunks: flags cleared with the slot claim, the kill branch handing the flag back, the kill spin-timeout completing instead of abandoning, and the iaad walk guard) | **carried as `0002`** - this entry is a correction: the disposal record above had folded the lifecycle fix into the worklist-flag ruling and dropped it, and the fault came back. The board that produced it reports the bad semaphore handle in the give path with an identical fault address across builds, which is the signature the four hunks were written against |
| `0005-ehci-control-in-cache-invalidate.patch` | **added 2026-10-14**: `ehci_urb_waitup()` took the control-IN direction from `urb->ep->bEndpointAddress`, which is 0x00 for endpoint zero by definition - the invalidate never ran for control reads, so a stale cache line could be returned instead of the transfer's bytes (seen on the board as an occasional all-zero device descriptor). Written to be sendable as-is |
| `0006-ehci-kill-iaa-wait.patch` | **added 2026-10-17** (user ruling: this one can be changed - CherryUSB may not have thought it through, so add a patch): `usbh_kill_urb()`'s wait for the IAA handshake was a busy spin bounded by 200000 loop iterations - it burned the core for the whole wait, and the bound was not a time (how long those iterations take is whatever the compiler made of the loop). It now spins only for the first rounds and then naps one OS tick between checks, so the budget is a real time bound. Written to be sendable as-is |
