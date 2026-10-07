# components/net80211 - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/net_80211.git (the portable net80211 library; its `net80211/` tree is itself a verbatim NetBSD import recorded in `IMPORT-INFO.md`) |
| Pin | commit `9770b1ad7093f43a0f2ab2dc8d980067fb0f62a1`, tag `netbsd-shape-freeze` (2026-09-12); the tag was pushed to the recorded URL on 2026-10-07 and resolves from a fresh clone (`git ls-remote --tags` verified) |
| License | mixed per directory, summarized in `LICENSES/README.md`: `net80211/` BSD-3-Clause (NetBSD, per-file blocks retained), `port/`, `compat/`, `tests/` BSD-2-Clause (the net_80211 authors), `crypto/` NetBSD per-file blocks |
| Vendoring | vendored subset snapshot, byte-identical to the pin: 133 files, contract = `MANIFEST.sha256` (sha256 per file). Not a git submodule (user ruling 2026-10-07: a component, not third-party) |
| Fetched | 2026-10-07, with `git archive <pin> <paths>` from a local checkout of the recorded URL (tracked files only) |
| Local differences | none (zero patch); all 134 files verified byte-identical to the pin at fetch time (sha256 comparison, 0 mismatches) |
| Compiled set | see "Compiled set" below |
| Consumers | `tests/host/net80211-linuxkpi-import/` (host build contract of this round); planned: the vwifi host mock suite (M0-6) and the RTOS ports under `ports/net80211/` (M2-1) |
| Reason | the net80211 stack is the wireless core of the project and the object under test of the host mock suites; fixing its content in-tree lets the mock specs anchor assertions on in-repo paths plus `MANIFEST.sha256` instead of re-vendoring the library per mock round |

## Snapshot layout

Paths inside this directory map one to one onto paths relative to the upstream
repository root. Nothing was renamed, reordered or reformatted.

Included:

* `net80211/` (28 files) - the NetBSD net80211 stack.
* `crypto/` (12 files) - the imported ciphers the stack needs (NetBSD `sys/crypto/aes`).
* `compat/` (73 files) - NetBSD shadow headers and minimal shims (`compat/netbsd/`).
* `tests/` (6 files) - the library's host-built sanitizer tests and their includes.
* `port/` (11 files) - the port interface and the parts the host contracts use:
  `port.h`, `port_config.h`, `port_config_bsd.h`, `aes_impl_compat.c`,
  `osal/freertos/` (FreeRTOS OSAL plus the `compat/` shadow headers the forced
  include needs), `net/embox/` (`bsd_mbuf.c`, `bsd_ifnet.c`).
* `README.md`, `IMPORT-INFO.md`, `LICENSES/README.md` - upstream documentation and
  the license summary.

Excluded (not vendored; the pin remains the retrieval point if a later round needs them).
The pin tree carries 810 files: 133 are vendored here, the 677 below are not.

* `driver/` (106 files; ISC / Realtek notices) - chip drivers are out of scope until the driver milestone.
* `firmware/` (7 files; Realtek binary redistribution) - no firmware blob is materialized or consumed here.
* `third-party/` (353 files) - the library's own vendored copies of FreeRTOS/lwIP/CherryUSB; this project pins those separately.
* `port/rk3568/` (108), `port/bus/` (56), `port/wpa/` (11), `port/utility/` (21) - the library's board/bus/supplicant harness, not part of the portable core.
* `port/Makefile` - the library's board-build harness entry; this repository's host contract re-expresses the flags it needs, and the file names lab machinery that has no place here.
* `port/osal/embox/` (7 files) - the embox OSAL, including 6.3 MB + 845 KB + 84 KB of firmware-embedded C arrays; firmware blobs are not materialized (see `driver/` and `firmware/` above).
* `port/net/lwip/` (3 files) - the lwIP presentation layer, a later milestone's work.
* `cmd/`, `scripts/`, `.gitignore`, `port/.gitignore` - environment-facing helpers and repository housekeeping.

## Compiled set

* `tests/run-host-tests.sh` (run as-is, zero edits): builds and runs two sanitizer
  binaries on the glibc host - the mbuf shim test (`tests/mbuf_trim.c` plus
  `port/net/embox/bsd_mbuf.c`) and the AES-CCM test (`tests/ccm.c`, the
  `crypto/aes/` set, `port/aes_impl_compat.c`, `bsd_mbuf.c`) with
  `-fsanitize=address,undefined`.
* Frozen stack compile set (compile-only, `-c`), driven by
  `tests/host/net80211-linuxkpi-import/Makefile`: 10 stack translation units
  (`net80211/ieee80211*.c` list in that Makefile), 6 `crypto/aes/` units and
  `port/aes_impl_compat.c` (17 total), with the harness flag set (`CFG_BSD` plus
  `INC_NET`, taken from the component's own build files, which are not vendored)
  host-adapted and `-idirafter compat/netbsd` kept so the shadow headers fill only
  the gaps the host libc leaves. No link, no run.
* Not compiled here: `port/osal/**` service code and `port/net/embox/bsd_ifnet.c`
  (they need an RTOS/presentation environment), and everything in the exclusion list.

## Rules for changes

* Files in this snapshot stay byte-identical. A needed fix to the library body is
  written as an upstreamable patch under `patches/net80211/` (see its README) and
  applied only through that channel.
* If an in-tree edit is unavoidable, the library's own rule applies
  (`IMPORT-INFO.md`): keep it minimal, mark it with `/* NET80211_PORT(L): ... */`
  and record it in `DIVERGENCES.md` with file, reason and round.
* The vendored bytes are not subject to this repository's authoring scans (the
  local-knowledge and ASCII gates skip imported unit trees, the same way the
  vendored upstream test tree is skipped): they are copies and cannot be rewritten
  to satisfy authoring rules. This file, `DIVERGENCES.md` and `MANIFEST.sha256` are
  project-authored and stay fully scanned.
