# STATUS

- Updated: 2026-11-10
- Stage: **the RTOS line builds its acceptance images**; no board boot is
  recorded in this repository yet.

## Stages

Each stage is stated as what it is. A stage is announced only when the one
below it has been reached; "compiles" and "links" are not "runs".

| Stage | Meaning | State |
|---|---|---|
| C0 compile | the profile's translation units produce object files | **reached** for `rk3568-tick`, `rk3568-net`, `rk3568-pcie-nvme`, `rk3568-emmc`, `rk3568-usb-msc` |
| C1 link | no undefined symbols; the linker gates pass | **reached** for the same five (gates: `_start`, vector table, backend symbols, 0 undefined) |
| B board | two clean cold boots per profile, judged by `tools/rtos/judge.py` | not yet recorded here |
| K component | `components/liblinux80211/` builds as its own archive and proves its bus seams on the board | not started |

## Receipts

The C0/C1 numbers are the image lines in the build logs; the per-profile
sha256/size are printed by the build itself (`build/build-info-freertos.txt`).
Board evidence, when it lands, goes under the project's `runs/` ledger, not
here: this file only says which stage has been reached.

## Preconditions

See `README.md`: the uniprocessor assumption is a precondition of the design,
not an implementation detail.
