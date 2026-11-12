# STATUS

- Updated: 2026-11-12 (A4 TX stall: root cause found and fixed; down/UDP pass, up open)
- Stage: **the imported wireless stack registers, the AC 7260 attaches, joins
  the bench AP and moves data through lwIP**.  C0/C1/C2/C3/C4 are 2/2 cold
  boots; C5-open and C6-open (ICMP over wl0) are 2/2 in
  `runs/2026-11-11-iwl7260-c5-open`.  The C6T throughput round and the
  stability round are this round's work; their numbers land in
  `runs/2026-11-11-iwl7260-c6-throughput/`, not in this stage line.

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
| C4 scan | real `NL80211_CMD_TRIGGER_SCAN` bytes through nl80211's handler -> iwlwifi hw scan -> cfg80211 BSS table carries the bench AP | **reached 2/2**: `runs/2026-11-11-iwl7260-c4` (SSID/BSSID/ch5) |
| C5/C6-open | association (open AP) / ICMP over wl0 | **reached 2/2**: `runs/2026-11-11-iwl7260-c5-open` |
| C6T throughput | TCP/UDP phases over wl0 against the GT arm; the 80% / 1% gates | **reached**: GT 2/2 + baseline 2/2 + pool128 2/2; T1 passes on pool128 (TCP down 142.6%, up 93.3%, UDP 0% loss, T2 0.002%/2.37%) - `runs/2026-11-11-iwl7260-c6-throughput/REPORT.json` |
| C6S stability | 30 min TCP downlink + 10 reconnects; dynamic ARP | **reached 1/1 + partial**: `stab1` 30 min (1,777,484 ms, 13.95 GB, peer 60.4 Mbit/s), 10/10 reconnects (713-903 ms, 4/4 ping), 60 health lines, no reset, judge 4/4; dynamic ARP `PERMANENT -> REACHABLE` with `dev_set_rx_mode`/allmulti. `stab2` was aborted at 7.5 min for session close (partial, not a verdict); PS buffering remains open - `runs/2026-11-11-iwl7260-stab/` |
| A0/A1 AP | the AC 7260 becomes an AP: `SET_INTERFACE rc=0 type=AP`, `START_AP rc=0`, monitor sees its beacons with the GT IE order and its probe responses **2/2**; the Pi's scan lists the BSS - `runs/2026-11-12-iwl7260-ap/`, ADR `2026-11-11-ap-mode.md` |
| A2/A3 AP (hostapd) | **reached, repeated**: the AP MLME is the ported NXP hostapd (`third-party/wpa_supplicant-rtos`), not the hand-written one. `hostap start rc=0 ready=1`, the Pi associates, hostapd's station table and `NL80211_CMD_SET_STATION(AUTHORIZED)` land, both ping payloads 20/20 (RTT 1-2 ms) in 6 of 9 rounds of 2026-11-12 - `runs/2026-11-12-iwl7260-ap-port/`, ADR `2026-11-12-ap-mlme-port.md` |
| A5 AP-WPA2 (G1) | **reached 2/2**: the 7260 is a WPA2-PSK/CCMP AP and the Pi's MT7921AU completes the 4-way. PTK+GTK `set_key rc=0`, 4/5 EAPOL frames on air, `wpa_state=COMPLETED`, 56/1400 B ICMP 20/20 **both directions**, dynamic ARP `REACHABLE`, `PASS APW`, judge PASS. The round also added a real `ccm(aes)` provider (`runs/2026-11-12-iwl7260-ap-wpa2/`, ADR `2026-11-12-ap-wpa2.md`) |
| A4 AP | **TX stall fixed**: the stop was the port's single-shot NAPI poll, not the device. A poll returned the whole budget (`napi_ret=64`), iwlwifi's poll therefore did not re-enable `CSR_INT_MASK`, and the port cleared SCHED without a second poll; the device still had pending RX (`int=0x92000000 mask=0`). Root cause 2/2 (`table1/table2`), fix validated (`fix1` verdict=none; standard AP arm `a4-1b`/`a4-2` both `PASS APT`). A4-P2: TCP down 73.2 Mbit/s (99.9% of GT 73.3), UDP 40M loss <=0.12%, app-vs-app <=0.11% pass; **TCP up 43.9 Mbit/s (57.3% of GT 76.6) fails the 80% gate** - the board RX path, next round. See `runs/2026-11-12-iwl7260-ap-a4/RUN.md`, ADR `docs/decisions/2026-11-12-ap-a4-napi-budget.md` |
| K1/K2 context | the host semantic bench and the three execution-context experiments | **reached on the host model**: 33/33 cases, `--break` red; ADR `2026-11-10-execution-context-model.md`; not board evidence |

## Receipts

The C0/C1 numbers are the image lines in the build logs; the per-profile
sha256/size are printed by the build itself (`build/build-info-freertos.txt`).
Board evidence, when it lands, goes under the project's `runs/` ledger, not
here: this file only says which stage has been reached.

## Preconditions

See `README.md`: the uniprocessor assumption is a precondition of the design,
not an implementation detail.
