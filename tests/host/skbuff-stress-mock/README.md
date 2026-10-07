# skbuff-stress - the skbuff pool contract harness (host)

`skbuff-stress` exercises the seam behind `components/linuxkpi/skbuff/skbuff_pool.h`
off board: a harness double of the pool body plus a scenario matrix, an invariant
judge and a seeded random layer. It needs nothing but libc and the production
headers in this repository.

The point of the harness is the contract, not the double. Every expectation is
read off the two production headers (`skbuff_pool.h` and `linuxkpi_core.h`): the
offset-style geometry (`tail`/`end` are offsets from `head`, `data` points
inside `[head, head + end]`), the `end == requested size` rule with the
`NET_SKB_PAD` reserve on `dev_alloc_skb`, the generation protocol on `_gen`
(0 = never handed out, positive = current owner, `LKPI_SKB_FREED_MAGIC` =
released), the Linux geometry semantics (`put` appends, `push` prepends, `pull`
consumes from the head, `trim` shortens the tail, `reserve` moves data forward)
and the named defect counters. `skb_pull` and `skb_trim` are the production
inline functions from the header; `lkpi_skb_put`, `lkpi_skb_push`,
`lkpi_skb_reserve` and the queue surgery are entry points. Nothing is derived
from the double's own code, so a test that re-reads the double cannot pass by
accident.

## Build and run

```
make -C tests/host/skbuff-stress-mock          # builds build/skbuff-stress
tests/host/skbuff-stress-mock/build/skbuff-stress
```

The build uses `-Wall -Wextra -Werror` and AddressSanitizer + UndefinedBehaviorSanitizer
(`-fsanitize=address,undefined -fno-omit-frame-pointer`). **A sanitizer report is a
failure of the run even when every `SKBST:` line looks clean** - the process exit
code is not the whole verdict; read the stderr output too (for example with
`ASAN_OPTIONS=detect_leaks=1` or the defaults, a leak or a UBSan trap prints after
the result lines and must be treated as a red run).

## Modes

| invocation | what it runs |
|---|---|
| `skbuff-stress` | the scenario matrix only: one line per cell plus `SKBST: matrix cells=19 pass=<n> fail=<n>` |
| `skbuff-stress --fuzz` | the matrix, then seeds 1, 2, 3 with 200000 iterations each |
| `skbuff-stress --fuzz --fuzz-iters M` | the same with M iterations per seed |
| `skbuff-stress --fuzz-seed N --fuzz-iters M` | a single explicit seed |
| `skbuff-stress --profile mcu --fuzz` | the constrained geometry: 8 header slots, 8 data slots of 256 bytes, heap fallback off, so oversize requests fail and are counted instead of falling back |
| `skbuff-stress --replay <seed> <n>` | replays the first n operations of that seed and stops where a full run would stop |
| `skbuff-stress --list-cells` | the nineteen cell ids |
| `skbuff-stress --selftest-must-red` | the harness self-proof: a misuse stream whose expectation is wrong by construction must go red, and its own replay of the same prefix must land on the same operation index. The process exits non-zero by design - a green run of this mode is a suite failure (exit 3 if the deliberate failure never showed, 4 if the replay did not reproduce it) |
| `skbuff-stress --verbose` | also prints the pool's own refusal diagnostics; off by default because a stress run produces one line per refused operation |

All results go to stdout with the `SKBST: ` prefix. The process exits 0 only when
everything it ran held. A first violation prints

```
SKBST: first-witness op=<index> kind=<family> detail=<...>
```

followed by the last few operations of the stream, and the run stops there.

## The matrix (19 cells)

* **A1 lifecycle pairing** - alloc/free pairs, capacity and recycle under a fixed
  geometry, the realloc-after-free path, a shuffled drain. Each cell ends with
  `n_alloc - n_free == live slots`.
* **A2 geometry boundaries** - put/push/pull/trim/reserve at the exact boundary,
  one past it and far past it, the reserve/headroom interaction, zero-length
  operations, and the far-out-of-range refusals. A refused operation must return
  NULL and change nothing.
* **A3 queue/sentinel** - tail enqueue, `skb_peek` on an empty queue returning
  NULL (the documented strengthened contract), FIFO dequeue order, and releasing
  a queued skb.
* **A4 named defects** - the double free (named and skipped, never reaching the
  allocator), the stray release of a foreign pointer, the use-after-free write
  attempt (named, payload untouched), and a deliberately corrupted free slot
  caught by the fence scan.

## The seeded layer

The random layer is a deterministic function of `(profile, seed, iterations)`:
xorshift64* drives an ordered stream that mixes valid operations with the named
misuses (double free, stray free, use-after-free write, oversize allocation,
corrupt-then-scan). It carries a shadow model of every live skb and of the
counters, so the double's observable state is compared against a prediction
before and after every operation, plus the named-counter ledger the judge checks
against the operations that were supposed to hit each counter. A 4096-entry
operation ring records what happened for the witness report.

Identical seeds must produce identical digests on the same profile; `--replay`
reproduces a prefix exactly, which is how a failing stream is shrunk to a
minimal repro.

## Layout

| file | role |
|---|---|
| `skb_contract.h/.c` | the harness double of the pool body and the include boundary for the production headers |
| `cases.c` | the nineteen-cell scenario matrix |
| `judge.h/.c` | the four invariant families and the first-witness report |
| `fuzz.c` | the seeded random layer, the model, the ring and the replay driver |
| `main.c` | modes and argument parsing |
