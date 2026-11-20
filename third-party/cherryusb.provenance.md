# third-party/cherryusb - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Pin | commit `51fef88da1382a395f7e6c0e907626d9e205abdd` (2026-10-06, `v1.6.1-103-g51fef88d`; SHA-only, no tag) |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | **compiled subset, not the full upstream working tree**: 82 files carrying `core/`, `common/`, `class/{hub,msc,video}/`, `port/{dwc2,ehci,xhci}/`, `osal/`, `ports/osal/cmsis2/`, `patches/` and `LICENSE`. The previous line's full-tree import (919 files) is represented by `patches/MANIFEST.sha256` and `patches/README.md` as a historical record; it does not verify this subset. The DWC2 register/parameter files below were copied byte-identical from the same pin; `usb_hc_dwc2.c` carries patch 0011. |
| Fetched | 2026-10-26 from the project's reference RTOS tree at commit `19c5736` (submodule `third-party/cherryusb` at the pin, clean working tree), whose provenance records the same pin as the upstream master tip on 2026-10-08. The three `port/dwc2` files were copied on 2026-11-15 from the same-pin full tree in the sibling line's checkout, whose `patches/MANIFEST.sha256` carries the same digests. |
| Local differences | **eleven patches applied in the tree** (recorded in `patches/`, applied with `patch -p1 --fuzz=0` in name order): `0001-xhci-host-controller-port.patch` - adds the xHCI host-controller driver (`port/xhci/usb_hc_xhci.c/.h`), because the pin ships no usable xHCI host port for this SoC; `0002-ehci-iaad-qh-lifecycle.patch` - keeps the IAA flag consistent with the queue-head slot lifecycle; `0003-ehci-isochronous-transfer.patch` - adds the high-speed isochronous engine (`port/ehci/usb_hc_ehci_iso.c`) that the port declares but the pin does not define, plus two guarded call-site hunks and two frame-cursor fields in `struct ehci_hcd`; `0004-uvc-host-streaming.patch` - adds the host video class's running-stream half (`class/video/usbh_uvc_stream.c/.h`) behind the calls the upstream demos make; `0005-ehci-control-in-cache-invalidate.patch` - invalidates control-IN buffers from the setup packet's direction (endpoint zero's address is 0x00, so the old test skipped every control read); `0006-ehci-kill-iaa-wait.patch` - turns the kill handshake's IAA wait into a real time budget; `0007-ehci-qtd-five-page-chunk.patch` and `0008-xhci-td-sixteen-trb.patch` - the board line's own transfer-size fixes (a 20 KiB receive URB is one qTD chain, not a 16+4 split; the xHCI ring's TD bound); `0009-host-msc-modeswitch.patch` - the ZeroCD mode-switch support (hub force-reenumerate + MSC CBW-only vendor-command macros), from the qwer-zxcv fork's `9c7c2ce`, for the AIC8800 line's `1111:1111` dongle; `0010-usbh-aic8800-class.patch` - the thin AIC8800 vendor class (`class/vendor/wifi/usbh_aic8800.c/.h` + `demo/usbh_aic8800_template.c`), from the same fork's `da9d3a3`; `0011-dwc2-isochronous-transfer.patch` - adds the DWC2 full-speed isochronous engine (one packet per channel activation, per-packet `actual_length`/`errorcode`, one channel per endpoint), selected by `CONFIG_USB_DWC2_ISO` for the F429 UVC images. Patches 0009/0010 are formatted from the fork (commit pair noted in each file header); the rest are this repository's own. **`port/dwc2/usb_hc_dwc2.c` is the pin's bytes plus patch 0011** (its two companions, `usb_dwc2_param.h`/`usb_dwc2_reg.h`, are still byte-identical; the pin's `usb_hc_dwc2.c` SHA256 is `d3cd15213899f0bf4517cf3a1d06416980ff6bf7f637e24479a1c5f2fdf36732`). **One imported core file has a recorded local change**: `core/usbh_core.c` uses `1 << bMaxPacketSize0` only for a SuperSpeed port (a USB3 device on this FS host must use the USB 2 EP0 maximum, 64); see `docs/DIVERGENCES.md` row 66. |
| Compiled set | per image: `core/usbh_core.c`, `class/hub/usbh_hub.c`, `port/ehci/usb_hc_ehci.c` and `port/xhci/usb_hc_xhci.c` (firefly v2 images drop the EHCI unit: the board runs xHCI only); storage images add `class/msc/usbh_msc.c`; the aic8800 image adds `class/msc/usbh_msc.c` (the ZeroCD switch rides the MSC connect path) + `class/vendor/wifi/usbh_aic8800.c`; the F429 camera images add `class/video/usbh_video.c`, `class/video/usbh_uvc_stream.c` and the DWC2 iso engine selected by `CONFIG_USB_DWC2_ISO` (`build/uvc.mk`); the older RK3568 EHCI camera line is not carried by this tree. **F429 images** compile `core/usbh_core.c`, `class/hub/usbh_hub.c`, `port/dwc2/usb_hc_dwc2.c`, `class/msc/usbh_msc.c`, the first-party MSC app and `app/cases/usb_dwc2.c`; the F429 UVC images replace the storage class with `class/video/usbh_video.c` + `usbh_uvc_stream.c`, the first-party `uvc_app.c` and the four `app/cases/uvc_*.c`; they are single-controller, so `CONFIG_USBHOST_MULT_HC` stays undefined and the DWC2 port supplies `usb_hc_init`/`usbh_submit_urb`/`USBH_IRQHandler` directly. `0006` applies at offset -5 lines and `0003` at offset +25 lines here, which is recorded rather than papered over by fuzz. |
| Consumers | `components/linuxkpi/rtos/usb/`, `tests/board/rtos/platform/board/{rk3568-itx,firefly/v2,stm32f429-challenger}/usb/`, `tests/board/rtos/app/usb/` and `third-party/cherryusb/ports/` (the first-party adapter, board glue, class halves and the CMSIS-RTOS2 OSAL) and the RTOS bin images (`tests/board/rtos/build/{usb,uvc,rtw89,aic8800}.mk`; the F429 source selection is in `usb.mk`'s board branch). |
| Reason | the RTOS bin platform's USB host stack: the RK3568 board wires two EHCI roots and two xHCI (DWC3) instances, the F429 board wires one OTG_HS core in full-speed host mode, and the wireless lines need a host stack whose OS coupling is one small OSAL and whose DMA-buffer contract this project controls. The reference line had already driven the same pin on the RK3568 board (MSC, rtw88/rtw89), and the F429 host path is the pin's own DWC2 port with a first-party board glue. |

## Notes

* The tree is the **compiled subset**, not the full working tree. The previous
  line's 919-file import is kept as a historical manifest in `patches/`; this
  line's provenance is this table plus the source digests above. A file not in
  the compiled subset is not in this tree.
* The F429 DWC2 port is the pin's host controller driver plus patch 0011's
  isochronous engine (`CONFIG_USB_DWC2_ISO`); every non-camera F429 image
  compiles the port with the engine's code left out. It requires the
  core's internal-DMA architecture (`GHWCFG2.arch == 2`); the F429 OTG_HS core
  reports `GSNPSID=0x4f54281a` and `GHWCFG2=0x229ed590`, which is INT DMA. The
  board supplies `dwc2_get_user_params`, `usb_hc_low_level_init` and
  `usb_hc_low_level_deinit` in first-party glue; upstream's `usb_glue_st.c` is
  not imported because it pulls STM32 HAL and the CubeMX generated init.
* Cross-tree evidence (the reference tree's board runs) is `documented` only; it
  is not `verified` for this repository's runs. The camera line's own board
  records are under this repository's measurement records, not in the tree.
* The patches are carried as records, not replayed: this repository's policy is
  that an imported tree is compiled in place and `patches/` is the patch index
  (`docs/PATCH-POLICY.md`).
* The pin's `port/xhci/phytium/` vendor material (prebuilt `libxhci_a*.a` for
  another vendor's platform) is present as upstream ships it; no image here
  compiles it - the xHCI host controller driver this line builds is the patched
  `port/xhci/usb_hc_xhci.c`.
* The isochronous engine is high-speed only by design: full/low-speed
  isochronous needs the split-transaction descriptor and periods above one
  frame would need frame-scheduled descriptors; both are rejected explicitly in
  `port/ehci/usb_hc_ehci_iso.c` rather than silently mis-scheduled.
