# skbuff-stress

The Gate 1 suite over the skbuff seam: the pool contract face is driven with a
scenario matrix, seeded operation streams and a constrained profile, and every
invariant is judged after every operation.

## Cases

| Case | What it proves |
|---|---|
| `matrix` | The nineteen-cell scenario matrix passes in full |
| `matrix-A1..A4` | Each family passes on its own (lifecycle pairing, geometry boundaries, queue sentinel, named defects) |
| `seeds` | The three seeded streams (200000 operations each) run green and are deterministic: two runs produce identical output |
| `mcu-profile` | The constrained profile (8 header slots / 8 data slots / 256-byte buffers, no heap fallback) runs the matrix and a stream green |
| `must-red` | The reverse control: a deliberately wrong expectation is detected and reported as a first-witness, not swallowed |
| `replay` | The minimal-repro path reproduces that control's failure point |

## Run shape

The wrapper `check.sh` runs the harness in its shapes, compares two seeded runs
for determinism, and turns each into a case line.  The harness itself lives in
`tests/host/skbuff-stress-mock/` with its own README (`SKBST: ` anchor lines).

The seed ledger `seeds.tsv` records the digests a passing run produced; a digest
change is a round-level event, not a silent update.

## Scope

The object under test is the harness double of the skbuff seam, whose contract
face is the in-repo headers (`components/linuxkpi/skbuff/skbuff_pool.h` and
`components/linuxkpi/linuxkpi_core.h`).  The pool bodies and their hardening
belong to the memory subsystem milestone; when the real pool lands through the
same seam the matrix and the streams run unchanged.
