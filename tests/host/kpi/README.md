# tests/host/kpi — the host semantic bench

One x86 binary that links the component's real kernel-API bodies, the upstream
FreeRTOS POSIX port and the CMSIS-RTOS2 adapter, and pins the execution-context
semantics the board cannot show (`docs/HANDOFF-S0-HOST.md` §3–§4).

```sh
./make.rtos host-test        # build, run green, then run --break (must be red)
./tests/host/kpi/build/kpi-test --break   # the negative control alone
```

## What it is, and is not

It is a **model** of the target's context: same kpi sources, same CMSIS-RTOS2
facade, no hardware.  It is not a board test and not a kernel simulator.

| Cannot prove (deliberately) | Why |
|---|---|
| real interrupt-masking latency | the host's `osCriticalEnter` is a process-wide recursive pthread mutex (same exclusion, no latency claim) |
| a real ISR context | host has no interrupts; high-priority tasks approximate one, and the case says so |
| single-core behaviour | the host is SMP; interleavings are made explicit with semaphores/delays, they are not the target's preemption |
| GIC/cache/DMA, board timing | not modelled |

Evidence label: `model`, not `baseline`/`perturbed`.

## Cases

| Case | What it pins | Anchor on failure |
|---|---|---|
| `work_order` | FIFO dispatch of three queued works | order 0,1,2 |
| `work_coalesce` | the same work queued twice runs once | `runs==1` |
| `work_flush` | `lkpi_work_flush` returns after the body | `ran==1` |
| `work_cancel` | `cancel_work_sync` drops a queued body and waits for a running one | `victim_ran==0`, return 1 |
| `timer_due` | one-shot fires, not early, not twice | elapsed ≥ 40 ms, second acquire times out |
| `timer_rearm` | re-arm moves the expiry | fires at the new expiry |
| `timer_cancel` | `del_timer_sync` before expiry prevents the callback | no fire in 200 ms |
| `sync_completion` | timeout returns 0, `complete()` wakes | left==0 then left!=0 |
| `sync_mutex` | not recursive: trylock from another context fails while held, succeeds after release | `try_ret==0` |
| `sync_semaphore` | count arithmetic and the trylock refusal | `down_trylock` 1 then 0 |
| `skb_bounds` | a put past the tail is refused and leaves `len` alone | NULL, len unchanged |
| `skb_pool` | exhaustion returns NULL and freed slots are reusable | NULL then non-NULL |
| `idr_rb` | **skipped**: no idr/rbtree implementation yet (gap in `docs/LINUXKPI-GAP.md`) | skip line |
| `selftest` | the negative control: passes normally, fails under `--break` | exits non-zero under `--break` |

## Reading the anchors

```
kpi-test: <case> pass|fail|skip
kpi-test:   assertion failed at <file>:<line>: <expr> (<detail>)
kpi-test: result pass <n>/<m> skipped=<k> failures=<f>
kpi-test: selftest red ok
```

Exit code 0 only when `failures==0`; `make run` additionally requires the
`--break` run to exit non-zero.
