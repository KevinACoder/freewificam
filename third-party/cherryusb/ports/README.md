# third-party/cherryusb/ports

The first-party port of the imported CherryUSB host stack, filed here the same
way `third-party/lwip/ports/` and `third-party/fsl_sdmmc/ports/` are:

- `osal/cmsis2/osal_cmsis2.c` - the one CMSIS-RTOS2 OSAL both kernels use.

The rest of the port lives with the module that owns it: the board glue in
`tests/board/rtos/platform/board/rk3568-itx/usb/`, the linuxkpi USB backend in
`components/linuxkpi/rtos/usb/`, and the class application halves in
`tests/board/rtos/app/usb/`. `docs/USB-PORT.md` is the map, including the
provenance and the divergences.
