# patches/cherryusb - index

Local patches replayed onto the vendored CherryUSB submodule
(`third-party/cherryusb/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by `tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Base (= pin) | `51fef88da1382a395f7e6c0e907626d9e205abdd` (SHA-only; no tag at this commit) - the pin the patches below were generated against, per `third-party/cherryusb.provenance.md`; the earlier carrier-tree commit `5019680e` is not an ancestor of it |
| Patch list | `0001-xhci-host-controller-port.patch` - adds `port/xhci/usb_hc_xhci.c` and `.h` (the host controller driver for the RK3568's xHCI instances); `0002-ehci-iaad-qh-lifecycle.patch` - keeps the iaad flag consistent with the qh slot lifecycle (the four hunks that stop the pool walk from replaying a completion on a slot that is freed or rebound) |
| Submission status | not submitted upstream (`0001` is a new-file port for a platform upstream does not carry; `0002` is a bug fix for the kill/reap path, written to be sendable as-is) |

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible; the xHCI port is exactly that shape.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/cherryusb.provenance.md`.

Disposal record for the pre-existing local-vs-upstream patches found in the
frozen carrier tree (the ruling is in the import spec's decision record):

| Candidate patch | Disposal |
|---|---|
| `USBH_URB_ZERO_PACKET` (bulk OUT zero-length-packet semantics) | not carried - revisit only if the mass-storage consumer proves it is needed, then as its own patch through this channel |
| a second multi-HCD dispatch mechanism (`struct usbh_hcd_ops` + `CONFIG_USBHOST_MULTI_HCD`) | not carried - the pin already ships the register-driver API (`usbh_register_hc_driver`), and a second mechanism for the same job is a design defect, not a feature |
| EHCI worklist-flag fix (a single `qh->urb` guard in the iaad walk) | not carried - the guard alone was judged off-target for the long-run stall and that stall's root cause was never established |
| EHCI kill/iaad slot-lifecycle fix (four hunks: flags cleared with the slot claim, the kill branch handing the flag back, the kill spin-timeout completing instead of abandoning, and the iaad walk guard) | **carried as `0002`** - this entry is a correction: the disposal record above had folded the lifecycle fix into the worklist-flag ruling and dropped it, and the fault came back. The board that produced it reports the bad semaphore handle in the give path with an identical fault address across builds, which is the signature the four hunks were written against |
