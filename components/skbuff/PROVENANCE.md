# components/skbuff - provenance

| Item | Value |
|---|---|
| Upstream | TizenRT (Samsung/TizenRT lineage); the imported checkout is the `swasthik22` fork, https://github.com/swasthik22/TizenRT.git |
| Pin | commit `faa8a42a03804f4906873de6bcab0037407dd39a` (2023-03-20); the checkout carries no tags |
| License | Apache-2.0 (repository `LICENSE`, copied here as `LICENSE.Apache-2.0`). `skbuff.h` and `freertos/wrapper.h` carry the Realtek Semiconductor Corp. Apache-2.0 notice; `freertos/freertos_skbuff.c` and `freertos/freertos_skbuff.h` carry no per-file header and are covered by the repository license |
| Vendoring | vendored verbatim, byte-identical to the pin (4 files plus the license copy); not a git submodule (user ruling 2026-10-07: a component, not third-party) |
| Fetched | 2026-10-07 |
| Local differences | none (zero patch); sha256: `skbuff.h` = `408363f6ac2dd3a58c428439710056d95ff69ab6612f2d1a215b392b77a14f12`, `freertos/freertos_skbuff.c` = `c67e201dfe737eb772a1f393f66e7c50a8bdc2cc5e9ab3e9840dbf33c43f6669`, `freertos/freertos_skbuff.h` = `3d5c5cf4fef568a8cd218d6e0ad60f5d418711dda5c8a1b8d9f5f51a3a82b1a7`, `freertos/wrapper.h` = `0797f7c280113af84b2d75aad42a54ad4f689cf1989b492918ea18f6f046de8e` |
| Compiled set | none in this round (see "Compiled set" below); the two self-contained headers are syntax-checked by the import build contract |
| Consumers | planned: the memory subsystem pool ruling (M1-1) and the host mock suites (M0-6 / M0-7) through their seams |
| Reason | user ruling 2026-10-07: the previous line's skbuff pool is a function-level port of this source with twelve registered deviations, and that ported implementation is not the reference; the pool's source of truth is this TizenRT/Realtek implementation, imported verbatim so later rounds adapt it under a ledger instead of re-inventing it |

## Layout

Upstream paths and names are preserved, including the `freertos/` subdirectory, so
the sources' own includes resolve unchanged with `-Icomponents/skbuff`:

| This directory | Upstream path (relative to `os/board/rtl8720e/src/component/wifi/driver/src/`) |
|---|---|
| `skbuff.h` | `osdep/skbuff.h` |
| `freertos/freertos_skbuff.c` | `osdep/freertos/freertos_skbuff.c` |
| `freertos/freertos_skbuff.h` | `osdep/freertos/freertos_skbuff.h` |
| `freertos/wrapper.h` | `osdep/freertos/wrapper.h` |

The source's own includes are `<skbuff.h>` and `<freertos/freertos_skbuff.h>`;
`wrapper.h` also includes `wireless.h` and `osdep_service.h`, which are not part of
this unit (see "Compiled set").

What the four files are:

* `skbuff.h` - the `struct sk_buff` / `struct sk_buff_head` shapes and the `skb_*`
  API prototypes of the Realtek OS-dep layer.
* `freertos/freertos_skbuff.c` - the pool implementation: a dual static pool
  (header slots plus data-buffer slots), FIFO free lists, a reference count on data
  slots, and an oversize fallback to the general allocator.
* `freertos/freertos_skbuff.h` - the per-SoC pool-size matrix.
* `freertos/wrapper.h` - the Linux-like list and critical-section wrapper the pool
  and the driver share (`save_and_cli` / `restore_flags` contract, `SKB_DATA_ALIGN`,
  `MAX_SKB_BUF_SIZE`).

Not vendored: the rest of the board driver tree (the `drv_types.h` umbrella,
`osdep_service.h`, `hci_intfs.h`, the OS service layer) and the driver-tree copy of
the same header (`os/drivers/wireless/realtek/rtk/{include,src/osdep}/skbuff.h`,
identical 82-line files); the pin remains the retrieval point.

## Compiled set

None in this round. `freertos_skbuff.c` needs the Realtek driver umbrella
(`drv_types.h` chaining to `osdep_service.h` and `hci_intfs.h`), the OS service
layer (`save_and_cli` / `restore_flags`, `rtw_zmalloc` / `rtw_free`) and the osdep
atomics/list helpers - none of which are in this repository, so host compilation
would mean standing up a substitute driver environment. That adaptation is
consumer-round work and is recorded in `DIVERGENCES.md`.

What is checked here: `skbuff.h` and `freertos/freertos_skbuff.h` are
self-contained (incomplete-`struct list_head` pointers and macros only) and are
syntax-checked by the `contract-check` target of `tests/host/linuxkpi-usb-test/`;
`freertos/wrapper.h` is not compiled standalone because it includes `wireless.h`
and `osdep_service.h`, which are outside this unit.

## Rules for changes

* The four files stay byte-identical. Adaptations happen in the consumer round that
  needs them and every one of them is recorded in `DIVERGENCES.md` (what changed,
  why, which round) before it lands.
* Keep the upstream names and the `freertos/` relative layout; the include shape is
  part of the imported contract.
