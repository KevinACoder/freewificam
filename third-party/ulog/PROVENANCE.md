# components/ulog - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/rdpoor/ulog |
| Pin | commit `fc3b8c26dc296225439470771751e0786fd59be0` (master tip, 2025-02-10, "Merge pull request #15 from tecera/feature/do-not-evaluate"; 29 commits, single active branch `master`) |
| License | MIT (`LICENSE` at the tree root, upstream file, Copyright (c) 2019 Robert Poor) |
| Vendoring | vendored whole (3 files: `src/ulog.c`, `src/ulog.h`, `LICENSE`), byte-identical to the pin; contract = `MANIFEST.sha256` (sha256 per file). Not a submodule: the small-component form, same as `components/printf/` |
| Fetched | 2026-10-11, `git clone https://github.com/rdpoor/ulog.git` at the pin above |
| Local differences | none (zero patch): no vendored byte was edited; the sha256 contract holds |
| Compiled set | one translation unit, `src/ulog.c`, compiled into every image that compiles `os/baremetal/log.c` (six board images and the `ulog-level-test` host suite), with `-DULOG_ENABLED -DULOG_MAX_MESSAGE_LENGTH=256` |
| Consumers | `os/baremetal/log.c` - the only entry shell (print lock, anchor promotion, outlet subscription). `components/linuxkpi` borrows only the `ulog_level_t` type and the level constants for its print-helper mapping |
| Reason | Named by the user as the logging engine. MIT, two files, "aggressively standalone" (only stdio/string/stdarg), seven levels with a per-subscriber threshold and a compile-time master gate - exactly the shape a single level mechanism needs. The level vocabulary and the dispatch loop are the vendored engine's, so the project does not grow a second level implementation |

## Notes

* The engine keeps one static message buffer, so the buffer is only valid until the next
  call and the engine is not re-entrant. The shell therefore takes the print lock at the
  entry point (`log_emit`) and the subscriber does not lock again; the lock primitive
  stays in `os/baremetal/log.c`.
* `ULOG_MAX_MESSAGE_LENGTH` is raised to 256 to line up with the outlet's assembly buffer
  (`CONSOLE_LINE_MAX`), so the truncation point does not move.
* The subscriber typedef in the upstream header is `void (*)(ulog_level_t, char *msg)`
  while the upstream documentation example uses `const char *`; the header wins. The
  divergence is recorded in `DIVERGENCES.md` and the vendored bytes are left alone.
* The repository's authoring gate scans this tree's in-tree records only (imported-unit
  rule, same as the other vendored components); the vendored bytes are excluded from the
  ASCII and local-knowledge scans.
