# cherryusb/patches - record only

The tree is imported **already patched**. This directory records the patch set
and the digest of the source tree the build compiles; it is not a build-time
replay directory.

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Base (= pin) | `51fef88da1382a395f7e6c0e907626d9e205abdd` (SHA-only; `v1.6.1-103-g51fef88d`) |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Patch list | `0001-xhci-host-controller-port.patch` (adds `port/xhci/usb_hc_xhci.c/.h`), `0002-ehci-iaad-qh-lifecycle.patch`, `0005-ehci-control-in-cache-invalidate.patch`, `0006-ehci-kill-iaa-wait.patch`, `0007-ehci-qtd-five-page-chunk.patch`, `0008-xhci-td-sixteen-trb.patch` - all applied in the tree |
| Not carried | `0003-ehci-isochronous-transfer.patch` (no isochronous consumer in this line; the board config leaves `CONFIG_USB_EHCI_ISO` undefined, which is what the port's `#ifdef` guards read) and `0004-uvc-host-streaming.patch` (no UVC line in this image) |
| Applied | the five patches above are applied in the tree; `0001`/`0002`/`0005`/`0006` were replayed with `patch -p1 --fuzz=0` on 2026-10-26 (`0006` at offset -5 lines), and `0007`/`0008` are the board line's own qTD-chunk and TD-bound fixes, applied directly |
| Manifest | `MANIFEST.sha256` - the digest of the imported tree, excluding this directory |
| Verify | `cd third-party/cherryusb && sha256sum -c patches/MANIFEST.sha256` |

## What is carried

**The full upstream working tree** at the pin (913 files, every directory:
`core/`, `common/`, `class/` including every class, `port/` including the
vendor ports, `osal/`, `demo/`, `docs/`, `tests/`, `platform/`, `third_party/`,
the root build files and `LICENSE`/`VERSION`). Nothing is dropped: the tree is
the reference for what upstream offers, even where this line compiles only a
subset. The user ruling of 2026-10-26 is explicit - a sparse import loses the
map of upstream capabilities and costs more later than the bytes save.

Compiled by this line's images: `core/usbh_core.c`, `class/hub/usbh_hub.c`,
`port/ehci/usb_hc_ehci.c`, `port/xhci/usb_hc_xhci.c`; storage images add
`class/msc/usbh_msc.c`. The OSAL is the first-party
`ports/cherryusb/osal/cmsis2/` backend, not upstream's `osal/` implementations;
the isochronous engine (`0003`) and the video class (`0004`) are present in the
tree but not compiled in any image of this line.

## Note on the patch series

The first four patches are byte-identical to the carrier tree's records
(`~/workspace/project_rtos_webcam/freewificam/patches/cherryusb/`, documented
only). `0001` is a new-file port for a platform upstream does not carry;
`0002`, `0005` and `0006` are EHCI lifecycle/coherency fixes the board line
proved. A future upstream acceptance removes the patch here, moves the pin
forward and notes both in `third-party/cherryusb.provenance.md`.
