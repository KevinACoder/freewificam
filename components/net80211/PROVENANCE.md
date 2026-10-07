# components/net80211 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/net_80211.git (the portable net80211 library; its `net80211/` tree is itself a verbatim NetBSD import recorded in `IMPORT-INFO.md`) |
| Pin | commit `9770b1ad7093f43a0f2ab2dc8d980067fb0f62a1`, tag `netbsd-shape-freeze` (2026-09-12); the tag was pushed to the recorded URL on 2026-10-07 and resolves from a fresh clone |
| License | mixed per directory, summarized in `LICENSES/README.md`: `net80211/` BSD-3-Clause (NetBSD, per-file blocks retained), `port/`, `compat/`, `tests/` BSD-2-Clause (the net_80211 authors), `crypto/` NetBSD per-file blocks |
| Vendoring | vendored **trimmed** snapshot, byte-identical to the pin for every kept file: **61 files**, contract = `MANIFEST.sha256` (sha256 per file). Not a git submodule (user ruling 2026-10-07: a component, not third-party) |
| Fetched | 2026-10-07 (full 133-file snapshot), trimmed the same day |
| Local differences | none (zero patch): no kept file was edited; the trim only removes files. The trim itself and the machinery it moved to the port are recorded in `DIVERGENCES.md` |
| Compiled set | `tests/host/net80211-port-test/` (this repository's contract) builds 17 translation units of this component on the port layer (10 stack + 6 `crypto/aes/` + `port/aes_impl_compat.c`), compile-only, zero warnings; the same set links and runs on the ThreadX host runtime |
| Consumers | `ports/net80211/` (the project's port layer: CMSIS-RTOS2 osal, thin mbuf/ifnet shells, the compat machinery headers and the forced-include prelude); planned: the host mock suites (M0-6 / M0-7) |
| Reason | the net80211 stack is the wireless core of the project and the object under test of the host mock suites. It is kept here as a byte-identical, pin-anchored component so assertions can anchor on in-repo paths plus `MANIFEST.sha256`; the OS-facing machinery lives in the port, which is this project's own code |

## Trimmed set (61 files)

| Group | Count | Content |
|---|---|---|
| `net80211/` | 20 | the compiled stack: 10 `.c` (`ieee80211{,_proto,_node,_output,_input,_amrr,_netbsd,_crypto,_crypto_none,_crypto_ccmp}.c`) and the 10 headers they pull |
| `crypto/` | 12 | 6 `.c` + 5 headers + `IMPORT-INFO.md` (the upstream import record) |
| `compat/netbsd/` vocabulary | 24 | the type/vocabulary headers the compile set references: `lib/libkern/libkern.h`, `net/{bpf,if,if_arp,if_dl,if_ether,if_llc,if_media,if_vlanvar,route}.h`, `netinet/in.h`, `sys/{cprng, endian, ioccom, mbuf, malloc, module, param, queue, socket, sockio, sysctl, types}.h` - `sys/cdefs.h` and `sys/errno.h` are owned by the port as forwarding shims (see `ports/net80211/DIVERGENCES.md`), `sys/malloc.h` and `sys/kmem.h` stay here because they only declare the port's allocation API. `netinet/in.h` serves the INET-gated code paths only, so the contract's current flags do not reach it; it stays under the union trim rule and returns to the closure as soon as a consumer defines INET |
| `port/` | 2 | `port.h` (the library's stable port interface) and `aes_impl_compat.c` (part of the compile set) |
| root | 3 | `README.md`, `IMPORT-INFO.md`, `LICENSES/README.md` |
| records (ours) | 3 | `PROVENANCE.md`, `DIVERGENCES.md`, `MANIFEST.sha256` |

## Retired set (72 files) - the pin stays the retrieval point

| Group | Count | Why |
|---|---|---|
| `net80211/` uncompiled | 8 | `ieee80211_{acl,ioctl,rssadapt}.c`, `ieee80211_crypto_{tkip,wep}.c`, `ieee80211_xauth.c`, `ieee80211_radiotap.h`, `ieee80211_rssadapt.h`: outside the compile and link closure. A later consumer (the supplicant's ioctl path, the TKIP/WEP ciphers) fetches them back from the pin and records it |
| `compat/` fat | 39 | all of `dev/**` (18 files, including the 237 KB `pci/if_iwmreg.h` and the 197 KB `usb/usbdevs.h`), `machine/endian.h`, `net/if_types.h`, `netinet/{if_inarp,in_systm,in_var,ip}.h`, `opt_inet.h`, `opt_usb.h`, `sys/{bitops,bus,callout,compat_stub,condvar,conf,device,kauth,pmf,rndsource,stdint,time,workqueue}.h` |
| `compat/` machinery | 10 | `sys/{mutex,intr,atomic,once,systm,kernel,cpu,proc}.h` are replaced by the port's thin machinery headers; `sys/cdefs.h` and `sys/errno.h` are replaced by the port's forwarding shims (both moves are port deviations) |
| `port/` library shells + harness config | 9 | `osal/freertos/**` (the FreeRTOS OSAL: it calls FreeRTOS directly and needs a bus header that is not in this repository), `net/embox/**` (the embox mbuf/ifnet shells), `port_config.h` and `port_config_bsd.h` (the library's forced-include harness config, superseded by the port prelude) |
| `tests/` | 6 | the library's host test script and its helpers: they are bound to the retired `bsd_mbuf.c`. This repository's contract (`tests/host/net80211-port-test/`) supersedes them |

## Trim method

The trim follows the compile-time include closure under the port's declared search
order (port machinery first, then this component's vocabulary headers, then the
component root, then the toolchain), taken as the union of two measurements: the
closure under the previous host include discipline and the closure under the port's
target-shape discipline. A file that either measurement reaches is kept; the union is
what protects the target build from a host-only closure. Two headers changed group during the
implementation (`sys/cdefs.h`, `sys/errno.h`: they are shims and must forward to the
host/target header, so the port owns them) - recorded in `DIVERGENCES.md`. The
per-file list is checked by `MANIFEST.sha256`.

## Rules for changes

* Kept files stay byte-identical. A needed fix to the library body is written as an
  upstreamable patch under `patches/net80211/` (see its README).
* If an in-tree edit is unavoidable, the library's own rule applies
  (`IMPORT-INFO.md`): keep it minimal, mark it with `/* NET80211_PORT(L): ... */`
  and record it in `DIVERGENCES.md`.
* The machinery headers belong to the port: extend `ports/net80211/compat/`, never
  this tree.
