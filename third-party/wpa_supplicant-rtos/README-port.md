# hostap (NXP wpa_supplicant-rtos) — this import

## Provenance

Pristine source: **https://github.com/nxp-mcuxpresso/wpa_supplicant-rtos**,
branch `release/25.06.00`, commit `c21bbad35c072d53cec51f268029109893846a14`
("Update scr and delete unused files", 2025-06-10).  Lineage: the FreeRTOS port
of hostap (AP MLME: `src/ap/*`, `hostapd/`; STA supplicant: `wpa_supplicant/`)
published by NXP as MCUXpresso SDK middleware.

Imported: the whole tree except `port/` — `src/`, `wpa_supplicant/`,
`hostapd/`, `freertos/`, `Kconfig`, `CMakeLists.txt`, `LICENSE`, `README.md`,
`scr.txt` — 897 files, with upstream's directory layout unchanged.

Not imported: `port/` — 22 files of mbedtls/KSDK hardware-crypto glue
(`port/mbedtls/ksdk/*_alt.c`, `wpa_supp_*_mbedtls_config.h`) for the NXP SDK's
accelerator.  This port runs `CONFIG_WPA_SUPP_CRYPTO=0` with hostap's internal
crypto, so nothing in it is referenced; adding it would also pull an Apache-2.0
mbedtls dependency into an image that does not use it.

The directory is named after its upstream project (`wpa_supplicant-rtos`), not
`hostap`: the tree carries both halves — the STA supplicant (`wpa_supplicant/`)
and hostapd's AP MLME (`hostapd/`, `src/ap/`) — and this round compiles only the
AP half.

## Verification

`patches/MANIFEST.sha256` holds the sha256 of every imported file **as it is
upstream**.  `patches/*.patch` are this port's only changes, applied in order:

    cd <this dir> && for p in patches/*.patch; do patch -p1 < "$p"; done

That reproduces the working tree from the pristine tree; nothing else differs.

| patch | why |
|---|---|
| 0001 includes: the `CONFIG_OS_LKPI` branch | errno/timeval/lwIP IP types/AF_*/IFNAMSIZ, no sockets, no NXP SDK headers, PSK-only feature set |
| 0002 build_config: `CONFIG_OS_LKPI` guard | the port config header owns every `CONFIG_*`; the SDK's `wifi_config_default.h` does not exist here |
| 0003 common: the `CONFIG_OS_LKPI` branch | plain libc stdio, no `fsl_debug_console.h`, endian helpers for aarch64 |
| 0004 drivers: register the lkpi driver | `wpa_driver_lkpi_ops` in `wpa_drivers[]`; the SDK's `driver_freertos` stays out of an LKPI image |
| 0005 eloop_freertos: no signals, no select | no POSIX signals on this port; `eloop_wait_for_read_sock` has no socket to wait on |
| 0006 supp_main: the AP entry | the AP image has no supplicant STA interface; `hostapd_get_interface()` must read an empty registry as NULL; the thread stack is the port's |
| 0007 os_freertos: `os_zalloc` zeroes | `OSA_MemoryAllocate` (pvPortMalloc) does not zero, so a recycled block handed hostapd a `struct sta_info` with a bogus AID and a stray AUTHORIZED bit — silently skipping the station's authorization |
| 0008 os.h: `os_reltime_sub` normalises | upstream has the borrow behind `#if 0`, which leaves a negative usec; the AP's timeout math needs it |
