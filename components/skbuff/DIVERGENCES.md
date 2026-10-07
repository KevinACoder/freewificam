# components/skbuff - divergences

Deviations of this vendored unit from the upstream pin. Empty at import time.

| # | File | Change | Reason | Round |
|---|---|---|---|---|
| - | - | none | - | - |

Import state (2026-10-07): all four files are byte-identical to
`faa8a42a03804f4906873de6bcab0037407dd39a` (hashes in `PROVENANCE.md`); no patch is
applied and nothing in this unit is compiled yet.

Rules:

* The expected first entries are the consumer-round adaptations, for example the
  host build environment for `freertos_skbuff.c` (the driver umbrella and OS service
  substitutes) or the API-face convergence with the linuxkpi contract header
  (`components/linuxkpi/skbuff/skbuff_pool.h`). Each entry names the file, the
  change, the reason and the round.
* A change that the upstream project could accept should first be offered upstream
  (TizenRT / the Realtek OS-dep layer) as an upstreamable patch; only the local
  form of the adaptation lives in this ledger.
