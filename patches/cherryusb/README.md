# patches/cherryusb - index

Local patches replayed onto the vendored CherryUSB submodule
(`third-party/cherryusb/`). The vendored tree itself is never edited: every
difference lives here and is applied at build time by `tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Base (= pin) | `5019680e7a364c0a95b4b0cf0e30802047a26f59` (SHA-only; no tag at this commit) |
| Patch list | `0001-xhci-host-controller-port.patch` - adds `port/xhci/usb_hc_xhci.c` and `.h` (the host controller driver for the RK3568's xHCI instances) |
| Submission status | not submitted upstream (the patch is a new-file port for a platform upstream does not carry; the lineage and the reasons are in the patch's commit message) |

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
| EHCI worklist-flag fix and a persistent-QH experiment | not carried (ruling: the worklist-flag idea was judged off-target for the long-run stall by the community, and the stall's root cause was never established) |
