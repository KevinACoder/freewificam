# CMSIS-FreeRTOS/patches - record only

The adapter tree is imported **already patched**. This directory records the
patch set and the digest of the source tree the build compiles; it is not a
build-time replay directory.

| Item | Value |
|---|---|
| Upstream | https://github.com/ARM-software/CMSIS-FreeRTOS |
| Pin | tag `v11.3.0`, commit `4dd0035148cdc106f892877c27cba77d1de717d2` |
| Patch list | `0001-cmsis_os2-do-not-launder-handles-through-32-bit-integers.patch` - the adapter round-trips mutex and timer handles through `uint32_t`, which truncates them on any 64-bit target (mutex acquire faults, the timer callback dispatch dereferences a truncated pointer); the patch uses `uintptr_t` for those round trips, no API/ABI change |
| Applied | the patch is applied in the tree; this directory records it |
| Manifest | `MANIFEST.sha256` - the digest of the patched tree, excluding this directory |
| Verify | `cd third-party/CMSIS-FreeRTOS && sha256sum -c patches/MANIFEST.sha256` |
| Submission status | **not submitted** (the patch is written in `git format-patch` form and the PR is ready to open against the pin above) |

Rules (project patch policy):

* The imported tree carries its applied patches; the build compiles the tree
  directly, with no replay copy. `patches/` is the record of what was applied.
* One concern per patch, English, `Signed-off-by`, no local knowledge.
* The defect is measurable on the host tier before it is fixed: a mutex handle
  placed above 4 GiB faults inside the adapter. The board image's heap window
  sits below 4 GiB, so the board round cannot show it - the host conformance
  tier is the red control that keeps the patch honest.
* When upstream accepts it: delete the patch here, move the pin forward,
  regenerate `patches/MANIFEST.sha256`, and record both in
  `third-party/CMSIS-FreeRTOS.provenance.md`.
