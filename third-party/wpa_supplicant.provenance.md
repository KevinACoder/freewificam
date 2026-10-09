# third-party/wpa_supplicant - provenance

| Item | Value |
|---|---|
| Upstream | https://github.com/KevinACoder/wpa_supplicant-rtos.git (branch `feat-embox-port`) - an NXP MCUXpresso SDK middleware delivery of wpa_supplicant, forked for use on RTOS carriers (`README.md`:1 identifies it as `mcuxsdk-middleware-wireless-wpa_supplicant-rtos`) |
| Pin | commit `0248409847056512eafae878a0a2260f10437b3c` (2026-09-10, "embox: drive the active net80211 port, not a fixed urtwn device") |
| License | **BSD-3-Clause** (`LICENSE` starts "The BSD 3 Clause License"); upstream hostap traces to the same license. Distribution obligations: see `LICENSES.md` |
| Vendoring | git submodule over the pin, then `sparse-checkout set --cone src wpa_supplicant` (plus the root files `LICENSE`, `README.md`, `CMakeLists.txt`, `Kconfig`, `scr.txt`, which cone mode keeps). `hostapd/`, `port/` and `freertos/` (the NXP delivery's own build tree) are outside the sparse set and never enter the build |
| Fetched | 2026-10-09 from the upstream URL above (`git ls-remote ... refs/heads/feat-embox-port` returns the pin) |
| Local differences | one patch, `patches/wpa_supplicant/0001-events-compare-roam-thresholds-in-integer-arithmetic.patch`: `wpa_supplicant/events.c` compared estimated-throughput ratios against `1.2`/`1.5`/`1.1`; the patch expresses them as exact integer ratios so the file builds on images without floating-point support. Applied at build time by `tools/patches/replay.sh`, never by editing the submodule |
| Compiled set | 45 PSK-only units under `src/` and `wpa_supplicant/` (drivers 2, common 3, rsn_supp 4, utils 5, crypto 18, wpa_supplicant/ 13); a first-party adapter layer under `ports/wpa_supplicant/` is compiled with them. `ctrl_iface*`, `config_file.c`, `p2p/` and `eap_peer/` are outside the set |
| Consumers | `ports/wpa_supplicant/` (the CMSIS-RTOS2 adapter layer: thread host, event queue, eloop, os shims, l2_packet carrier, driver-slot skeleton); `tests/host/wpa-port-test/` (host gate) and the WLAN board image |
| Reason | The wireless milestone's authentication face needs a supplicant before any WPA2 network can be joined. This fork pre-provisions a single driver slot (`src/drivers/drivers.c`: `CONFIG_DRIVER_EMBOX`, the `wpa_driver_embox_ops` entry) whose neutral contract first-party code implements, so the vendored tree is consumed with one patch instead of a driver backport |

Notes:

* Lineage: hostap `2.11-devel` base (`src/common/version.h`: `VERSION_STR "2.11-devel"`); the fork
  steers the NXP build gates for a non-Linux port and keeps WPA2 sessions and Ethernet traffic
  working on that carrier. Both changes are inside the pin.
* The driver slot is the whole reason this fork is used: `wpa_drivers[]` carries an
  `wpa_driver_embox_ops` entry behind `CONFIG_DRIVER_EMBOX`, which the first-party adapter
  implements with a nine-operation neutral contract. The consumer side never sees hostap types.
* Upgrading the pin means re-reviewing the 45-unit compile set and the three adapter rewrite
  points; it is a spec-level decision, not an implementation-round chore.
