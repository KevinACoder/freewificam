# cherryusb/patches - record only

The tree is imported **already patched**. This directory records the patch set
and the digest of the source tree the build compiles; it is not a build-time
replay directory.

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Base (= pin) | `51fef88da1382a395f7e6c0e907626d9e205abdd` (SHA-only; `v1.6.1-103-g51fef88d`) |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Patch list | `0001-xhci-host-controller-port.patch` (adds `port/xhci/usb_hc_xhci.c/.h`), `0002-ehci-iaad-qh-lifecycle.patch`, `0003-ehci-isochronous-transfer.patch` (adds the high-speed isochronous engine `port/ehci/usb_hc_ehci_iso.c`, plus two guarded hunks in `usb_hc_ehci.c` and two frame-cursor fields in `struct ehci_hcd`), `0004-uvc-host-streaming.patch` (adds the video class's running-stream half `class/video/usbh_uvc_stream.c/.h`), `0005-ehci-control-in-cache-invalidate.patch`, `0006-ehci-kill-iaa-wait.patch`, `0007-ehci-qtd-five-page-chunk.patch`, `0008-xhci-td-sixteen-trb.patch`, `0009-host-msc-modeswitch.patch`, `0010-usbh-aic8800-class.patch`, `0011-dwc2-isochronous-transfer.patch` (adds the DWC2 full-speed iso engine the F429 camera image uses) - all applied in the tree |
| Not carried | none of the eleven series patches; the vendor ports and the upstream `osal/` implementations stay in the tree as upstream reference and are not compiled |
| Applied | `0001`/`0002`/`0005`/`0006` were replayed with `patch -p1 --fuzz=0` on 2026-10-26 (`0006` at offset -5 lines); `0003`/`0004` were replayed the same way on 2026-10-27 (`0003` at offset +25 lines because `0007` shifted the file); `0007`/`0008` are the board line's own qTD-chunk and TD-bound fixes, applied directly |
| Manifest | `MANIFEST.sha256` - the digest of the imported tree, excluding this directory (916 files) |
| Verify | `cd third-party/cherryusb && sha256sum -c patches/MANIFEST.sha256` |

## What is carried

**The previous full upstream working tree** at the pin (916 files) is the
historical record in `MANIFEST.sha256`; this repository's tree is the compiled
subset described in `../cherryusb.provenance.md` (82 files: `core/`, `common/`,
`class/{hub,msc,video}/`, `port/{dwc2,ehci,xhci}/`, `osal/`,
`ports/osal/cmsis2/`, `patches/`, `LICENSE`). A file not in that subset is not
in this tree.

Compiled by this line's images: `core/usbh_core.c`, `class/hub/usbh_hub.c`,
`port/ehci/usb_hc_ehci.c`, `port/xhci/usb_hc_xhci.c`; the F429 images use
`port/dwc2/usb_hc_dwc2.c`; storage images add `class/msc/usbh_msc.c`; the F429
camera images add `class/video/usbh_video.c`, `class/video/usbh_uvc_stream.c`
and the DWC2 iso engine selected by `CONFIG_USB_DWC2_ISO`
(`tests/board/rtos/build/uvc.mk`). The OSAL is the first-party
`ports/osal/cmsis2/` backend, not upstream's `osal/` implementations.

## Note on the patch series

`0001` is a new-file port for a platform upstream does not carry; `0002`,
`0005` and `0006` are EHCI lifecycle/coherency fixes the board line proved;
`0003` fills the port's declared-but-undefined isochronous entry points
(high-speed only: full/low-speed isochronous needs the split-transaction
descriptor, and periods above one frame would need frame-scheduled
descriptors, both rejected explicitly); `0004` fills the host video class's
missing streaming half the way the upstream demos call it; `0007` and `0008`
are this line's own qTD page-chunk and TD-bound fixes; `0011` adds the DWC2
full-speed isochronous engine, one packet per channel activation, for the F429
camera image. The first four patches are byte-identical to the reference tree's
records at commit `19c5736`.

A future upstream acceptance removes the patch here, moves the pin forward and
notes both in `third-party/cherryusb.provenance.md`.
