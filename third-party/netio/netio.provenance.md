# netio — provenance

Upstream: https://github.com/kai-uwe-rommel/netio
Pin: `52e83b61587fef908b98f5464aeb7270bfe04346` (NETIO 1.33, 1997-2018, Kai Uwe Rommel)

Vendored **unmodified**: `netio.c`, `getopt.c`, `getopt.h`, `README.md`,
`Makefile`, `netio.doc` — the whole tree ("full tree" rule of AGENTS.md 1.3).
`diff -r` against the pin is empty apart from build artifacts.

## What this tree is for

It is the **pinned peer source**: the throughput ruler's client half, built with
`make linux` and run on the Pi. Keeping it byte-identical to the pin is the
whole point — the peer is the one component that is identical across every arm
(linuxnative / linuxko / lwIP), so it must not drift.

The board side does **not** build from this tree. It uses the MDK-Middleware
netio server ported to lwIP in
`tests/board/rtos/app/cases/net_mdk_netio.c`, which speaks the same wire
protocol (TCP 18767 control, UDP 18768 data, `CMD_QUIT/C2S/S2C/RES`) so the
unmodified upstream client above drives it.

## History

2026-10-29: this tree briefly carried a `NETIO_RTOS_SERVER` port block so the
board could run the *upstream* server. That was retired — the upstream server's
`UDP_Receiver` is spawned unconditionally and its socket-close path takes this
board's kernel down (FreeRTOS `vQueueDelete` assertion with a thread still
parked in the lwIP mailbox). The port block and the case that used it
(`net_netio.c`) are archived in `patches/2026-10-29-xfer-harness-retire/`; the
tree was restored to pristine.
