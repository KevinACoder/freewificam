# rtl8xxxu/patches - record only

The tree is imported **already patched**. This directory records the patch set
and the digest of the source tree the build compiles; it is not a build-time
replay directory.

| Item | Value |
|---|---|
| Upstream | Linux `drivers/net/wireless/realtek/rtl8xxxu/` |
| Base (= pin) | `b98a1dfd4661670b9e16d9c74da43e0389ef7e62` |
| License | `GPL-2.0-only` (every file's SPDX line) |
| Patch list | `0001-rtl8xxxu-enable-ap-mode-for-the-rtl8188eu.patch` (the 8188e fileops gains `supports_ap` and `max_macid_num`), `0002-rtl8xxxu-set-the-station-macid-in-the-v3-tx-descriptor.patch` (`fill_txdesc_v3` writes the frame's macid into `txdw1[5:0]`, plus the `TXDESC32_MACID_MASK` define), `0003-rtl8xxxu-repeat-the-ap-beacon-from-the-host-on-the-rtl8188eu.patch` (a chip-gated `host_beacon_repeat`: the 8188E does not repeat a host-downloaded beacon, so the host recomposes one per beacon interval) |
| Applied | all three replayed with `patch -p3 --fuzz=0` on 2026-11-14; apply order is the file-name order (`0003` sits on `0001`+`0002`) |
| Manifest | `MANIFEST.sha256` - the digest of the imported tree, excluding this directory (13 files) |
| Verify | `cd wireless-drivers/rtl8xxxu && sha256sum -c patches/MANIFEST.sha256`; and `tools/build/import-driver.py --driver rtl8xxxu --check` replays the two patches onto the pin and compares (the other 11 files stay byte-identical) |

## Why the tree is no longer zero-patch

Mainline rtl8xxxu has no AP mode for the RTL8188EU: its fileops never sets the
`supports_ap` gate, and `rtl8xxxu_fill_txdesc_v3()` ignores the `macid` argument
it receives. The rest of the AP path is chip-agnostic and already in this pin
(`add_interface`'s AP branch, `start_ap`, `set_tim`, `sta_add`/`sta_remove`,
`update_beacon_work`, the BEACON TX queue), and the vendor driver runs the same
chip as an AP with the same firmware image, so the target is reachable — it just
needs these two pieces.

The fixes are the upstream ones:

- `0001` is the pending upstream RFC's enablement patch (v3 patch 4,
  <https://lkml.iu.edu/hypermail/linux/kernel/2603.1/11890.html>), with the
  station count taken from the vendor driver's `NUM_STA` (32, the same value
  the 8192CU family sets here) instead of a new chip macro;
- `0002` is the gap the rtl8xxxu maintainer named in review of that series
  (<https://lkml.iu.edu/hypermail/linux/kernel/2603.1/12855.html>); the vendor
  driver writes `mac_id & 0x3F` into the low bits of `txdw1` for both data and
  management frames (`hal/rtl8188eu_xmit.c:228,288`), and the queue occupies
  `[12:8]` only. STA mode is unchanged by it: its macids are 0/1, so the field
  was already zero there.

`0003` is this line's own fix and has no upstream counterpart. The 2026-11-14
AP boots showed the chip emits exactly one beacon (the host-composed start-up
one) and none after, with `BCN_VALID`, `EN_BCNQ_DL` and `EN_BCN_FUNCTION` all
reading back set; clearing `REG_BCN_CTRL` bit4 (the vendor's post-download state)
changed nothing. The vendor driver hands the beacon to firmware through a
reserved page, which rtl8xxxu does not implement; its AP-capable chips repeat
the host-downloaded beacon in hardware, and this chip does not. So the beacon
timer moves into the host for the 8188E only (`host_beacon_repeat` in the
fileops table). Boundary: the cadence is the host timer's, not the chip's TBTT.

The AP-mode rate-control changes from the same RFC (per-macid `ra_info`, the
`dynamic_tx_rpt_timing_counter` move, the max-report-macid update) are **not**
carried: this line's AP round is a single-client open AP, and those patches
change multi-station rate control. They are the pre-registered fallback arm if
the data plane shows a rate-control signature instead of a macid one.

A future upstream acceptance of the series removes `0001`/`0002` here, moves the
pin forward and notes both in `../../docs/PROVENANCE.md` and
`wireless-drivers/rtl8xxxu/PROVENANCE.md`.
