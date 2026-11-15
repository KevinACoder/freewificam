# ports/usb-rk3568 provenance

Imported from the previous line's tree at commit `fdceccb9`
(`project_rea_wcam/freewificam`), path
`components/linuxkpi/rtos/usb/lkpi_usb_cherryusb.c`.

Files: `adapter.c` (renamed from `lkpi_usb_cherryusb.c`), `lkpi_usb_cherryusb.h`.

License: BSD-3-Clause (SPDX lines present).

Local differences: the adapter's three include paths were rewritten for this
component's layout (`docs/DIVERGENCES.md` row 57); the header is byte-identical.
It is the seam between linuxkpi's USB engine and the imported CherryUSB host
stack plus this board's USB glue (`tests/board/rtos/platform/board/rk3568-itx/usb/`),
and it is compiled only when the image selects `CONFIG_LIBLINUX80211_USB`.
