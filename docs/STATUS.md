# STATUS

- Updated: 2026-11-10 (S0 / K1 / K2 round)
- Stage: **the RTOS line builds its acceptance images; the imported wireless
  stack has a C0 inventory, and its execution-context semantics have a host
  bench**.  No board boot is recorded for the stack yet.

## Stages

Each stage is stated as what it is. A stage is announced only when the one
below it has been reached; "compiles" and "links" are not "runs".

| Stage | Meaning | State |
|---|---|---|
| C0 compile | the profile's translation units produce object files | **reached** for `rk3568-tick`, `rk3568-net`, `rk3568-pcie-nvme`, `rk3568-emmc`, `rk3568-usb-msc` |
| C1 link | no undefined symbols; the linker gates pass | **reached** for the same five (gates: `_start`, vector table, backend symbols, 0 undefined) |
| B board | two clean cold boots per profile, judged by `tools/rtos/judge.py` | **reached**: five acceptance profiles and two component profiles, 2/2 each (2026-11-10) |
| K component | `components/liblinux80211/` builds as its own archive and proves its bus seams on the board | **reached**: `liblinux80211.a` (4.08 MB) + `rk3568-kpi-mock` / `rk3568-kpi-live` 2/2 |
| S0 stack | `net/mac80211` + `net/wireless` compiled one `-c` at a time against the component's Linux face | **inventory reached, not a pass**: 62 TUs, 3 produce objects, 59 classified (`runs/S0`, `docs/LINUXKPI-GAP.md`); the face is not complete yet |
| S1 link | the stack linked with cfg80211 + linuxkpi, no undefined symbols | **not started** |
| K1/K2 context | the host semantic bench and the three execution-context experiments | **reached on the host model**: 23/24 cases (1 explicit skip), `--break` red; ADR `2026-11-10-execution-context-model.md`; not board evidence |

## Receipts

The C0/C1 numbers are the image lines in the build logs; the per-profile
sha256/size are printed by the build itself (`build/build-info-freertos.txt`).
Board evidence, when it lands, goes under the project's `runs/` ledger, not
here: this file only says which stage has been reached.

## Preconditions

See `README.md`: the uniprocessor assumption is a precondition of the design,
not an implementation detail.
