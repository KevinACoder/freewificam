# components/tlsf - provenance

| Item | Value |
|---|---|
| Upstream | tlsf.baisoku.org, TLSF v3.1 package (2016-04-10); author Matthew Conte, after Miguel Masmano's specification document |
| Pin | none - the package has no canonical git upstream, so the v3.1 package IS the pin (repo-bootstrap C7 ruling: vendored direct copy, the same ruling the prior carrier used) |
| License | **BSD-3-Clause** - the `tlsf.h` copyright header carries the three-clause text including the endorsement clause; there is no separate LICENSE file. The prior carrier's record labelled it "BSD-2"; repo-bootstrap v0.3 errata corrected this against the `tlsf.h` header |
| Vendoring | vendored **whole** (2 files: `tlsf.c`, `tlsf.h`), byte-identical to the frozen prior-carrier copy; contract = `MANIFEST.sha256` (sha256 per file) |
| Fetched | 2026-10-08, from the frozen read-only carrier copy (`~/backups/project_rtos_webcam_261006/freewebcam/components/tlsf/`); md5 `6caed2d3...` / `4fe7a189...` and sha256 recorded on both sides |
| Local differences | none (zero patch): no file was edited |
| Compiled set | one translation unit: `tlsf.c`, compiled into the board shell image through the board heap (`os/threadx/board/rk3568-itx/heap.c`) |
| Consumers | `os/threadx/board/rk3568-itx/heap.c` - the `pvPortMalloc`/`vPortFree` system allocation face |
| Reason | ThreadX has no integrated heap: thread stacks and kernel control blocks come out of this pool, created over the linker's reserved window. Vendored so the board build and its assertions anchor on in-repo bytes plus `MANIFEST.sha256` |

## Notes

* The package is freestanding C: it includes `stddef.h` / `limits.h` / `assert.h` and
  uses no libc allocation. The `assert()` path needs the consumer to provide
  `__assert_func` (the board heap lands it as one emergency line and parks).
* `tlsf.c` references `printf` on its error paths; the board heap provides a
  freestanding `printf` over the console raw outlet, so the reference resolves without
  pulling in a C library.
* `tlsf` is not thread-safe; every entry point in the board heap wraps the call in the
  kernel's public interrupt-control primitive (single core: masking interrupts is the
  whole mutual exclusion).
* The bytes are ASCII; the repository's authoring gate scans this tree's in-tree
  records only (imported-unit rule, same as the other vendored components).
