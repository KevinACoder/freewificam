# third-party/cherryusb - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Pin | commit `51fef88da1382a395f7e6c0e907626d9e205abdd` (2026-10-06, "chore: add adb into build") - the upstream master tip as of 2026-10-08, SHA-only (no tag at this commit). Ruling: the tip carries the multi-host-controller registration API and the latest host-side fixes; the pin is the SHA, so the branch moving does not move it |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | git submodule at the pin, full working tree checked out (verified 2026-10-13: no sparse filter is configured; the whole upstream tree - `core/ common/ class/ osal/ port/ demo/ docs/ tests/ platform/` - is materialized). Notes here once described a sparse set; that no longer matches the checkout and the video-class work depends on the full one |
| Fetched | 2026-10-08 from the upstream remote (`git fetch origin master`, reachable at fetch time); the frozen read-only carrier copy of this tree (kept outside this repository) holds the earlier pin `5019680e7a364c0a95b4b0cf0e30802047a26f59` as a fallback and a comparison point |
| Local differences | none (zero patch) |
| Compiled set | per image: `core/usbh_core.c`, `class/hub/usbh_hub.c`, `class/msc/usbh_msc.c` (storage images) or `class/video/usbh_video.c` (the UVC camera image), the controller ports `port/ehci/usb_hc_ehci.c` and `port/xhci/usb_hc_xhci.c` (both replayed - the xHCI host port and the EHCI isochronous engine travel in `patches/cherryusb/`), the first-party osal backend and the board glue |
| Consumers | `ports/cherryusb/` (single CMSIS-RTOS2 osal backend, board configuration and glue) and the ThreadX board images (regression, wireless lines, the UVC camera image) |
| Reason | USB host stack for the RTOS lines: the RK3568 board's USB controllers (two xHCI instances, two EHCI lines) need a host stack whose OS coupling is one small osal, so a single CMSIS-RTOS2 backend serves both kernels. Recorded as a deferred candidate in the repository-bootstrap ledger (C14); the stack-selection ruling itself belongs to the USB camera milestone |

Notes:

* The earlier pin `5019680e` came from an upstream branch that no longer exists and is no
  longer advertised by any ref (verified 2026-10-08); the frozen carrier copy above is its
  only local source. That is why the pin is always a SHA and why a local fallback is
  recorded here rather than discovered later. The current pin is reachable from the
  remote at fetch time.
* The host-side interface this project compiles against (`common/usb_hc.h`,
  `core/usbh_core.c`) is byte-identical between the two pins (verified by diff at
  import time), so the multi-controller registration API the port relies on is present
  in both; the tip additionally carries host-side fixes since 2026-09-23.
* At this pin the EHCI port declares `usb_hc_low_level_init`/`usb_hc_low_level_deinit`
  as extern rather than weak: the board glue must provide both.
* `port/xhci/` holds only vendor platform material at this pin (`phytium/`: glue sources and
  prebuilt `libxhci_a*.a` libraries for another vendor's platform). No image here compiles
  it; the xHCI host controller driver this project builds is the replayed patch
  `patches/cherryusb/0001-xhci-host-controller-port.patch`, whose lineage is documented
  there.
* `VERSION` (1.6.1 at the pin), `demo/`, `docs/`, `tests/` and `platform/` are present in
  the working tree but outside every compile set; nothing in the builds refers to them.
* The upstream `osal/` implementations (FreeRTOS, ThreadX, RT-Thread, ...) are not
  compiled: this project uses one CMSIS-RTOS2 backend for both kernels
  (`ports/cherryusb/osal/cmsis2/`).
