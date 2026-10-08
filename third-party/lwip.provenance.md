# third-party/lwip - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/lwip-tcpip/lwip.git |
| Pin | tag `STABLE-2_2_1_RELEASE` = commit `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (2025-02-05, "Prepare 2.2.1 release"); the tag is annotated (peeled commit `009c2256`), the pin is the commit the tag points at. 2.2.1 is the newest STABLE tag as of the import |
| License | BSD-3-clause style: `COPYING` at the tree root holds the Swedish Institute of Computer Science three-clause block, no additional terms. `contrib/apps/ping/` carries its own BSD-2 style header inside the same tree, so it adds no import unit |
| Vendoring | git submodule; the gitlink is locked to the commit above and the tree is consumed byte-identical (full checkout - the compile set spans `src/`, `contrib/apps/` and the headers; no sparse set is needed) |
| Fetched | 2026-10-08 from the upstream remote (tag advertised at fetch time) |
| Local differences | none (zero patch). The port adapts through first-party files under `ports/lwip/` and configuration only; the tree is never edited |
| Compiled set | `src/core/` 20 files, `src/core/ipv4/` 9 files, `src/api/` 9 files, `src/netif/ethernet.c`, `src/apps/lwiperf/lwiperf.c`, `contrib/apps/ping/ping.c` (the group lists follow the upstream `src/Filelists.mk` groups); plus the first-party port layer |
| Consumers | `ports/lwip/` (the single CMSIS-RTOS2 sys_arch implementation, the compile abstraction header, the stack configuration, the platform hook file) and the ThreadX board regression image |
| Reason | lwIP is the network presentation layer of the wireless milestone: the portable net80211 library keeps a reserved lwIP netif slot, and the aarch64 board line needs a small TCP/IP stack whose OS coupling is one adapter over CMSIS-RTOS2, so one implementation serves every kernel |

Notes:

* Several files' licence is settled by the three-clause header block; `src/Filelists.mk` (BSD-3) is the authority for the file groups, and the group lists in the port build mirror it.
* The stack's memory model stays the static pools of `src/core/mem.c` and `src/core/memp.c`: the target image is freestanding, so the pools are sized in configuration and visible at link time. The `MEM_CUSTOM_ALLOCATOR` interface this release adds is the recorded switch-over path for the later unified allocator, unused for now.
* The include order used by the port build puts the first-party include directory ahead of `src/include/`: that shadowing is how `lwipopts.h` and `arch/cc.h` are supplied, the standard lwIP port mechanism (`lwip/opt.h` and `lwip/arch.h` include them by these names).
* The import compiles and links with no file from `contrib/ports/` except the vendored tree's own bytes being ignored: the OS interface is the first-party CMSIS-RTOS2 adapter, not the upstream FreeRTOS or unix ports.
