# third-party/lwip - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/lwip-tcpip/lwip.git |
| Pin | tag `STABLE-2_2_1_RELEASE` = commit `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (the annotated tag `STABLE-2_2_1_RELEASE`) |
| License | BSD-3-clause style: `COPYING` at the tree root holds the Swedish Institute of Computer Science three-clause block. `contrib/apps/ping/` carries its own BSD-2 style header inside the same tree. |
| Vendoring | **full flat import** (not a submodule) of the upstream working tree at the pin (2026-10-26): every file the pin carries (750 files, including `contrib/ports/`, `contrib/examples/`, `contrib/addons/`, `doc/` and `test/`). `third-party/lwip/patches/MANIFEST.sha256` lists every carried file. The image compiles `src/`, the contrib ping application, the first-party CMSIS-RTOS2 port under `third-party/lwip/ports/` and one applied patch (`0003`); the rest is present as upstream reference, per `AGENTS.md` §1.3. |
| Fetched | 2026-10-24, from the pinned checkout recorded by the RTOS reference tree (commit `19c5736`); the pin above was verified at import. |
| Local differences | **one patch**, applied in the tree (`0003-tcp-ack-delay-watermark.patch`); the record is `third-party/lwip/patches/`. The port adapts through first-party files under `third-party/lwip/ports/` and configuration only. |
| Compiled set | `src/core/` (20 units), `src/core/ipv4/` (9), `src/api/` (9), `src/netif/ethernet.c`, `src/apps/lwiperf/lwiperf.c`, `contrib/apps/ping/ping.c` - the group lists follow `src/Filelists.mk`. |
| Consumers | `third-party/lwip/ports/` (the single CMSIS-RTOS2 `sys_arch`, the compile abstraction header, the stack configuration, the platform hook file, the GMAC netif bridge) and the network case of the board images. |
| Reason | lwIP is the network presentation layer of the RTOS platform: the board line needs a small TCP/IP stack whose OS coupling is one adapter over CMSIS-RTOS2, so one implementation serves both kernels. |

Notes:

* The stack's memory model stays the static pools of `src/core/mem.c` and
  `src/core/memp.c`: the target image is freestanding, so the pools are sized in
  `third-party/lwip/ports/include/lwipopts.h` and visible at link time.
* The include order puts `third-party/lwip/ports/include/` ahead of `src/include/`: that
  shadowing is how `lwipopts.h` and `arch/cc.h` are supplied, the standard lwIP
  port mechanism.
* The ACK-delay watermark (`0003`) is carried and applied in the tree; the
  remaining series (`0001` lwiperf boundary, `0004` unsent-tail cache, `0006`
  Nagle) is parked under `third-party/lwip/patches/unadopted/` and not compiled.
* `LWIP_TIMEVAL_PRIVATE` is zero in this project's `lwipopts.h`: the toolchain's
  libc supplies `struct timeval`, and lwIP's private copy collides with it.