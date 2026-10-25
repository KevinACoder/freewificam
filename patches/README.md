# patches/ - the only channel for changes to a vendored pin

`third-party/` trees are pins: they are never edited in place. Each import carries a
digest manifest here (one line per carried file, paths relative to the tree root):

```sh
cd third-party/<name> && sha256sum -c ../../patches/<name>/MANIFEST.sha256
```

When a real defect in a pinned library has to be fixed for this repository to work,
the fix lives here as an upstreamable patch and is replayed at build time:

- `patches/<repo>/README.md` records upstream, base (= the pin), the patch list and
  the submission status of each patch.
- `tools/patches/replay.sh <repo>` materialises the tree to compile from: with no
  patch files it prints the vendored tree itself; with patches it copies the tree
  under `tools/mock/build/patched/<repo>/`, applies the patches in name order and
  prints that path. A patch that does not apply is a build error, and the replay
  refuses to run if the vendored path carries local modifications.

| tree | pin | carry | patches |
|---|---|---|---|
| `CMSIS_6` | `v6.3.0` `45dab712` | RTOS2 + Driver + A-profile core headers | none |
| `CMSIS-FreeRTOS` | `v11.3.0` `4dd00351` | the CMSIS-RTOS2 adapter | `0001` (64-bit handle round trip) |
| `FreeRTOS-Kernel` | `V11.3.1` `3a22924e` | kernel + A55 port + POSIX port + heap_4 | none |
| `lwip` | `STABLE-2_2_1_RELEASE` `77dcd25a` | `src/`, `contrib/apps/ping/` | `0003` (delayed-ACK watermark); `0001`/`0004`/`0006` parked under `patches/lwip/unadopted/` |
| `threadx` | `44d7c95c` (`6.5.1.202602a`) | kernel + A55 port + Linux port | none |
| `iwlwifi` | `e71d9a28760f` | the driver tree | none |
| `rtl8xxxu` | `e63969708249` | the driver tree | none |

The RTOS trees are carried as a **sparse flat import**: the manifest covers the
files this repository carries, and the provenance file beside each tree names the
parts of the upstream tree that are deliberately absent. The manifests are the gate;
`make -C rtos/build digest` checks all six.

Patch form and lifecycle: one concern per patch, `git format-patch` form, English,
`Signed-off-by`, no local knowledge; written so it could be sent upstream as-is.
When upstream accepts a patch, delete it here, move the pin forward (regenerate the
manifest), and record both in the tree's provenance file.