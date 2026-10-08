# components/linuxkpi/skbuff - provenance

The pool face (`skbuff_pool.h`) is a component import (see
`../PROVENANCE.md`); the pool body (`skbuff_pool.c`) is first-party code written
against the fact source below. This record covers the body and the convergence
between the two.

| Item | Value |
|---|---|
| Fact source | `components/skbuff/` - the verbatim TizenRT/Realtek osdep skbuff pool (Apache-2.0; pin and file digests in its own `PROVENANCE.md`) |
| API face | `skbuff_pool.h` (this directory), imported from the previous line; the body implements exactly its declarations |
| License | BSD-2-Clause (body, first-party); the pool's shape and the dual-pool discipline descend from the Apache-2.0 fact source, recorded in `../../LICENSES.md` |
| Written | 2026-10-08 |
| Local differences | none to the fact source (it is read-only); every deliberate convergence of the body away from it is a numbered entry in `DIVERGENCES.md` |

## Function-level lineage

Every public symbol of the body, and where its behaviour comes from. "Source"
refers to `components/skbuff/freertos/freertos_skbuff.c`; "harness contract"
refers to the frozen semantics in
`tests/host/skbuff-allocation-free-stress/skb_contract.c` (the double the README
of that suite calls "the contract, not the double").

| Body symbol | Lineage |
|---|---|
| `lkpi_skb_pool_init`, slot/data arrays | Source `:10-47,105-137` (`skb_pool[]`, `skb_data_pool[]`, `init_skb_pool`, `init_skb_data_pool`) |
| `lkpi_skb_alloc`, `lkpi_dev_alloc_skb`, `__netdev_alloc_skb` | Source `:143-236` (`alloc_skb`, `dev_alloc_skb`); the `NET_SKB_PAD` reserve is the face's rule (`skbuff_pool.h:36-39`) |
| `lkpi_skb_free` | Source `:238-263` (`kfree_skb`: returns the data slot by reference count, then the header slot); the named refusals and the release marker are the harness contract |
| `skb_clone` | Source `:654-690` (`skb_clone`: shares the payload, increments its reference) |
| `lkpi_skb_put`, `lkpi_skb_push`, `lkpi_skb_reserve` | Source `:471-512` (`skb_put`/`skb_pull`/`skb_push`/`skb_reserve`); the boundary refusals are the harness contract |
| queue family (`lkpi_skb_queue_*`, `lkpi_skb_do_*`, `lkpi_skb_unlink`) | Linux semantics as the face states them (`linuxkpi_core.h:1800-1853`); the raw/public split is the face's contract |
| `lkpi_skb_pool_stats`, `lkpi_skb_diag_dump`, `lkpi_skb_gen_report`, `lkpi_skb_fence_scan`, `lkpi_skb_uaf_hit`, `lkpi_skb_geo_bad_count` | First-party diagnostics; the counter meanings and the fence scan's expectations are the harness contract (`skbuff_pool.h:85-122`) |
| `lkpi_skb_pool_configure` | First-party; source `:15-22,37` (`_WEAK int max_local_skb_num` - the fact source's own sizes are runtime variables, so a runtime geometry is its native shape) |
| platform hooks (`lkpi_spl_*`) | Declared by the face (`skbuff_pool.h:124-133`); target bodies reach the port facade (`os/cmsis2/cmsis2.h`), host bodies are supplied by the harness |

## How the two references meet

The source is a board osdep layer: it assumes a single core, arrays sized by the
SoC's matrix, and a caller discipline of its own. The harness contract is a
frozen set of expectations this repository keeps in its Gate 1 tier. Where they
disagree - the free-list mechanism, the geometry variables, the diagnostic
naming - the body follows the harness contract and the deviation is recorded in
`DIVERGENCES.md` with its reason; where they agree, the body is a direct
transposition of the source's shape.
