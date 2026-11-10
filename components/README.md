# components

Code written in this repository.

| Component | Contents | State |
|---|---|---|
| `linuxkpi/` | the kernel API surface the wireless stack needs | to be built |

The kernel API layer carries no protocol logic: it supplies what the stack
expects to find in its host kernel (allocation, workqueues, timing, per-CPU
access, the locking and reference-count helpers), so that the stack can be
compiled and run unchanged.
