# STATUS

- Updated: 2026-11-10 (S1b/S1c + C2/C3 round)
- Stage: **the imported wireless stack registers and the AC 7260 attaches on
  the board**.  C0/C1 for both, C2 (wiphy registration) and C3 (iwlwifi probe ->
  firmware ALIVE -> register -> start) are 2/2 cold boots; C4-C6 are not
  started.

## Stages

Each stage is stated as what it is. A stage is announced only when the one
below it has been reached; "compiles" and "links" are not "runs".

| Stage | Meaning | State |
|---|---|---|
| C0 compile | the profile's translation units produce object files | **reached** for `rk3568-tick`, `rk3568-net`, `rk3568-pcie-nvme`, `rk3568-emmc`, `rk3568-usb-msc` |
| C1 link | no undefined symbols; the linker gates pass | **reached** for the same five (gates: `_start`, vector table, backend symbols, 0 undefined) |
| B board | two clean cold boots per profile, judged by `tools/rtos/judge.py` | **reached**: five acceptance profiles and two component profiles, 2/2 each (2026-11-10) |
| K component | `components/liblinux80211/` builds as its own archive and proves its bus seams on the board | **reached**: `liblinux80211.a` (4.08 MB) + `rk3568-kpi-mock` / `rk3568-kpi-live` 2/2 |
| S0 stack | `net/mac80211` + `net/wireless` compiled one `-c` at a time against the component's Linux face | **reached**: 62/62 TUs (`runs/S0/run-50`, `docs/LINUXKPI-GAP.md` v2) |
| S1b driver | the vendored iwlwifi (294 files) compiled against the face | **reached**: 72/72 TUs (`runs/S1/driver/run-3`) |
| S1c link | stack + driver partial link, classified undefined list, one winner per API symbol | **reached**: 314 external refs, 186 provided, 112 real gaps, 0 duplicate winners (`runs/S1/link/run-1`, `runs/S1/RUN.md`) |
| C2 register | `ieee80211_alloc_hw` + `register_hw` + default `wlan0` on the board | **reached 2/2**: `runs/2026-11-10-mac80211-c2` (clean image, cold boots) |
| C3 attach | iwlwifi claims `8086:08b1`, firmware `17.bfb58538.0` ALIVE, `wlan0`, `dev_open` -> start rc=0 | **reached 2/2**: `runs/2026-11-10-iwl7260-c3` |
| C4-C6 | scan / association / data plane | **not started** |
| K1/K2 context | the host semantic bench and the three execution-context experiments | **reached on the host model**: 23/24 cases (1 explicit skip), `--break` red; ADR `2026-11-10-execution-context-model.md`; not board evidence |

## Receipts

The C0/C1 numbers are the image lines in the build logs; the per-profile
sha256/size are printed by the build itself (`build/build-info-freertos.txt`).
Board evidence, when it lands, goes under the project's `runs/` ledger, not
here: this file only says which stage has been reached.

## Preconditions

See `README.md`: the uniprocessor assumption is a precondition of the design,
not an implementation detail.
