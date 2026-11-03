# The USB port: one port, four module homes

The first-party half of the USB stack used to sit under a top-level `ports/`
directory.  It is now filed by module, and this note is the map:

| Module | Contents | Role |
|---|---|---|
| `tests/board/rtos/platform/board/rk3568-itx/usb/` | `usb_config.h`, `usb_glue.c/.h` | the board glue: the shadow header every imported header includes by name (bus count, static pool sizes, class instances, the DMA coherency contract, the hub thread, the xHCI/EHCI pools); `usb_board_init(busid)` (domain + controller driver registration + interrupt line), the enumeration event sink, `usb_board_after_start()` (the EHCI connect-change edge a device attached at boot never produces), the register-base query, the DMA-cache primitives and the stack's console sink.  It sits next to the board's USB power/PHY/reset layer (`usb_domain.c`), which it drives |
| `third-party/cherryusb/ports/osal/cmsis2/` | `osal_cmsis2.c` | the one CMSIS-RTOS2 backend both kernels use; deferred timer callbacks run on a service thread (`usb_osal_timer_service_start()`), because the hub's expiry callback suspends inside a command submit |
| `tests/board/rtos/app/usb/` | `msc_app.c/.h`, `uvc_app.c/.h` | the application halves of the mass-storage and video classes (`usbh_msc_run()` does the SCSI identity handshake and keeps the ready disk in a small table; the camera half owns the bring-up, the shared open state and the 640x480 MJPEG alt4 policy, and never closes the camera - see the file header) |
| `components/linuxkpi/rtos/usb/` | `lkpi_usb_cherryusb.c/.h` | the linuxkpi USB backend: `submit_urb`/`kill_urb`/control transfer over `usbh`, the 64-byte-aligned stage pool whose slot size must cover the widest single transfer of the image's driver, and the named counters a bring-up reads |

No file under `third-party/cherryusb/` itself is first-party except the OSAL in
its `ports/` directory, matching the other imported trees (`third-party/lwip/ports`,
`third-party/fsl_sdmmc/ports`, `third-party/threadx/ports`).

## Provenance

Imported on 2026-10-26 from the project's reference RTOS tree at commit
`19c5736` (documented only, not this line's verified evidence). Source blob
ids at that commit (paths are the reference tree's; the current homes are the
table above):

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

The camera application half is a new file for this repository; its behaviour
follows the reference tree's camera image at the same commit
(`tests/board/threadx-rk3568-uvc/tests/uvc_app.c`, blob `ff6ba4b263bc`), with
the bus bring-up and the wait moved in from that image's cases and the
`board_suite` frame replaced by this repository's case interface. The four
camera cases follow that image's four steps, with the same blob lineage:

| Case | Origin |
|---|---|
| `app/cases/uvc_detect.c` | the reference image's `usb-host-up` step (`889396cbb65b`) |
| `app/cases/uvc_open.c` | its `uvc-open` step (`411cbfc20b5c`) |
| `app/cases/uvc_iso_smoke.c` | its `uvc-iso-smoke` step (`58ec8de72d8b`) |
| `app/cases/uvc_stream.c` | its `uvc-stream` step (`883531d5874a`) |

## Divergences from the source copy

1. `lkpi_usb_cherryusb.c`: the backend-seam include moved from
   `components/linuxkpi/usb/lkpi_usb_backend.h` (the reference tree's layout) to
   `components/linuxkpi/rtos/usb/lkpi_usb_backend.h` (this repository keeps the
   target units under `components/linuxkpi/rtos/`). One line, no behaviour.
2. `board/rk3568-itx/usb_config.h`:
   - `CONFIG_USB_DCACHE_ENABLE` is `#ifndef`-guarded so a diagnostic arm can
     build with the maintenance switched off (the default is unchanged);
   - `CONFIG_USB_EHCI_QTD_NUM` is six per queue head instead of three: the
     storage line's 1 MiB command needs 52 queue elements (20 KiB per element),
     and a 48-element pool would refuse it at the pool, not at the controller;
   - `CONFIG_USB_EHCI_ISO` is left undefined here: the camera image defines it
     and compiles the isochronous engine (`build/uvc.mk`), so the other images
     neither link the engine nor pay for its per-bus state;
3. `lkpi_usb_cherryusb.c`: the backend seam gains a `poll` hook
   (`chu_seam_poll()`) that runs `USBH_IRQHandler(busid)` for every started
   xHCI bus inside `usb_osal_enter_critical_section()`, installed as
   `.poll` in `chu_ops`. It exists because the 2026-11-01 8188EU round
   measured the event interrupt of this controller being lost while
   `usbsts.EINT` stayed pending (`runs/wireless/2026-11-01-W2-01-wedge/`);
   the engine calls it only when a transfer is overdue, so the interrupt
   stays the fast path and this is its fallback.
4. The board glue's interrupt priority is supplied by the build
   (`-DUSB_BOARD_IRQ_PRIORITY_RAW=BOARD_IRQ_PRIORITY_API_CALL_RAW`) rather than
   left at the SPI default: the completion path gives a semaphore from the
   handler, and a kernel whose port forbids API calls above a threshold needs
   the line moved below it (FreeRTOS's ceiling is `0xd0`).

## The camera image

`tests/board/rtos/configs/uvc.mk` + `build/uvc.mk` are the camera's whole
build-side surface: the profile names the controller (`CONFIG_UVC_USB_BUS`),
the build adds `-DCONFIG_USB_EHCI_ISO=1`, the video class, the stream engine
and the isochronous engine, and the image's gate checks the four symbols that
must be present (`ehci_iso_urb_init`, `usbh_video_stream_start`,
`video_ctrl_class_info`, `video_streaming_class_info`).

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
