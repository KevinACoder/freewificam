# components/fatfs - provenance

| Item | Value |
|---|---|
| Upstream | ChaN FatFs R0.16 (2025-07-22), the generic FAT/exFAT filesystem module, `http://elm-chan.org/fsw/ff/` |
| Pin | R0.16 release archive. The unit keeps the seven files listed by the upstream `00readme.txt`: `ff.c`, `ff.h`, `ffunicode.c`, `diskio.h`, `00history.txt`, `00readme.txt`, `LICENSE.txt` |
| License | the FatFs license: BSD-style with a single condition (source redistributions keep the notice, condition and disclaimer), kept verbatim in `LICENSE.txt` at the unit root. See `LICENSES.md` |
| Vendoring | vendored snapshot, byte-identical to the release for every kept file: **7 files**, contract = `MANIFEST.sha256` (sha256 per file). **Not** a git submodule: FatFs ships as a release archive with no upstream git repository to pin |
| Fetched | 2026-10-12; the release archive was cross-checked byte-for-byte against the working copy the predecessor project proved on this board |
| Local differences | none (zero patch): no kept file was edited |
| Compiled set | `ff.c` and `ffunicode.c` compile (the filesystem core and its code-page / Unicode tables), driven by the configuration header that lives in the first-party port `ports/fatfs/ffconf.h` |
| Consumers | `ports/fatfs/` (the first-party adapter: the shadow config header, the block-device seam, `diskio`, the OS glue, the mount face and the shell command family) |
| Reason | exFAT on a 4096-byte-sector volume is the storage milestone's "large window" (the sector size dominates throughput). FatFs is the board-proven filesystem for this stack; importing it byte-identically keeps the auditable baseline in-repo while this project's own code lives in the port |

Notes:

* **No `ffconf.h` here.** FatFs reaches its configuration through `#include "ffconf.h"`,
  resolved relative to the including file first. Upstream ships a template `ffconf.h`;
  this unit deliberately does **not** carry it, so the only `ffconf.h` that exists is the
  first-party shadow in `ports/fatfs/` - the whole of this integration's feature set, and
  the one place `FFCONF_DEF` is matched to the vendored `FF_DEFINED`.
* **No `diskio.c` or `ffsystem.c` here.** Both are upstream *sample* glue whose entire
  purpose is to be replaced by the integrator; `ports/fatfs/diskio.c` and
  `ports/fatfs/fatfs_os.c` are that replacement, written against this project's seams.
* The `MANIFEST.sha256` paths are relative to this directory (bare file names; the unit
  is flat).
