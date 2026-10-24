# patches/CMSIS-FreeRTOS - local patches replayed onto the vendored pin

| Item | Value |
|---|---|
| Upstream | https://github.com/ARM-software/CMSIS-FreeRTOS |
| Base (= pin) | tag `v11.3.0`, commit `4dd0035148cdc106f892877c27cba77d1de717d2` |
| Patch list | `0001-cmsis_os2-do-not-launder-handles-through-32-bit-integers.patch` - the adapter round-trips mutex and timer handles through `uint32_t`, which truncates them on any 64-bit target (mutex acquire faults, the timer callback dispatch dereferences a truncated pointer); the patch uses `uintptr_t` for those round trips, no API/ABI change |
| Submission status | **not submitted** (the patch is written in `git format-patch` form and the PR is ready to open against the pin above) |

Rules (project patch policy):

* The vendored snapshot itself stays byte-identical. A patch is applied only through
  this channel: `tools/patches/replay.sh CMSIS-FreeRTOS` materialises
  `tools/mock/build/patched/CMSIS-FreeRTOS/` (copy + `patch -p1 --fuzz=0`) at build
  time, and every consumer compiles from the printed path - never from the vendored
  tree when a patch exists, and never by editing the tree.
* One concern per patch, English, `Signed-off-by`, no local knowledge.
* The defect is measurable on the host tier before it is fixed: a mutex handle placed
  above 4 GiB faults inside the adapter. The board image's heap window sits below
  4 GiB, so the board round cannot show it - the host conformance tier is the red
  control that keeps the patch honest.
* When upstream accepts it: delete the patch here, move the pin forward, regenerate
  `patches/CMSIS-FreeRTOS/MANIFEST.sha256`, and record both in
  `third-party/CMSIS-FreeRTOS.provenance.md`.