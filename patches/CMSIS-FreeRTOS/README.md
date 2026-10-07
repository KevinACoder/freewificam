# patches/CMSIS-FreeRTOS - local patches replayed onto the vendored pin

| Upstream | https://github.com/ARM-software/CMSIS-FreeRTOS |
| Base (= pin) | tag `v11.3.0`, commit `4dd0035148cdc106f892877c27cba77d1de717d2` |
| Patch list | `0001-cmsis_os2-do-not-launder-handles-through-32-bit-integers.patch` - the adapter round-trips mutex/timer handles through `uint32_t`, which truncates them on any 64-bit target (mutex acquire faults, the timer callback dispatch dereferences a truncated pointer); the patch uses `uintptr_t` for those round trips, no API/ABI change |
| Submission status | **not submitted** (no GitHub credentials in this environment; the patch is written in `git format-patch` form and the PR is ready to open against the pin above) |

Rules (project patch policy):

* The vendored snapshot itself stays byte-identical. A patch is applied only through
  this channel: `tools/patches/replay.sh CMSIS-FreeRTOS` materialises
  `tools/mock/build/patched/CMSIS-FreeRTOS/` (copy + `git apply`) at build time and every
  consumer compiles from the printed path - never from the vendored tree when a patch
  exists, and never by editing the submodule.
* One concern per patch, English, `Signed-off-by`, no local knowledge (the private
  knowledge gate scans `patches/` too).
* Why upstream may not accept as-is: the change is mechanical and behaviour-preserving,
  so the likely objection is none; the risk is only the review round-trip. If the
  maintainers prefer a different encoding (for example dropping the low-bit flag
  entirely), this patch is superseded rather than amended.
* When upstream accepts it: delete the patch here, move the pin forward, and record both
  in `third-party/CMSIS-FreeRTOS.provenance.md`.
