# net_80211

NetBSD net80211 wireless stack and chip drivers as a portable C library,
with per-OS "port" adapters. The protocol stack and the chip drivers are
imported verbatim from NetBSD (see `IMPORT-INFO.md`); all OS dependencies
are funneled through shadow headers in `compat/netbsd/` and through the
stable interface in `port/port.h`.

## Layout

```
net80211/        NetBSD sys/net80211 (verbatim import)
driver/          Chip drivers, one directory per chipset (see driver/README.md)
  urtwn/         Realtek RTL8188EU/8188EUS USB (NetBSD if_urtwn, verbatim import)
firmware/        Per-driver firmware blobs with their licenses (see firmware/README.md)
compat/netbsd/   Shadow headers and minimal shims for the NetBSD kernel API
crypto/          Imported ciphers the stack needs (NetBSD sys/crypto/aes)
tests/           Host-built sanitizer tests for the shims (see tests/run-host-tests.sh)
cmd/             Environment-facing helpers (the embox `wlan` shell command)
port/
  port.h         The stable, bus-agnostic port interface: firmware lookup,
                 presentation hooks, the wlan_chip_driver registry and the
                 port lifecycle
  osal/          OS adaptations, one directory per OS
    embox/       Locks/threads/timers, the BSD mbuf and ifnet shells,
                 embedded firmware lookup (embox)
  bus/           Bus backends, one directory per bus type
    usb/
      port_usb.h  The USB bus types of the port interface
      cherryusb/  CherryUSB host stack (vendored) + embox OSAL + RK3568
                  EHCI glue + the usbd_* shim
    pcie/        Reserved for a future PCIe bus backend
    sd/          Reserved for a future SDIO bus backend
  net/           Presentation layers, one directory per host network stack
    embox/       BSD ifnet/mbuf shells mapped onto the embox net stack
    lwip/        Reserved for a future lwIP netif presentation
scripts/         Helper scripts (firmware array generation)
```

This repository holds the portable library only. Building it for a
given OS is the integrator's job: the embox tree, for example, carries
a third-party/net80211 external-project description that pulls this
repository and compiles the ports above into its build.

## Adding a port

A full port combines one directory from each category, all described by
`port/port.h`:

- an `osal/<os>/` adaptation implementing the compat/netbsd/
  declarations (memory, locks, threads, timers) and the firmware lookup;
- a `bus/<bus>/<backend>/` backend that enumerates the bus, claims
  matching devices and drives the transfers (USB backends implement
  the types in `port/bus/usb/port_usb.h`);
- a `net/<stack>/` presentation layer registering the frame hooks
  (eapol/data rx, xmit, hwaddr) and the event handler, plus one
  `wlan_port_init()` call from the environment that walks the chip
  driver registry.

## Adding a chip driver

Create `driver/<name>/`, import the OS-neutral driver sources, and expose
one `wlan_chip_driver` describing the bus match table and the attach
hooks. Register it from the driver's own translation unit; ports pick up
registered drivers without further changes. See `driver/README.md`.

## Licenses

- `net80211/`: BSD-2/3-Clause (Atsushi Onoe, Sam Leffler / Errno
  Consulting, and others) - original headers retained per file.
- `driver/urtwn/`: ISC (Damien Bergamini, Kevin Lo, Stefan Sperling,
  Nathanial Sloss) - original headers retained per file.
- `firmware/`: Realtek proprietary firmware, binary redistribution only;
  the license text ships next to each blob.
- Everything written for this repository: BSD-2-Clause, copyright the
  net_80211 authors.
