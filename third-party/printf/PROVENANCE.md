# components/printf - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/mpaland/printf |
| Pin | commit `d3b9846` ("chore(readme): 2020 announcement" - the repository's final state; upstream is archived, so this commit is also the end of the tree) |
| License | MIT (`LICENSE` at the tree root, upstream file, copyright 2014 Marco Paland) |
| Vendoring | vendored whole (3 files: `printf.c`, `printf.h`, `LICENSE`), byte-identical to the frozen read-only prior-carrier copy; contract = `MANIFEST.sha256` (sha256 per file) |
| Fetched | 2026-10-08, from the frozen read-only copy of the prior carrier tree (kept outside this repository) |
| Local differences | none (zero patch): no file was edited |
| Compiled set | one translation unit: `printf.c`, compiled into the board images with `PRINTF_DISABLE_SUPPORT_FLOAT` and `PRINTF_DISABLE_SUPPORT_EXPONENTIAL` |
| Consumers | `os/baremetal/printf_bind.c` - the `vsnprintf`/`snprintf`/`sprintf` wrappers the whole image formats through (board log, shell, heap failure path) |
| Reason | The first-party formatter was a deliberate subset (no precision, no `*` width) and bit two callers before; lwIP's own diagnostics and the ICMP ping app format through widths and length modifiers the subset degraded (a `%hu` literal in the ping output was the visible symptom). One auditable full-C99 integer engine replaces piecemeal extension |

## Notes

* Float and exponential rendering are compiled out at the build rule: the image is
  `-mgeneral-regs-only` and has no FP state. Those specifiers degrade like any unknown
  one (the bare specifier character is emitted), which is the same behaviour the subset
  had. Long-long and pointer paths stay on (pure integer code).
* Binding, not aliasing: `printf.h` is deliberately NOT included by its consumer. Its
  standard-name `#define` aliases (`printf` -> `printf_`, ...) would collide with the
  standard-named wrappers `os/baremetal/printf_bind.c` defines and the rest of the
  image links against. That file declares the underscore realization (`vsnprintf_`) directly.
* `printf_`/`sprintf_`/`fctprintf` are not referenced by this project: their sections
  are dropped by `--gc-sections`, which is also why the unbound `_putchar` sink is
  not a link error. A future caller that wants them provides the sink.
* Truncation follows C99: the output is NUL-terminated when size > 0 and the return
  value is the length the complete output would have had.
* The bytes are ASCII; the repository's authoring gate scans this tree's in-tree
  records only (imported-unit rule, same as the other vendored components).