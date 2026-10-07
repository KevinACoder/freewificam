# lkpi-usb-stress-mock

Host stress harness for the linuxkpi USB urb engine. One binary, `linuxkpi-usb-urb-stress`:
the engine (`components/linuxkpi/usb`) runs against the synthetic controller and
scripted device model of the sibling suite (`tests/host/linuxkpi-usb-test`),
behind a harness-side wrapper that owns the injection knobs. AddressSanitizer and
UndefinedBehaviorSanitizer are on and a sanitizer report is a failure.

## Build and run

    make -C tests/host/linuxkpi-usb-urb-stress -j4
    ./tests/host/linuxkpi-linuxkpi-usb-urb-stress/build/linuxkpi-usb-urb-stress            # 28 cells
    ./tests/host/linuxkpi-linuxkpi-usb-urb-stress/build/linuxkpi-usb-urb-stress --fuzz     # + 3 seeds
    ./tests/host/linuxkpi-linuxkpi-usb-urb-stress/build/linuxkpi-usb-urb-stress --replay 2 120000
    ./tests/host/linuxkpi-linuxkpi-usb-urb-stress/build/linuxkpi-usb-urb-stress --fault double-complete
    ./tests/host/linuxkpi-linuxkpi-usb-urb-stress/build/linuxkpi-usb-urb-stress --list-cells

The default run exits 0 only when all 28 cells held and ends with

    URBSTRESS: matrix cells=28 pass=28 fail=0

plus two evidence lines the wrapper greps for: `URBSTRESS: pairing ...` (the
exactly-once rule ran over every judged urb) and `URBSTRESS: balance ...` (the
allocation counters pair). `--fuzz` runs the matrix and then the seeded stream
for seeds 1, 2, 3 (200000 operations each by default); same seed and iteration
count produce the same digest across runs. `--replay <seed> <n>` re-runs the
first `n` operations of that seed and stops at the same first-witness index as
the full run (or completes with the same digest).

## What is tested

**The 28-cell matrix** (`matrix.c`), each cell printing one
`URBSTRESS: B<group>.<cell> PASS|FAIL <details>` line:

| Group | Cells |
|---|---|
| B1 submit validation (5) | complete==NULL refused -EINVAL; double submit -EBUSY; dead-urb submit -ENODEV named; NULL device -ENODEV; missing setup packet -EINVAL |
| B2 completion semantics (4) | exactly-once over a batch; the lawful status set; the callback is not on the submitter's thread; the completion has already unanchored the urb |
| B3 kill/unlink interleave (6) | queued kill -ENOENT exactly once; unlink == kill; double kill idempotent; -EPERM during the kill barrier; kill-after-free named; post-completion kill harmless |
| B4 anchor family (4) | batch kill delivers each exactly once; a killed anchor's member behaviour (measured); natural completion unanchors; unanchor of an unanchored urb |
| B5 injection combinations (5) | stall -> clear_halt -> success; never-answering control transfer -ETIMEDOUT; disconnect with N in flight + M queued; the allocation seam's alloc-fail; scripted error codes with their Linux errno |
| B6 timing/reentrancy (4) | out-of-order completion keeps pairing; kill inside a callback; free inside a callback; resubmit inside a callback |

The **judge** (`judge.c`) applies six named rules - exactly-once, error-code,
kill-sync, uaf-named, alloc-balance, no-deadlock - and on a violation prints

    URBSTRESS: first-witness rule=<name> cell=<id> urb=<ptr> want=<v> got=<v> op=<index>
    URBSTRESS: failset n=<k> urb=...
    URBSTRESS: repro <command>

then exits 1. `--fault double-complete` is the must-go-red self-proof: the
wrapper gives one transfer a second giveback at the seam, and the exactly-once
rule must name exactly that urb (`failset n=1`).

The **seeded stream** (`fuzz.c`) drives 16-urb rows through alloc /
alloc_coherent / fill / submit / kill / unlink / anchor-family / control_msg /
free, plus misuse operators (free-freed, submit-freed, kill-freed,
double-kill, submit-live) at a declared 1-in-8 probability. Blocks of 4096
operations are quiesced, judged and released, so the allocation counters pair at
every block boundary.

**Knobs** (`engine_env.c`, all test-side parameters over the round-A controller
facilities): completion timing (immediate / delayed K harness steps / never),
completion order (FIFO / reverse / permutation), a programmed per-transfer
error, double completion, and a never-answering control lane. The controller's
worker is held for the whole run, so every transfer completes when the harness's
deterministic step counter says so - no wall-clock sleeps and no threads besides
the runtime's.

## Reentrancy cells: the measured facts

The engine's behaviour under the three reentrant callbacks is itself the fact
these cells record (each asserts only the invariants - no crash, exactly-once,
balanced accounting - and prints the measurement):

* **kill inside a completion callback**: a kill of the completing urb from its
  own callback is a bounded no-op (the engine's kill-wait latch fires, no second
  completion); a kill of a *different* queued urb drains that urb inline on the
  calling thread.
* **free inside a completion callback**: safe; the callback's release only drops
  one reference - the engine's drain releases the block after the callback.
* **resubmit inside a completion callback**: legal and lands; the engine accepts
  a urb in the CALLBACK state as the next queue entry and completes it once more
  after the callback returns.
