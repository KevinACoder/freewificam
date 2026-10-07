# usb-urb-stress

The Gate 1 suite over the linuxkpi USB urb engine under combination and
injection stress: the engine runs against the synthetic controller with the
injection knobs of this round, and six judgement rules watch every operation.

## Cases

| Case | What it proves |
|---|---|
| `matrix` | The 28-cell scenario matrix passes in full |
| `matrix-B1..B6` | Each family passes on its own (submit validation, completion semantics, kill/unlink interleave, anchor family, injection combinations, timing/reentrancy) |
| `pairing-balance` | Exactly-once completion and allocation balance hold across the matrix |
| `seeds` | The three seeded streams run green and are deterministic: two runs produce identical output |
| `double-complete` | The reverse control: a forced second completion is caught by the exactly-once rule and the failure set names that one urb |
| `replay` | The minimal-repro path reproduces that control's failure point |

## Injection knobs (test-side parameters)

Completion timing (immediate / delayed K / never), completion order (FIFO /
reverse / permutation), programmed error states, and disconnect with transfers
in flight and queued.  All deterministic: the harness advances a virtual step
counter and drives the controller's queue itself; the engine is untouched.

## Run shape

The wrapper `check.sh` runs the harness in its shapes, compares two seeded runs
for determinism, and turns each into a case line.  The harness itself lives in
`tests/host/lkpi-usb-stress-mock/` with its own README (`URBSTRESS: ` lines).

## Scope

The object under test is the linuxkpi USB engine as adopted by the usb-device-model
round, driven through the same backend seam.  This suite extends the input
dimension; it does not re-judge the identification-face suite's criteria.  When
the real urb pool lands through the seam the matrix and the streams run unchanged.
