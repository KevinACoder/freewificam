# STATUS

> Detailed RK3568 performance tables: [RK3568-PERFORMANCE.md](RK3568-PERFORMANCE.md).
> F429 UVC camera parameters and results: [F429-UVC.md](F429-UVC.md).
> F429 UAC microphone parameters and results: [F429-UAC.md](F429-UAC.md).

- Updated: 2026-11-20 (F429 wired Ethernet: the board's LAN8720A on the
  STM32F4 ETH MAC, driven by the imported ST HAL ETH driver.  Two relay
  cold boots PASS: 100 Mbit/s full duplex, DHCP lease 192.168.0.34/24
  from the router, gateway 10/10, and at least one public echo 10/10 per
  boot (223.5.5.5 in both; the Pi's own public pings 5/5 both times).
  Details in [F429-ETH.md](F429-ETH.md))
- Updated: 2026-11-20 (F429 UVC/UAC to Pi RTSP: four staged profiles, link /
  one frame / 20 s video / 20 s audio+video each 2/2 relay cold boots; 269
  video frames per 20 s, audio `gaps=0 err=0`, H.264+AAC RTSP records at
  640x480 / 16 kHz; details in [F429-STREAM.md](F429-STREAM.md))
- Updated: 2026-11-20 (F429 UAC microphone: descriptor census, 10 s capture
  and 20 s video+microphone each 2/2 relay cold boots; 16 kHz mono 16-bit,
  32 bytes per 1 ms frame, zero gaps and zero transport errors)
- Updated: 2026-11-15 (RTL8723BU: C3-C5 2/2, C6T 2/2; three of four
  throughput directions align, board-receive TCP is 62.5% of the same-driver
  GT; the chip needed no driver change, only a profile/firmware selection)
- Stage: **the imported wireless stack registers, the AC 7260 attaches, joins
  the bench AP and moves data through lwIP**.  C0/C1/C2/C3/C4 are 2/2 cold
  boots; C5-open and C6-open (ICMP over wl0) are 2/2 in
  the 2026-11-11 bench run.  The C6T throughput round and the
  stability round are this round's work; their numbers land in
  the 2026-11-11 bench run, not in this stage line.

## Stages

Each stage is stated as what it is. A stage is announced only when the one
below it has been reached; "compiles" and "links" are not "runs".

| Stage | Meaning | State |
|---|---|---|
| C0 compile | the profile's translation units produce object files | **reached** for `rk3568-tick`, `rk3568-net`, `rk3568-pcie-nvme`, `rk3568-emmc`, `rk3568-usb-msc` |
| C1 link | no undefined symbols; the linker gates pass | **reached** for the same five (gates: `_start`, vector table, backend symbols, 0 undefined) |
| B board | two clean cold boots per profile, judged by `tools/rtos/judge.py` | **reached**: five acceptance profiles and two component profiles, 2/2 each (2026-11-10) |
| K component | `components/liblinux80211/` builds as its own archive and proves its bus seams on the board | **reached**: `liblinux80211.a` (4.08 MB) + `rk3568-kpi-mock` / `rk3568-kpi-live` 2/2 |
| S0 stack | `net/mac80211` + `net/wireless` compiled one `-c` at a time against the component's Linux face | **reached**: 62/62 TUs (`docs/LINUXKPI-GAP.md` v2) |
| S1b driver | the vendored iwlwifi (294 files) compiled against the face | **reached**: 72/72 TUs () |
| S1c link | stack + driver partial link, classified undefined list, one winner per API symbol | **reached**: 314 external refs, 186 provided, 112 real gaps, 0 duplicate winners (`bench record`) |
| C2 register | `ieee80211_alloc_hw` + `register_hw` + default `wlan0` on the board | **reached 2/2**: the 2026-11-10 bench run (clean image, cold boots) |
| C3 attach | iwlwifi claims `8086:08b1`, firmware `17.bfb58538.0` ALIVE, `wlan0`, `dev_open` -> start rc=0 | **reached 2/2**: the 2026-11-10 bench run |
| C4 scan | real `NL80211_CMD_TRIGGER_SCAN` bytes through nl80211's handler -> iwlwifi hw scan -> cfg80211 BSS table carries the bench AP | **reached 2/2**: the 2026-11-11 bench run (SSID/BSSID/ch5) |
| C5/C6-open | association (open AP) / ICMP over wl0 | **reached 2/2**: the 2026-11-11 bench run |
| C6T throughput | TCP/UDP phases over wl0 against the GT arm; the 80% / 1% gates | **reached**: GT 2/2 + baseline 2/2 + pool128 2/2; T1 passes on pool128 (TCP down 142.6%, up 93.3%, UDP 0% loss, T2 0.002%/2.37%) - the 2026-11-11 bench run |
| 8189FTV functional ladder | the vendored vendor stack on the board's SDIO port: attach (embedded firmware), scan, open association, ICMP both ways | **reached 2/2** (2026-11-13): the 2026-11-13 bench run (`rk3568-rtl8189fs-sdio` / `-sta`) |
| 8189FTV throughput - station | the throughput table against the GT denominator | **reached 2/2** (2026-11-13): the earlier stall was the pump's fixed 10 ms tick, not the card - adaptive pacing plus a yield when the driver thread holds the claim moved the downlink from 48% to 69-97% of the same-day reference (TCP up 105-110%, UDP down 90%, no measurable loss on the 35 Mbit/s rung). The UDP uplink has no common instrument on the two arms and is not claimed - the 2026-11-13 bench run |
| 8189FTV throughput - access point | the same table with the board as the AP | **measured, gate not met** (2026-11-13): beacon/association/data are 2/2 and the data-plane runs, but the role is 43-44% of the same-day reference on TCP downlink and 35-42% on TCP uplink; the UDP ladder is at parity with no loss at 15 Mbit/s and voids on loss from 25 up (9-12% at 25, 35% at 35). Open face: AP-side rate/aggregation state, which the port's air census shows as `ampdu=0`. Also unexplained: a broadcast/multicast clone comparison warning that fires on every AP boot and never on the station role - the 2026-11-13 bench run |
| C3–C6 8851BU | the rtw89 (USB) stack on the board's DWC3 xHCI: attach (compiled-in firmware), the real nl80211 scan, open join, ICMP both ways | **reached 2/2** (2026-11-13): C3 (`usb drivers=1`, firmware `0.29.41.5`, `wlan0` after 2400 ms, start rc=0), C4 (`scan rc=0`, BSS 2432/-17), C5/C6-open (connect rc=0, netif `192.168.99.2/24`, 56/1400 20/20 on the board arm and on the AP->board arm), judge PASS every arm. The driver is the GT pin's tree, zero patch; the round's failures were instruments (case registration, adapter link, runner stack, peer window) and are recorded as such - the 2026-11-13 bench run, ADR `docs/decisions/2026-11-13-rtw89-usb.md` |
| C6T 8851BU | TCP/UDP phases over the 8851BU's wl0 against the GT denominator | **tested, gate not met on the downlink** (2026-11-14): the previous round's stall (`mac80211-agg`, reorder ring frozen) was a misdiagnosis. The 185-187 objects were rtw89's USB `rx_queue` (unparsed receive URB buffers: RX-descriptor head, len up to 12.8 KB), and the real cause was this port's `WARN_ON_ONCE` being `WARN_ON`: mac80211's `softirq_count()==0` warning printed once per received frame, the RX worker spent 25-28 s of the 30 s window formatting and filtering them, the driver's URB queue backed up to 187 and the skb pool hit 128/128. Making WARN_ON_ONCE truly once removed it (same diag arm: 12 MB then stop -> 92 MB / 28 s, pool 20/128). Clean arm 2/2 cold boots, seven phases ok, PASS C6T: TCP down 33.30 / 30.51 Mbit/s (0.75x GT 42.3, gate 0.80 not met by 1.9 Mbit/s), TCP up 45.27 / 45.93 (0.81x, passes), UDP 40M 0% loss within T2 (board +0.31%). Old-repo RTOS net80211 was 0.90x up / 0.77x down on netio, so the downlink ratio is the same class and the uplink is ~9 points lower; tools differ, only ratios compare. Reopen: air ledger (MCS/retry/BA length vs GT), RX-worker latency/priority arm, lwIP receive path - the 2026-11-14 bench run |
| A1-A3 8851BU AP | the same USB card as an AP: hostapd owns the AP MLME, mac80211 the AP state machine, rtw89 the H2C beacon; the Pi's MT7921AU joins and both directions pass ICMP | **reached 2/2** (2026-11-14): `SET_INTERFACE rc=0 type=3`, `dev_open rc=0`, `hostap start rc=0 ready=1` at 14.0 s, the monitor sees the beacon (SSID `rtos-8851-ap`, ch5/2432, interval 100, DTIM 2), the Pi joins first try (`sta n=1 hostap=1 mac=90:de:80:4c:38:c5`, ~8 s), 56/1400 B ICMP 20/20 each way with dynamic ARP, `rx_foreign/no_netif/input_fail=0`, judge PASS twice - the 2026-11-14 bench run |
| A4 8851BU AP | the AP phase table against the same-day GT AP arm (same card/pin, xHCI only) | **measured 2/2; UDP down fixed, TCP up short** (2026-11-14 second round): the same image `-align-tp` sha `da2711c9…` passes `PASS APT` in the two boots (A1/A2 first try, five phases, judge 2/2). The old 15.4% UDP-down loss was a port bug, not the Pi: `udp_blast` reused one pbuf after `udp_send`, lwIP left its 42-byte headers in it, every datagram became a 1540-byte IP packet split into two fragments. One pbuf per datagram gives `tx_frames = datagrams`, one frame on air, and **0.52% / 1.39%** loss at 10M offered. Exact 5 ms pacing raises UDP down to **9.57 / 8.11 Mbit/s** (mean 8.63). The `tcpdn2` phase now passes 2/2 (the two Pi TCP servers are started as one sequential job, not racing port 5001). Against the GT fallback (the GT fallback boots): TCP down **116.9%**, TCP up **78.4%** (fail; 10 s common window, 7.5/5.8 vs 8.50), UDP up **90.4%** (8.24 vs 9.115). Scatter TCP down 11%, TCP up 16%, UDP down 15% - means only. app-vs-app down 0.61%; app-up ~0 with the board's `udp_in` count (the sink is 5.1% only because it starts after the 3 s margin while the Pi's client starts at the marker). The GT same-ruler rerun failed three times on instruments (board-shell echo, sink path, phase-loop lag), so the denominator is the first round's xHCI boots; the pre-registration records the fallback. The an earlier A4 boot silence is corrected (phase 3, ~25 s, not tcpup/12 min) and does not reproduce with the fixed server sequencing; mechanism unproven - the 2026-11-14 bench run |
| C3–C5 8188EU | the vendored rtl8xxxu (USB) on the bench's "xhci0" - the GT kernel's xhci-hcd.0.auto at 0xFCC00000 (this board map's busid 3) - behind the external hub (`bus=3 hub=2 port=2`, next to the 8851BU on port 1): attach with the compiled-in `rtlwifi/rtl8188eufw.bin` (`Firmware revision 28.0`, `signature 0x88e1`, MAC `20:f4:1b:52:8f:01` - the same dongle the previous net80211 line used as its pilot), the real nl80211 scan (`labwifi-ar-ht20s@2432`, -49 dBm), open join first try (`connected waited_ms=100`), and 56 B / 1400 B ICMP 20/20 each way with the peer's own rounds 20/20 and the AP's ICMP ledger closing both directions (44/44, 40/40); AP's station dump -25 dBm against the old line's GT -24 | **reached C3 2/2, C4 2/2, C5/C6-open 2/2** (2026-11-14): c3-2/c3-3, c4-2/c4-3, c5-1/c5-2, judge PASS on every arm; the driver is the GT pin's tree, zero patch, byte-identical to the previous line's vendored copy. The round's failures were two instruments (a cold-boot U-Boot timeout, and an air-filter key the C4 case did not print yet) plus one pre-existing link defect the new image exposed: the rtw88-only `--wrap`s were global, so the first non-rtw88 wireless image failed at link - now conditional on `CONFIG_LIBLINUX80211_RTW88`. C6 throughput, WPA2, AP and monitor roles are this card's next four branches - the 2026-11-14 bench run |
| A1–A3 8188EU AP | the same card as a 2.4 GHz AP (`rtos-8188-ap`, ch5/2432, open) with the imported NXP hostapd as the AP MLME, the MT7921AU peer joining and ICMP both ways; the beacon is host-composed and host-sent every ~102 ms | **reached 2/2** (2026-11-14): `set_iface rc=0 type=3`, `start rc=0`, `hostap start rc=0 ready=1`, `link freq=2432`, the monitor captures the board's beacon (BSSID `20:f4:1b:52:8f:01`, interval 100, DTIM 2; 144 / 141 beacons in the captures), the peer joins first try (`sta n=1 hostap=1 mac=90:de:80:4c:38:c5`), 56 B / 1400 B ICMP 20/20 each way and the peer's own rounds 20/20 at 0% loss. The role needed three recorded driver patches (the upstream RFC's `supports_ap`/`max_macid_num`, the maintainer's txdesc macid, and this line's host beacon repeat - the chip emits only the one start-up beacon, measured, and the vendor's firmware-reserved-page path is not implemented in rtl8xxxu). Single-client open AP only; A4 throughput, WPA2, multi-client and 5 GHz are open - the 2026-11-14 bench run |
| MON 8188EU monitor | the same card as an RX-only monitor (ch5/2432): `SET_INTERFACE(MONITOR)` rc=0 type=6, `dev_open devtype=803`, `SET_WIPHY 2432` rc=0, then a 15 s window; the frames go to the port's sink (radiotap it_len 26-29, freq 2432) and never to lwIP (rx_frames 0->0, tx 0) | **reached 2/2** (2026-11-14): mon5/mon6 judge PASS - 528/569 beacons in 15 s (the whole channel carries 7-8 BSSes, 35-37 beacon/s; the bench AP itself 139/140 = 9.3/s, named from the sink's per-BSSID census), 21/19 data frames from the Pi client; plus **M5 1/1** (`ab3`): the Pi's MT7921AU monitor counts the same bench BSSID at 9.82/s vs the DUT's 9.07/s (7.6% apart). The role needed the driver's fourth recorded patch (`supports_monitor`, `WANT_MONITOR_VIF`, the promiscuous RCR); the chip needed no RX-engine change. RX-only: no injection/ACTIVE/otherbss - the 2026-11-14 bench run |
| C6T 8188EU STA | the station's throughput table over the wl0 netif (loopback, tcpdn, udpdn, tcpdn2, udpdn2, tcpup, udpup) against a same-day GT arm: the same pin's in-tree `rtl8xxxu`, the same firmware blob (rev 28.0), the same bench and peer | **measured 2/2** (2026-11-14, two boots): board-receive TCP **40.5 Mbit/s = 104% of GT** (39.1), UDP-receive 9.49 (0.0% loss) = 90%, UDP-send 10.0 = 95%; board-send TCP **5.26 = 14% of GT's 37.65** - the round's finding, and the case's repeat phases (tcpdn2) are samples, not gates (the lwiperf second session is intermittent). One STA boot hung mid-RX at 40 Mbit/s (one boot, console stops after `AMPDU_RX_START`), not reproduced in the two boots - the 2026-11-14 bench run |
| A4 8188EU AP | the AP role's four direction cells with the roles swapped (8812AU as the client, MT7921AU as the monitor), compared against the same-day GT STA arm | **measured 2/2** (2026-11-14 ap1/ap3): board-receive TCP 38.16 = 98% of GT, UDP-send 10.0 = 95%; board-send TCP 6.38 = **17%** (the same gap as the station role: AP/STA 121%), UDP-receive 5.42 = 52% with 12.4/15.4% loss (the one role-specific cell; STA role 0% loss). Alignment verdict: 3 of 4 directions align, the send-TCP gap and the AP-role UDP receive are open mechanisms - the 2026-11-14 bench run |
| C3–C5 8723BU | the same vendored `rtl8xxxu` on the board's **EHCI** dongle (RTL8723BU `0bda:b720`, the `1a86:8091` hub's port 2, `bus=1 hub=2 port=2 intf=2`; MAC `88:e6:28:53:10:9c`): the probe reads `RTL8723BU rev E ... 1T1R, TX queues 3`, loads `rtlwifi/rtl8723bu_nic.bin` (`Firmware revision 35.0`, `signature 0x5301`), registers `wlan0` and starts; the real nl80211 scan finds `labwifi-ar-ht20s@2432` (-16 dBm), open join and 56 B / 1400 B ICMP 20/20 each way | **reached C3/C4 1 dedicated boot each + repeated in every C5/C6 boot (5 more), C5/C6-open 2/2** (2026-11-15): `c3-2`/`c4-1`/`c5-1`/`c5-2` judge PASS with `--expect chip=8723bu`; the chip needed **zero driver changes** - the same 13-file GPL tree, only `CONFIG_RTL8XXXU_CHIP_8723BU` (firmware blob) and the EHCI controller family differ - the 2026-11-15 bench run |
| C6T 8723BU | the same station's throughput table, against a same-day GT arm using the same pin's in-tree `rtl8xxxu` and the same `rtl8723bu_nic.bin` (the GT board carries both dongles; the arm selects by MAC/firmware line) | **measured 2/2** (2026-11-15, two boots per arm): board-send TCP **21.16 = 115% of GT** (18.35), UDP-receive 9.49 = 90% with 0.0% loss, UDP-send 10.0 = 96%; board-receive TCP **16.94 = 62.5% of GT** (GT itself 21.4/28.7, decaying inside the 30 s window) - the one open cell, mechanism pointed at the EHCI RX path/RX aggregation, not the air (MCS7/72.2). One throughput boot hung at the `tcpup` phase begin (1/3, one boot, power-cycled), and the lwiperf second session returns 0 bytes in 3/3 RTOS boots (GT: 25.4/27.7) - both open defects, not rate gates - the 2026-11-15 bench run |
| MON 8851BU monitor | the same USB card as an RX-only monitor: `SET_INTERFACE rc=0 type=6`, `dev_open devtype=803`, `SET_WIPHY 2432 rc=0`; the frames go to the port's sink, not lwIP | **reached 2/2** (2026-11-14): both cold boots print `PASS MON` with 161 beacons/15 s (10.7/s; 236/231 frames), radiotap `it_len` 26/39 and 26/50, dominant `freq=2432`, `bad=0`, lwIP `rx_frames` 0 -> 0 and `tx_packets=0`; the ten 2412 frames are the default channel before the request landed. The census also carries probes/probe-resp/auth/assoc/data/ctrl from the Pi's client loop. No same-card Linux monitor arm this round: the reference is the AP's 100 TU (~9.77/s), and the role is RX only - the 2026-11-14 bench run |
| C6S stability | 30 min TCP downlink + 10 reconnects; dynamic ARP | **reached 1/1 + partial**: `stab1` 30 min (1,777,484 ms, 13.95 GB, peer 60.4 Mbit/s), 10/10 reconnects (713-903 ms, 4/4 ping), 60 health lines, no reset, judge 4/4; dynamic ARP `PERMANENT -> REACHABLE` with `dev_set_rx_mode`/allmulti. `stab2` was aborted at 7.5 min for session close (partial, not a verdict); PS buffering remains open - the 2026-11-11 bench run |
| A0/A1 AP | the AC 7260 becomes an AP: `SET_INTERFACE rc=0 type=AP`, `START_AP rc=0`, monitor sees its beacons with the GT IE order and its probe responses **2/2**; the Pi's scan lists the BSS - the 2026-11-12 bench run, ADR `2026-11-11-ap-mode.md` |
| A2/A3 AP (hostapd) | **reached, repeated**: the AP MLME is the ported NXP hostapd (`third-party/wpa_supplicant-rtos`), not the hand-written one. `hostap start rc=0 ready=1`, the Pi associates, hostapd's station table and `NL80211_CMD_SET_STATION(AUTHORIZED)` land, both ping payloads 20/20 (RTT 1-2 ms) in 6 of 9 rounds of 2026-11-12 - the 2026-11-12 bench run, ADR `2026-11-12-ap-mlme-port.md` |
| A5 AP-WPA2 (G1) | **reached 2/2**: the 7260 is a WPA2-PSK/CCMP AP and the Pi's MT7921AU completes the 4-way. PTK+GTK `set_key rc=0`, 4/5 EAPOL frames on air, `wpa_state=COMPLETED`, 56/1400 B ICMP 20/20 **both directions**, dynamic ARP `REACHABLE`, `PASS APW`, judge PASS. The round also added a real `ccm(aes)` provider (the 2026-11-12 bench run, ADR `2026-11-12-ap-wpa2.md`) |
| A4 AP | **TX stall fixed**: the stop was the port's single-shot NAPI poll, not the device. A poll returned the whole budget (`napi_ret=64`), iwlwifi's poll therefore did not re-enable `CSR_INT_MASK`, and the port cleared SCHED without a second poll; the device still had pending RX (`int=0x92000000 mask=0`). Root cause 2/2 (`table1/table2`), fix validated (`fix1` verdict=none; standard AP arm `a4-1b`/`a4-2` both `PASS APT`). A4-P2: TCP down 73.2 Mbit/s (99.9% of GT 73.3), UDP 40M loss <=0.12%, app-vs-app <=0.11% pass; **TCP up 43.9 Mbit/s (57.3% of GT 76.6) fails the 80% gate** - the board RX path, next round. See the 2026-11-12 bench run, ADR `docs/decisions/2026-11-12-ap-a4-napi-budget.md` |
| A6 AP 5 GHz | **not usable on this Linux stack, and not a port defect**: iwlwifi fills every 5 GHz channel's `orig_flags` with `NO_IR`, and cfg80211's merge only ORs rule flags onto `orig_flags`, so no country clears it. The non-LAR path maps two NVM bits to `NO_IR` (`IBSS` and `ACTIVE`); which one is unset was not measured (the GT kernel has `CONFIG_IWLWIFI_DEBUG=n`, so the NVM channel dump cannot print). The card itself is dual-band and its 5 GHz RX/TX work - only AP-mode permission is missing, which is Intel's design: "AP mode is supported - not on 5.2 GHz band", and iwlwifi "enforce[s] the regulatory in the device itself" (iwlwifi maintainer, linux-wireless 2014-06-30). A different card (LAR-capable, 7265D/8260+) is the only route to a 5 GHz AP; 7260-17 has no LAR, hence no `ALLOW_20MHZ_ACTIVITY` 20 MHz AP permission either. hostapd disables NO_IR channels before any of that. GT (mainline + iwlwifi, signed `regulatory.db` added, `iw reg set US` effective - 12/13 became `disabled`) fails identically: `Frequency 5180 ... not allowed for AP mode, flags: ... NO-IR`. The port reports the band (25 channels, HT) and every candidate's flags faithfully - the 2026-11-12 bench run |
| C5/C6 5 GHz STA | **partly reached**: the 5 GHz band is real in the wiphy (25 channels, `5g_ht=1`), a passive scan finds the Pi's 5 GHz AP on 5180 (RSSI -38 dBm), and the monitor sees the 7260's own probe requests on 5180 (RX and TX both work). Association does not start: `NL80211_CMD_CONNECT` returns 0, cfg80211's SME never sets `current_bss` (`progress=0x0` for 20 s, twice), and no auth/assoc frame reaches the air. 2.4 GHz through the same seam completes in 300 ms. Blocked with a reopen condition, not a verdict on the band - the 2026-11-12 bench run |
| MON monitor (RX-only) | **reached 2/2**: `rk3568-iwl7260-mon` — `SET_INTERFACE rc=0 type=6`, `dev_open rc=0 devtype=803`, `SET_WIPHY freq=2432 rc=0`; the port's sink counted 148/156 beacons in 15 s (**9.87 / 10.4 per second**, GT's own 7260 monitor arm 9.25/s, +6.7% / +12.4%), plus probe req/resp, auth, assoc, data and ctrl; radiotap `it_len` 56/64 matches GT's set, channel 2432, `bad=0`; lwIP `rx_frames` unchanged and `tx_packets=0`. Three port defects found and fixed (`dev_open` never emitted NETDEV_UP → `-EBUSY`; `skb_copy_expand` was a NULL stub → monitor frames silently dropped; the sink read radiotap at fixed offsets). **RX only**: injection/TX, channel hopping, 5 GHz and the 5 min stability arm are not tested — the 2026-11-12 bench run, ADR `docs/decisions/2026-11-12-ac7260-monitor.md` |
| K1/K2 context | the host semantic bench and the three execution-context experiments | **reached on the host model**: 33/33 cases, `--break` red; ADR `2026-11-10-execution-context-model.md`; not board evidence |

## Receipts

The C0/C1 numbers are the image lines in the build logs; the per-profile
sha256/size are printed by the build itself (`build/build-info-freertos.txt`).
Board evidence, when it lands, goes under the project's evidence ledger, not
here: this file only says which stage has been reached.

## Preconditions

See `README.md`: the uniprocessor assumption is a precondition of the design,
not an implementation detail.
| C5 8821CU station | the same card joined to the bench AP: the C5 gate (56/1400 B ICMP both ways) and the link mechanism | **blocked** (2026-11-14 second round): four candidate mechanisms were eliminated with one instrumented boot each - power save is off (`cfg_ps=0`, `ps_enabled=0`), the TX report matches the driver's own V1 branch (`sn_match=2/2`, `tx_status=21 == dequeue=21`; the first round's `sn 0x00/0x04 对不上 4/8` was read with the opposite macro), the AP sees the station at -24 dBm (GT -24), and there is no BA session (`agg_enable_bitmap=0`). The AP-side 802.3 ICMP ledger is the finding: the AP answered 23/23 of the board's requests while the board counted 11/20 replies, and a same-day GT control boot on the same card/port/AP is 20/20 with 0% loss - so the bench, the card and the port are not the variable. Reopen: (1) delay the AP's reply after the board's transmit and plot the loss (`tc netem`), (2) force MCS7 through `dm_info.fix_rate` and see whether the loss follows the rate or the transmit itself, (3) read the same PHY-DM registers on GT (`igi_bitmap=0` on the RTOS side) - the 2026-11-14 bench run |
| A1-A2 8821CU AP | the RTL8821CU (rtw88, EHCI) as a 2.4 GHz AP: hostapd owns the AP MLME, mac80211 the AP state machine, rtw88 the reserved-page beacon | **A1 2/2 reached; A2 1/2** (2026-11-14): both boots print `set_iface rc=0 type=3`, `start rc=0`, `netif up 192.168.99.1/24`, `hostap start rc=0 ready=1` at 17.4 s and `link freq=2432`; the monitor sees the BSS (`rtos-8821-ap`, ch5, interval 100, DTIM 2) and the Pi joined once (`sta n=1 hostap=1 mac=90:de:80:4c:38:c5`, 7 s after hostap start). The second boot's `iw connect` timed out and the third is the repetition. A3 is not reached: the same first boot`s counted round is 0/20 with the board's own receive ledger pending. This is the RTL8821CU's first AP role on this line - the 2026-11-14 bench run |

## STM32F429 (MCU) wireless line — 2026-11-15

The MCU is a separate platform, judged by its own ladder (see
`docs/F429-WIFI.md`; it does not transfer to the RK3568 numbers):

| gate | state |
|---|---|
| SDRAM bring-up + DMA | **2/2 PASS** (`sdram test ok size=33554432`) |
| attach / link / register | **2/2 PASS** |
| scan (BSS + probe request on air) | **PASS** |
| association (auth + assoc, AP station table) | **3/3 PASS**, 200 ms |
| data plane (lwIP, ICMP) | **PASS** (2026-11-16 size-round image): `stm32f429-wifi-c6` (726,080 B, sha `74ada2a0…`) runs both counted ICMP rounds in one boot - 20x56 B and 20x1400 B, 20/20 each, `rx_err=0`, `PASS DATA` - and the AP's own capture carries 44 echo requests + 44 replies; **2/2 relay cold starts** with the same image. The earlier 1/2 was the MCU-reset trap (an MCU reset does not reset the 8188EUS) at scan, `rx_err=32`, before the data-plane gate; the two real blockers were outside the driver (a stale PERMANENT ARP entry on the AP, and the armv7m console print lock spinning with interrupts enabled). See the 2026-11-15/16 bench runs |
| data-plane throughput (c6t, report-only) | **measured** (2026-11-15): the `stm32f429-wifi-c6t` image (736,552 B, sha `94e55254…`) runs the four phases in one boot and all moved data - end-to-end TCP down **2.84 Mbit/s** (stable: 2.78 / 2.84 across two boots), TCP up **1.85 Mbit/s** (0.36 in the other boot - not stable, unexplained), UDP down **6.12 Mbit/s** counted with **0 drops** (the Pi offered 6.86), UDP up blast 0.16 Mbit/s offered with 90% delivered. The host is full-speed USB (bulk ceiling near 9.7 Mbit/s), so the UDP cell is the USB link being approached, not the radio; these samples do not transfer to the RK3568 line. No gate and no stability claim - the 2026-11-15 bench run |

| AP role (A1-A3) | **A1 reached; A2/A3 blocked in USB** (2026-11-16): the 8188EUS becomes an open AP (`rtos-8188-ap`, ch5/2432, HT20/WMM) with the port's own minimal open-AP MLME (`CONFIG_AP_MLME`) - the imported hostapd costs 326,612 B of flash and the 1 MiB part cannot carry it. The BSS comes up and beacons on air: `set_iface rc=0 type=3`, `start rc=0`, the 63/101 beacon template, `start_ap rc=0 freq=2432 interval=100 dtim=2`, `link freq=2432`, the monitor counts 614 beacons from the board's BSSID and the peer's scan lists the BSS with the template's IEs. The peer's authentication does not reach the stack: at interface start 29 of the driver's 32 receive URBs come back with a bulk-IN babble (2376-byte transfer, 64-byte max packet, zero bytes moved), the driver frees a failed receive URB without resubmitting it, and the receive count freezes for the boot; a bounded retry of the same transfer did not help. The station image on the same board, cable and dongle runs `rx_err=0` and passes scan/associate/data, so the variable is the AP receive start. Reopen: a receive path that keeps completing in the AP role (see `F429-WIFI.md` section 6). |

The F429 association defect was a frozen Linux clock (`jiffies` stayed 0 because the
board's FreeRTOS config never enabled the tick hook), not a stack or driver
defect; the 2026-11-15 bench run has the trace and the fix.


## F429 AP6181 (BCM43362)

The onboard module runs the Cypress WICED WWD driver as a first-party port
(`wireless-drivers/ap6181`, not the Linux stack). Each rung is a case with its
own profile, judged on relay cold boots:

| Rung | Case / profile | State |
|---|---|---|
| attach | `ap6181_attach` / `stm32f429-ap6181-attach` | **2/2** (3/3 in the run): `wifi_on rc=0`, MAC `9c:b8:b4:74:5f:e1` |
| scan | `ap6181_scan` / `stm32f429-ap6181-scan` | **2/2**: 18 and 13 BSS, the bench AP among them |
| STA join | `ap6181_join` / `stm32f429-ap6181-join` | **2/2**: link up in 700-800 ms, BSSID `1c:a7:70:de:29:a1` |
| data plane | `ap6181_data` / `stm32f429-ap6181-data` | **2/2**: 20/20 ICMP over the bench AP, 20 requests / 20 replies on the AP's own capture |
| throughput (report only, no gate) | `ap6181_tput` / `stm32f429-ap6181-tput` | **measured 2/2**: TCP **TX 6.06 / 6.33 Mbit/s**, **RX 9.83 / 10.15 Mbit/s**, 4 MiB per phase, the AP's own counters agreeing to 0.1 Mbit/s |

The image gate (`make wiced-image-gate`) proves the images carry no
`linuxkpi`/`mac80211`/`cfg80211`/`brcmfmac` symbol. Details, including the two
seams that bit (one lwIP init per image, and the pbuf headroom the driver
prepends into), are in `docs/F429-AP6181.md`.

## F429 UVC camera (2026-11-20)

The same OTG_HS full-speed host carries a Logitech QuickCam Pro 9000
(`046d:0990`). The DWC2 port's missing isochronous path is patch 0011 (one
packet per channel activation, per-packet `actual_length`/`errorcode`, one
channel per endpoint); the camera image is `stm32f429-uvc-{detect,480,
800x600,720p}`.

| Rung | Case / profile | State |
|---|---|---|
| identity + mode/probe census | `uvc_detect` / `stm32f429-uvc-detect` | **PASS**: `046d:0990`, 3 formats, 7 alts, MJPEG 160x120..800x600, **`720_line_modes=0`** |
| open | `uvc_open` / `-480`, `-800x600` | **2/2 each**: 640x480 alt4 mps=640 dmp=640 ival=1; 800x600 alt6 mps=944 dmp=944 ival=1 |
| raw ISO transport | `uvc_iso_smoke` | **2/2 each**: 16/16 URBs, 1536 bytes, 0 packet errors |
| stream, 640x480 | `uvc_stream` / `-480` | **2/2**: 248/276 frames, 12.38/13.76 fps, 1917/2065 kbit/s, err=0, 1 dropped, JPEG SOI/EOI on every frame |
| stream, 800x600 (max FS mode) | `uvc_stream` / `-800x600` | **2/2**: 275/275 frames, 13.71/13.73 fps, 2907/2899 kbit/s, err=0, 1 dropped |
| 1280x720 | `-720p` | **not available on this camera at full speed**: the FS descriptor set has no 720-line mode. Booted once as an expected negative: `uvc: open FAIL mode 1280x720 mjpeg not in the camera's tables`, `1/4` cases (detect only). The negative is the camera's mode table, not the bus |

The full-speed isochronous ceiling is 1023 B/1 ms = 8.184 Mbit/s; the 800x600
arm uses 2.9 Mbit/s of it, so the missing 720p is not a bandwidth subtraction.
Details, the mode table, the engine's contract and the commands are in
`docs/F429-UVC.md`; the USB-host facts are in `docs/F429-USB-HOST.md` section 8.

## F429 UAC microphone (2026-11-20)

The camera's interfaces 2/3 are a USB Audio Class 1.0 microphone; the line
carries a first-party host class (patch 0012) and a PCM capture engine.  The
camera has no UAC OUT, so playback is not part of this line.

| Rung | Case / profile | State |
|---|---|---|
| descriptor census | `uac_detect` / `stm32f429-uac-detect` | **PASS**: interfaces 2/3, `bcdADC=0x0100`, 1 channel, 2 bytes/16 bits, one rate 16000, `ep=0x86` ISO IN, `mps=36`, `bInterval=1`, sampling-frequency control present |
| microphone, 10 s | `uac_mic` / `stm32f429-uac-mic` | **2/2**: 10,000 packets, 319,994 / 319,992 bytes, `gaps=0`, `err=0`, non-zero samples 159,997 / 159,988 |
| video + microphone, 20 s | `uac_av` / `stm32f429-uac-av` | **2/2**: video 297 / 297 frames, JPEG SOI/EOI 297/297, 0 packet/URB errors, 1 frame dropped; audio 20,003 / 20,004 packets, 640,080 / 640,114 bytes, `gaps=0`, `err=0`, overruns 0 |

The microphone image carries the UVC class (and its descriptor census) even
though the window measures audio only: this camera's audio function returns
zero-length packets until its video function has been claimed.  The two
isochronous endpoints share the DWC2 host's 12 channels; the video stream is
not degraded by the microphone.  Details, descriptor bytes, the engine's
contract and the reproduce commands are in `docs/F429-UAC.md`.
