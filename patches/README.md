# patches/ - the only channel for changes to a vendored pin

`third-party/` trees are pins: they are never edited in place. When a real defect in a
pinned library has to be fixed for this repository to work, the fix lives here as an
upstreamable patch and is replayed by the build.

- `patches/<repo>/README.md` records upstream, base (= the pin), the patch list and the
  submission status of each patch.
- `tools/patches/replay.sh <repo>` materialises the tree to compile from: with no patch
  files it prints the vendored tree itself; with patches it copies the tree under
  `tools/mock/build/patched/<repo>/`, applies the patches in name order and prints that
  path. A patch that does not apply is a build error, and the replay refuses to run at all
  if the vendored tree has local modifications - a modified pin would otherwise go
  unnoticed.
- Consumers pass the printed path as their source root (`tests/host/freertos-cmsis2-smoke`
  for CMSIS-FreeRTOS). Nothing else may read `third-party/` for a patched repo.

Patch form and lifecycle: see `RELEASE_STANDARDS.md` section 5.
