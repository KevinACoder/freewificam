# components/linuxkpi - divergences

Deviations of this import from its source blobs. Empty at import time.

| # | File | Change | Reason | Round |
|---|---|---|---|---|
| - | - | none | - | - |

Import state (2026-10-07): the nine imported files are byte-identical to their
source blobs on the previous line's `feat/lkpi-stabilize-r1` branch (blob IDs in
`PROVENANCE.md`); no patch is applied.

Known convergence work that will produce the first entries (recorded here so it is
not rediscovered later):

* `skbuff/skbuff_pool.h` declares the Linux-shaped skbuff contract face that
  `linuxkpi_core.h` includes, while the pool's source of truth is the verbatim
  TizenRT unit in `components/skbuff/`. The two faces converge in the memory
  subsystem / consumer rounds; every step of that convergence is a ledger entry
  here.
* The USB engine group (`usb/*`) is vendored but not compiled; adopting it in the
  linuxkpi-usb mock round (dropping `tx_api.h`, placing the CMSIS extension header,
  providing a host timer header, deciding the diagnostics surface) produces one
  entry per change.

Rules: prefer an upstreamable patch over an in-tree edit; keep every edit minimal
and register it here with file, reason and round.
