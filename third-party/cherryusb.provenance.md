# third-party/cherryusb - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Pin | commit `51fef88da1382a395f7e6c0e907626d9e205abdd` (2026-10-06, `v1.6.1-103-g51fef88d`; SHA-only, no tag) |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | **flat import of the full upstream working tree** (not a submodule), 916 files: every directory the pin carries - `core/`, `common/`, `class/` (all classes), `port/` (all ports, including the vendor ports), `osal/`, `demo/`, `docs/`, `tests/`, `platform/`, `third_party/`, the root build files, `LICENSE`, `VERSION`. The user ruling of 2026-10-26: import everything, so the upstream capability map stays readable and a later consumer does not have to guess what was left out. The file digests are in `third-party/cherryusb/patches/MANIFEST.sha256`; verify from the tree root with `sha256sum -c patches/MANIFEST.sha256` |
| Fetched | 2026-10-26 from the project's reference RTOS tree at commit `19c5736` (submodule `third-party/cherryusb` at the pin, clean working tree), whose provenance records the same pin as the upstream master tip on 2026-10-08 |
| Local differences | **eight patches applied in the tree** (recorded in `patches/`, applied with `patch -p1 --fuzz=0` in name order): `0001-xhci-host-controller-port.patch` - adds the xHCI host-controller driver (`port/xhci/usb_hc_xhci.c/.h`), because the pin ships no usable xHCI host port for this SoC; `0002-ehci-iaad-qh-lifecycle.patch` - keeps the IAA flag consistent with the queue-head slot lifecycle; `0003-ehci-isochronous-transfer.patch` - adds the high-speed isochronous engine (`port/ehci/usb_hc_ehci_iso.c`) that the port declares but the pin does not define, plus two guarded call-site hunks and two frame-cursor fields in `struct ehci_hcd`; `0004-uvc-host-streaming.patch` - adds the host video class's running-stream half (`class/video/usbh_uvc_stream.c/.h`) behind the calls the upstream demos make; `0005-ehci-control-in-cache-invalidate.patch` - invalidates control-IN buffers from the setup packet's direction (endpoint zero's address is 0x00, so the old test skipped every control read); `0006-ehci-kill-iaa-wait.patch` - turns the kill handshake's IAA wait into a real time budget; `0007-ehci-qtd-five-page-chunk.patch` and `0008-xhci-td-sixteen-trb.patch` - the board line's own transfer-size fixes (a 20 KiB receive URB is one qTD chain, not a 16+4 split; the xHCI ring's TD bound) |
| Compiled set | per image: `core/usbh_core.c`, `class/hub/usbh_hub.c`, `port/ehci/usb_hc_ehci.c` and `port/xhci/usb_hc_xhci.c`; storage images add `class/msc/usbh_msc.c`; the camera image adds `class/video/usbh_video.c`, `class/video/usbh_uvc_stream.c` and `port/ehci/usb_hc_ehci_iso.c`, and defines `CONFIG_USB_EHCI_ISO` for that image only. `0006` applies at offset -5 lines and `0003` at offset +25 lines here, which is recorded rather than papered over by fuzz |
| Consumers | `ports/cherryusb/` (the first-party board glue, the CMSIS-RTOS2 OSAL, the MSD application half and the camera application half) and the RTOS bin images (`tests/board/rtos/build/{usb,uvc,rtw89}.mk`) |
| Reason | the RTOS bin platform's USB host stack: the RK3568 board wires two EHCI roots and two xHCI (DWC3) instances, and the rtw89/RTL8851BU line needs a host stack whose OS coupling is one small OSAL and whose DMA-buffer contract this project controls. The reference line had already driven the same pin on this board (MSC, rtw88/rtw89), so the import keeps the proven bytes instead of starting a second stack |

## Notes

* The import is **the full working tree**, per the 2026-10-26 user ruling recorded
  in `AGENTS.md` §1.3: a sparse import loses the upstream capability map, which
  costs more later than the bytes save. The manifest is the import's own byte
  anchor; the compiled set is a per-image build decision, not an import decision.
* Cross-tree evidence (the reference tree's board runs) is `documented` only; it
  is not `verified` for this repository's runs. The camera line's own board
  records are under this repository's measurement records, not in the tree.
* The eight patches are carried as records, not replayed: this repository's
  policy is that an imported tree is compiled in place and `patches/` is the
  patch index (`docs/PATCH-POLICY.md`).
* The pin's `port/xhci/phytium/` vendor material (prebuilt `libxhci_a*.a` for
  another vendor's platform) is present as upstream ships it; no image here
  compiles it - the xHCI host controller driver this line builds is the patched
  `port/xhci/usb_hc_xhci.c`.
* The isochronous engine is high-speed only by design: full/low-speed
  isochronous needs the split-transaction descriptor and periods above one
  frame would need frame-scheduled descriptors; both are rejected explicitly in
  `port/ehci/usb_hc_ehci_iso.c` rather than silently mis-scheduled.
