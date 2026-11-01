# Wireless bench: making the access point trustworthy

Any wireless throughput number is only as good as the access point under it.
In one lab session the bench looked healthy for twenty-second cells and then
dropped the station every one to three minutes: the AP station table went empty,
the client got "No route to host", and every number measured in that window was
worthless. This document describes how to prove the bench *before* taking
numbers, and which card may be the AP.

Two scripts implement it:

- `tools/bench/stability.sh` - send a fixed-rate stream for minutes and decide
  PASS/FAIL on the AP own counters.
- `tools/bench/channel-busy.sh` - measure how much airtime other networks use on
  each candidate channel, so "the channel is bad" can be ruled out with numbers
  instead of with a guess.

Both run on the AP host. `stability.sh` also needs a build of
`tools/netio/ppsgen.c` (see `tools/netio/build.sh`).

## 1. Role assignment

| card | bus | role | why |
|---|---|---|---|
| AR9271 (`ath9k_htc`) | USB | **AP** | host mac80211 AP, per-station `tx failed/retries`, 1-9 ms RTT, holds a link for minutes |
| MT7921AU (`mt7921u`) | USB | **monitor** | monitor on a separate phy from the AP; also capable of AP, but see below |
| BCM43455 (`brcmfmac`, SDIO) | SDIO | **retired from AP duty** | firmware-offload AP; dropped the station every 1-3 minutes |

Rules that came out of the measurements:

- **The AP and the monitor must be different phys.** A card cannot capture its
  own transmissions, so a single-card bench can either transmit or observe.
- **Use the AR9271 as the AP, and keep the monitor on the MTK card.** The
  AR9271 in AP mode held 400 pps for four minutes with `tx failed 0`,
  `tx retries 0` and zero loss; the same station on the brcmfmac AP dropped
  every one to three minutes.
- **Do not use the MT7921AU as the AP for a linuxko station.** It works for the
  Linux reference, but it has a 100-800 ms downlink latency (measured on the
  air: the request and the reply were 719 ms apart, and the delay is on the host
  side, not in the air), and a linuxko station on it collapsed to about 600
  packets in 20 s in both legacy and HT mode. Disabling USB runtime PM
  (`power/control=on` on the device) took the latency from about 700 ms to about
  100 ms, which is better but still not a bench.
- **The brcmfmac SDIO card is fine as a station, not as an AP.** It also has no
  monitor mode, which is why retiring it costs nothing.

A 5 GHz AR card would extend the same method to the 5 GHz band; nothing in the
scripts is band specific (pass the right `--freq`).

## 2. Procedure

Run it in this order, before any tuning and before any number that goes into a
report.

### 2.1 Identity and channel

```sh
sudo iw dev <ap> info          # ssid, channel, txpower, addr
lsusb -t                       # which card is on which bus, and the speed
```

Then check that the channel is actually quiet:

```sh
bash tools/bench/channel-busy.sh \
    --mon-if <mon> --secs 5 --channels "1 3 6 9 11 13"
```

The tool reports `busy_pct` per channel: the sum of `wlan_radio.duration` over
the capture window, which is the fraction of airtime somebody was transmitting.
In the lab all six 2.4 GHz channels measured 1.5-2.3% busy, so the channel was
never the cause of the instability - moving channel would have hidden the real
fault. Treat "the channel is bad" as a hypothesis to be measured, not as an
explanation.

### 2.2 Stability run

```sh
bash tools/bench/stability.sh \
    --ap-if wlan2 --sta-ip 192.168.99.2 \
    --pps 400 --secs 240 --out ./bench-stability.json
```

It sends a fixed-rate UDP stream to a closed port on the DUT (the DUT kernel
counts the frames in `Udp:NoPorts`, so no server is needed), samples the AP
per-station counters once a second, and decides on four criteria:

| check | pass condition | what it catches |
|---|---|---|
| `link_never_dropped` | the station is present in `iw station dump` at every sample | association flapping, the AP losing the peer |
| `ap_transmitted_enough` | AP `tx packets` >= 99.5% of what the sender handed over | frames dropped before the air |
| `no_tx_fail_growth` | `tx failed` delta = 0 | the driver or firmware failing to send |
| `signal_stable` | signal spread <= 10 dB over the run | antenna, power, or a card resetting |

Exit code 0 means PASS; the JSON is the record that a number measured right
after it is worth something. Forty-five seconds is enough for a smoke check;
four minutes is what the lab uses before a measurement campaign.

### 2.3 What a pass looks like

From the lab, AR9271 AP, 8188EU station, channel 5, 400 pps:

```
sent 24011, ap_tx 24016, loss 0 ppm, tx failed 0, tx retries 0,
station present at every sample, signal spread 2 dB  -> PASS
```

For comparison, the same station on the brcmfmac AP disappeared from
`station dump` within the first minutes while hostapd logged repeated
`AP-STA-DISCONNECTED` / `associated` lines - a FAIL on the first criterion with
every other counter still looking healthy, which is exactly the failure this
test exists to catch.

## 3. Traps this method encodes

- **dumpcap may write a zero-byte capture** when the target directory is not
  allowed by its AppArmor profile (temporary filesystems are a common case).
  Capture into a root-owned directory on persistent storage;
  `channel-busy.sh` refuses a `CAPDIR` on a temporary filesystem for this
  reason.
- **`iw dev <mon> info` does not print the channel** on some drivers; after
  tuning, verify with the radiotap field of a short capture instead of trusting
  `info`.
- **A monitor keeps its last channel.** Re-tune before every capture, or an
  empty pcap will be silently attributed to "no traffic".
- **`iw dev <ap> station dump` reports the last rate used**, so an idle station
  shows a management rate (1 Mbit/s) even though the link is fine. Judge the
  link under traffic, not while idle.
- **A duplicate AP address on another interface silently blackholes replies**:
  after moving the AP role between cards, an old `192.168.99.1/24` left on the
  monitor interface made the kernel route replies out of the monitor. Check
  `ip route get <dut>` after every role swap.
- **Retry counters are not uniform**: one driver reported `tx retries 0` while
  the air showed about 25% retried frames. Use the air (`wlan.fc.retry`) when
  the retry count matters, and use the AP counter only as a "did it fail" flag.

## 4. Where this sits in the method

This is the environment gate in front of everything else: an unproven bench
makes every later column - airtime, retries, gap, throughput - unattributable.
It pairs with `docs/NET-TESTING.md` for the wired port (same "prove the bench
first" idea).
