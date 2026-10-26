# ports/cherryusb - the board port of the imported USB host stack

The first-party half of the USB stack: the board glue that binds the imported
CherryUSB controllers to this board's USB power/PHY/reset domain, the
CMSIS-RTOS2 OSAL, the mass-storage application half, and the adapter that maps
the linuxkpi USB engine's backend seam onto CherryUSB's `usbh` transfer calls.

| File | Role |
|---|---|
| `board/rk3568-itx/usb_config.h` | the shadow header every imported header includes by name: bus count, static pool sizes, class instances, the DMA coherency contract (`CONFIG_USB_DCACHE_ENABLE`, 64-byte alignment), the hub thread, the xHCI/EHCI pools |
| `board/rk3568-itx/usb_glue.c/.h` | `usb_board_init(busid)` (domain + controller driver registration + interrupt line), the enumeration event sink, `usb_board_after_start()` (the EHCI connect-change edge a device attached at boot never produces), the register-base query, the DMA-cache primitives and the stack's console sink |
| `osal/cmsis2/osal_cmsis2.c` | the one CMSIS-RTOS2 backend both kernels use; deferred timer callbacks run on a service thread (`usb_osal_timer_service_start()`), because the hub's expiry callback suspends inside a command submit |
| `class/msc_app.c/.h` | the application half of the mass-storage class (`usbh_msc_run()` does the SCSI identity handshake and keeps the ready disk in a small table) |
| `lkpi_usb_cherryusb.c/.h` | the linuxkpi USB backend: `submit_urb`/`kill_urb`/control transfer over `usbh`, the 64-byte-aligned stage pool whose slot size must cover the widest single transfer of the image's driver, and the named counters a bring-up reads |

## Provenance

Imported on 2026-10-26 from the carrier checkout
`~/workspace/project_rtos_webcam/freewificam@19c5736` (documented only, not
this line's verified evidence). Source blob ids at that commit:

| File | Blob |
|---|---|
| `components/linuxkpi/usb/linuxkpi_usb.c` | `21deb1d769ab` |
| `components/linuxkpi/usb/lkpi_usbh_class.c` | `2bfb78aa4000` |
| `ports/cherryusb/lkpi_usb_cherryusb.c` | `707b065bbefa` |
| `ports/cherryusb/board/rk3568-itx/usb_glue.c` | `31fd78fff744` |
| `ports/cherryusb/board/rk3568-itx/usb_config.h` | `8ccad044d59c` |
| `ports/cherryusb/osal/cmsis2/osal_cmsis2.c` | `680e470af818` |
| `ports/cherryusb/class/msc_app.c` | `6a37b1cf3740` |
| `board/rk3568-itx/usb/usb_domain.c` | `5341a8373946` |

## Divergences from the source copy

1. `lkpi_usb_cherryusb.c`: the backend-seam include moved from
   `components/linuxkpi/usb/lkpi_usb_backend.h` (the carrier's layout) to
   `components/linuxkpi/rtos/usb/lkpi_usb_backend.h` (this repository keeps the
   target units under `components/linuxkpi/rtos/`). One line, no behaviour.
2. `board/rk3568-itx/usb_config.h`:
   - `CONFIG_USB_DCACHE_ENABLE` is `#ifndef`-guarded so a diagnostic arm can
     build with the maintenance switched off (the default is unchanged);
   - `CONFIG_USB_EHCI_QTD_NUM` is six per queue head instead of three: the
     storage line's 1 MiB command needs 52 queue elements (20 KiB per element),
     and a 48-element pool would refuse it at the pool, not at the controller;
   - `CONFIG_USB_EHCI_ISO` is 0: this line carries no isochronous consumer, so
     the carrier's `0003` patch is not imported.
3. The board glue's interrupt priority is supplied by the build
   (`-DUSB_BOARD_IRQ_PRIORITY_RAW=BOARD_IRQ_PRIORITY_API_CALL_RAW`) rather than
   left at the SPI default: the completion path gives a semaphore from the
   handler, and a kernel whose port forbids API calls above a threshold needs
   the line moved below it (FreeRTOS's ceiling is `0xd0`).

## What the image must call, and in what order

```
lkpi_core_init();                  /* the system workqueue the deferred driver
                                      paths ride on; without it they are dropped */
wlan_seam_adapter_init();          /* the linuxkpi-side bridge (wireless images) */
lkpi_usb_cherryusb_init();         /* install this adapter behind the seam */
lkpi_usb_init();                   /* walk .lkpi_module, start the drain thread */
lkpi_usb_force_sync_set(false);    /* completion-callback model, not the blocking
                                      storage lane; without it a driver's RX urbs
                                      never run their callbacks (rx=0) */
usb_osal_timer_service_start();    /* before any stack timer can expire */
usb_board_init(busid);             /* domain, HCD registration, IRQ line */
usbh_initialize(busid, base, usb_board_event_handler);
usb_board_after_start(busid);      /* EHCI: manufacture the connect-change edge */
```

`lkpi_usb_force_sync_set(false)` is called before enumeration: the engine's
default is the blocking lane the storage-shaped bring-up used, and a driver
whose receive path resubmits from its completion needs the asynchronous one.
