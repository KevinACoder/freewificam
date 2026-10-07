# components/cherrysh - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/cherry-embedded/CherrySH.git |
| Pin | commit `8efe539c6e55b71d2f2cd1116cd00c4d64222a67` (2026-09-05); `git describe --tags` = `v1.0.1-20-g8efe539`; tags `v1.0.0` (2024-03-28, where `CHANGELOG.md` stops) and `v1.0.1` = `cda6369d7bf938144d1aa49d17bb2ffdc7192145` (2025-02-10) |
| License | Apache-2.0 (root `LICENSE`; `cherryrl/LICENSE` is the same license text kept with its directory for tree fidelity) |
| Vendoring | vendored **subset**, byte-identical to the pin for every kept file: **14 files**, contract = `MANIFEST.sha256` (sha256 per file). Not a git submodule (user ruling 2026-10-07: a component, not third-party) |
| Fetched | 2026-10-07, from the pinned frozen copy of the same commit |
| Local differences | none (zero patch): no kept file was edited. The exclusion set is 81 - 14 = 67 files (see the notes below) |
| Compiled set | five translation units: `chry_shell.c`, `cherryrl/chry_readline.c`, `builtin/{clear,help,shsize}.c`, compiled through the port layer (`ports/cherrysh/`) into the host smoke suite `tests/host/cherrysh-port-smoke/` on both CMSIS-RTOS2 host runtimes. `csh_config_template.h` is kept as the upstream reference; the build includes the port's own `csh_config.h` |
| Consumers | `ports/cherrysh/` (port config, shell port, host console backend); planned: the board image (R2, the board UART console seam) |
| Reason | CherrySH is the console shell of the board line - the command carrier the board gate drives (repo-bootstrap C8). Vendored so host and board assertions anchor on in-repo bytes plus `MANIFEST.sha256`, with every OS touchpoint living in the port |

## Notes

* The subset is self-contained: the core carries no OS primitives and no dynamic
  allocation (no `pthread_*` / FreeRTOS / ThreadX calls, no `malloc`); the only OS
  touchpoints are in the port layer.
* Excluded: `samples/**` (53 board-specific demo ports; their shapes are the port's
  references, the files themselves are not built here), `builtin/login.c` (no login
  on this console), `builtin/lsusb.c` (needs CherryUSB headers, not in this tree yet),
  `cherryrl/example.c` and the demo Makefile/README/branding files, `doc/` images.
* Two kept files carry non-ASCII characters in comments (Unicode arrows):
  `cherryrl/chry_readline.c` and `cherryrl/chry_readline_keycode.h`. The bytes stay
  verbatim, so the repository's ASCII-only authoring gate excludes this imported tree
  and scans the in-tree records only - the same rule the other imported units use.
* Porting facts that the port relies on: commands and variables register through the
  `FSymTab` / `VSymTab` linker sections, and their boundaries come from linker symbols
  (`__fsymtab_start` family); the `sput` / `sget` callbacks are the whole byte-level
  I/O contract. See `patches/cherrysh/README.md` for the change policy.
