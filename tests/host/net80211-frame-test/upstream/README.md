# The library's own host tests, re-anchored

Two test programs come from the imported library's tests/ directory and are kept
byte-identical to their pin:

| file | upstream path | sha256 |
|---|---|---|
| `mbuf_trim.c` | `tests/mbuf_trim.c` | `291a6eec6dd7f35e1748128fec540d230dff4a747dcb37a0316a5a6635c4bfa6` |
| `ccm.c` | `tests/ccm.c` | `4d65c6fa37ac4cfa8686644c9bfacb0f34f3a152c33d3180fbabb6bd1b6ab381` |

pin: `9770b1ad7093f43a0f2ab2dc8d980067fb0f62a1` (tag `netbsd-shape-freeze`).

What changed when they were re-anchored onto this port (the test sources
themselves are untouched):

* the retired `tests/include/` shims (`crypto_host.h`, `hal/ipl.h`,
  `mem/sysmalloc.h`) and the retired `port/net/embox/bsd_mbuf.c` are replaced by
  this repository's port layer: the two programs link `ports/net80211/net/mbuf.c`
  and `ports/net80211/osal/cmsis2/osal_memory.c` (plus the AES units and
  `port/aes_impl_compat.c` for the CCM program) and are compiled with the port's
  include discipline and its `prelude.h`;
* the one symbol the retired shims provided, `panic()`, comes from
  `host_stubs.c` next to this file;
* the port's osal logging unit is not linked, so the `kprintf` stub the test
  files carry themselves is the one that is used.

The CCMP vector in `ccm.c` is RFC 3610 packet vector 1 (public test material),
which is the independent truth source of the AES-CCM leg; nothing in these two
programs reads the stack under test.
