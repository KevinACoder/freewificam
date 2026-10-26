# third-party/cherryusb - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherryUSB.git |
| Pin | commit `51fef88da1382a395f7e6c0e907626d9e205abdd` (2026-10-06, `v1.6.1-103-g51fef88d`; SHA-only, no tag) |
| License | Apache-2.0 (`LICENSE` at the tree root) |
| Vendoring | **flat import of the full upstream working tree** (not a submodule), 913 files: every directory the pin carries - `core/`, `common/`, `class/` (all classes), `port/` (all ports, including the vendor ports), `osal/`, `demo/`, `docs/`, `tests/`, `platform/`, `third_party/`, the root build files, `LICENSE`, `VERSION`. The user ruling of 2026-10-26: import everything, so the upstream capability map stays readable and a later consumer does not have to guess what was left out. The file digests are in `third-party/cherryusb/patches/MANIFEST.sha256`; verify from the tree root with `sha256sum -c patches/MANIFEST.sha256` |
| Fetched | 2026-10-26 from the carrier checkout `~/workspace/project_rtos_webcam/freewificam/third-party/cherryusb` (submodule at the pin, clean working tree), whose provenance records the same pin as the master tip on 2026-10-08 |
| Local differences | **four patches applied in the tree** (recorded in `patches/`, applied with `patch -p1 --fuzz=0` in name order): `0001-xhci-host-controller-port.patch` - adds the xHCI host-controller driver (`port/xhci/usb_hc_xhci.c/.h`), because the pin ships no usable xHCI host port for this SoC; `0002-ehci-iaad-qh-lifecycle.patch` - keeps the IAA flag consistent with the queue-head slot lifecycle; `0005-ehci-control-in-cache-invalidate.patch` - invalidates control-IN buffers from the setup packet's direction (endpoint zero's address is 0x00, so the old test skipped every control read); `0006-ehci-kill-iaa-wait.patch` - turns the kill handshake's IAA wait into a real time budget. `0003` (isochronous engine) and `0004` (UVC streaming) are **not carried**: this line has no isochronous consumer and the board config sets `CONFIG_USB_EHCI_ISO` to 0 |
| Compiled set | per image: `core/usbh_core.c`, `class/hub/usbh_hub.c`, `port/ehci/usb_hc_ehci.c` and `port/xhci/usb_hc_xhci.c`; storage images add `class/msc/usbh_msc.c`. The four patches were generated against this pin; `0006` applies at offset -5 lines here, which is recorded rather than papered over by fuzz |
| Consumers | `ports/cherryusb/` (the first-party board glue, the CMSIS-RTOS2 OSAL and the MSD application half) and the RTOS bin images (`tests/board/rtos/build/usb.mk` and `rtw89.mk`) |
| Reason | the RTOS bin platform's USB host stack: the RK3568 board wires two EHCI roots and two xHCI (DWC3) instances, and the rtw89/RTL8851BU line needs a host stack whose OS coupling is one small OSAL and whose DMA-buffer contract this project controls. The carrier line had already driven the same pin on this board (MSC, rtw88/rtw89), so the import keeps the proven bytes instead of starting a second stack |

## Notes

* The import is **the full working tree**, per the 2026-10-26 user ruling recorded
  in `AGENTS.md` §1.3: a sparse import loses the upstream capability map, which
  costs more later than the bytes save. The manifest is the import's own byte
  anchor; the compiled set is a per-image build decision, not an import decision.
* Cross-tree evidence (the carrier line's board runs) is `documented` only; it
  is not `verified` for this repository's runs.
* The four patches are carried as records, not replayed: this repository's
  policy is that an imported tree is compiled in place and `patches/` is the
  patch index (`docs/PATCH-POLICY.md`).
* The pin's `port/xhci/phytium/` vendor material (prebuilt `libxhci_a*.a` for
  another vendor's platform) is present as upstream ships it; no image here
  compiles it - the xHCI host controller driver this line builds is the patched
  `port/xhci/usb_hc_xhci.c`.
