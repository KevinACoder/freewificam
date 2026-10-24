# third-party/lwip - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/lwip-tcpip/lwip.git |
| Pin | tag `STABLE-2_2_1_RELEASE` = commit `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (the annotated tag `STABLE-2_2_1_RELEASE`) |
| License | BSD-3-clause style: `COPYING` at the tree root holds the Swedish Institute of Computer Science three-clause block. `contrib/apps/ping/` carries its own BSD-2 style header inside the same tree. |
| Vendoring | **sparse flat import** (not a submodule): `src/` in full, the contrib ping application and `COPYING`, byte-identical to the pin. `patches/lwip/MANIFEST.sha256` lists every carried file. Not carried: `contrib/ports/` (upstream's FreeRTOS, unix and other ports - the OS interface here is the first-party CMSIS-RTOS2 adapter), `contrib/examples/`, `contrib/addons/`, `doc/`, `test/`. |
| Fetched | 2026-10-24, from the pinned submodule checkout recorded by the RTOS reference tree (commit `19c5736`); the submodule HEAD was verified to be the pin above at import. |
| Local differences | none (zero patch). The port adapts through first-party files under `rtos/ports/lwip/` and configuration only; the tree is never edited. |
| Compiled set | `src/core/` (20 units), `src/core/ipv4/` (9), `src/api/` (9), `src/netif/ethernet.c`, `src/apps/lwiperf/lwiperf.c`, `contrib/apps/ping/ping.c` - the group lists follow `src/Filelists.mk`. |
| Consumers | `rtos/ports/lwip/` (the single CMSIS-RTOS2 `sys_arch`, the compile abstraction header, the stack configuration, the platform hook file, the GMAC netif bridge) and the network case of the board images. |
| Reason | lwIP is the network presentation layer of the RTOS platform: the board line needs a small TCP/IP stack whose OS coupling is one adapter over CMSIS-RTOS2, so one implementation serves both kernels. |

Notes:

* The stack's memory model stays the static pools of `src/core/mem.c` and
  `src/core/memp.c`: the target image is freestanding, so the pools are sized in
  `rtos/ports/lwip/include/lwipopts.h` and visible at link time.
* The include order puts `rtos/ports/lwip/include/` ahead of `src/include/`: that
  shadowing is how `lwipopts.h` and `arch/cc.h` are supplied, the standard lwIP
  port mechanism.
* The wired line's tuning series (five patches under the reference tree's
  `patches/lwip/`: lwiperf boundary, TCP probe hook, ACK-delay watermark, unsent-tail
  cache, TCP event hook) is **not carried** here. This round measures the untuned
  stack; the two hooks `lwipopts.h` points at are behind `CONFIG_*` levers that stay
  undefined, so no patched source is needed to build.
* `LWIP_TIMEVAL_PRIVATE` is zero in this project's `lwipopts.h`: the toolchain's
  libc supplies `struct timeval`, and lwIP's private copy collides with it.