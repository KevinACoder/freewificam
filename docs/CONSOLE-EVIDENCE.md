# Bring-up evidence: what the console prints

A role is "brought up" here when one cold boot reaches the line's own end-to-end
anchor - association and an address for a station, a beacon and an authorised
peer for an access point, a non-zero receive count for a monitor - and says so.
The transcripts below are the board's UART output, verbatim, from boots that
reached those anchors.  Elisions are marked `...` and name what was cut; nothing
else is edited, including the timestamps, which are the image's own milliseconds
since boot.

Why the console and not a test report: the interesting failures on this project
have all been silent (a frame path that never runs, an interrupt that is never
re-armed, a handshake that stalls with no error), so the log is the artefact
that carries the evidence.  What each transcript proves is stated above it, in
the order the lines appear; what it does not prove is at the bottom of the page.

Reproduce: the profile is named per section (`PROFILE=<name>`); the image is
`tests/board/rtos/build/build/rtos-freertos.bin`, loaded over TFTP and entered
with `go 0xa000000`; the console is the board's debug UART at 115200 8N1.  The
host-side driver for each role is in `tools/rtos/`.

## Intel AC 7260 (PCIe, mac80211 + iwlwifi)

Card: Intel Dual Band Wireless-AC 7260, `8086:08b1`, MAC
`6c:29:95:12:f0:d9`, firmware `17.bfb58538.0` (`7260-17.ucode`), `iwlmvm`.  The
stack above it is upstream `net/mac80211` + `net/wireless`, unmodified; the
linuxkpi and PCIe layers are this tree's.  Every profile runs the same cases in
the same order (`tick`, `pcie`, then the Wi-Fi role), so two transcripts differ
only after `pcie: PASS`.

Three benches appear below, and they are not interchangeable:

| role | board is | peer is | channel |
|---|---|---|---|
| STA | station `wlan0` | Kali Pi 4B, RTL8812AU `wlan2` running hostapd, `labwifi-ar-ht20s` | ch5 / 2432 MHz, HT20 |
| AP | access point `wl1` running hostapd | Kali Pi 4B, MT7921AU `wlan1` running `wpa_supplicant` | ch11 / 2462 MHz, HT20 |
| monitor | receive-only monitor vif | the STA bench's AP and its traffic | ch5 / 2432 MHz |

One artifact is visible in the transcripts and is left as printed: where a
mainline kernel prints a MAC, the port prints `000000000A656498M`, because its
`printf` has no `%pM`.  The MACs that matter on air are the ones the case prints
itself (`bssid=`, `mac=`, `RX AssocResp`).

### STA, open AP - `rk3568-iwl7260-c5`

The AP is open.  What the transcript proves, in order: the image boots and hands
over to the runner; PCIe comes up and iwlwifi claims `8086:08b1` by identity and
loads the firmware; the MSI vector is live and `CSR_INT` moves from
`0x08000000` (the firmware's own start-of-day interrupt) to `0x80000001`, the
driver's first receive, and back to `0x00000000` with the mask cleared;
`wlan0` exists with the card's MAC; the scan returns the bench BSS on 2432 MHz
at -40 dBm; authenticate and associate complete within 300 ms of `connect`;
`PASS C5`; an address is bound; 20 of 20 pings come back at both payload sizes
with `tx_fail=0`; and the run reports `cases=3/3`.


```
## Starting application at 0x0A000000 ...
boot: el2 entry
boot: dcache flushed
boot: el2 mmu off
boot: ic iallu
boot: eret to el1
boot: el1
30d00800 boot: sctlr_el1 above
boot: pre-bss
boot: post-bss
boot: mmu_enable
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=bc8913c-dirty
rtos: kernel ready tick=1000Hz
gicv3: rd frame off=00000000
gicv3: ready ctlr=00000012 waker=00000000
tick: source=cntv irq=27 freq=1000Hz
[   0.000] rtos: runner start cases=3 build=bc8913c-dirty
```

... (the `tick` case, then PCIe enumeration and the NVMe/MSI walk) ...

```
[   3.163] rtos: case iwl7260_c5 start
[   3.166] iwl7260_c5: core ready
[   3.169] iwl7260_c5: fw registered=0
[   3.173] iwl7260_c5: initcalls run
[   3.176] its: 03:00.0 got 1 message irq(s) (event 0 -> intid 8193)
[   3.182] msi: 8086:08b1 rid 0300, 1 vector(s), addr 0xfd450040 data 0
[   3.188] pcie: coherent pool 2048 KiB (non-cacheable)
[   3.194] iwl7260_c5: drv pcie: 'iwlwifi' claimed 8086:08b1
[   3.201] iwl7260_c5: firmware 03:00.0: loaded firmware version 17.bfb58538.0 7260-17.ucode op_mode iwlmvm
[   3.210] iwl7260_c5: pci devs=2
[   3.225] pcie: irq 8193 bar0=0xf0300000 csr int=0x08000000 mask=0x08000000 fh=0x00000001 reset=0x00000001 gp=0x080403c5
[   3.242] [previous line repeated 2 times]
[   3.245] pcie: irq 8193 bar0=0xf0300000 csr int=0x80000001 mask=0xba00008f fh=0x40010000 reset=0x00000000 gp=0x080403c5
[   3.257] pcie: irq 8193 bar0=0xf0300000 csr int=0x00000000 mask=0xba00008f fh=0x00000000 reset=0x00000000 gp=0x080403cd
[   3.267] pcie: irq 8193 bar0=0xf0300000 csr int=0x00000000 mask=0x00000000 fh=0x00000000 reset=0x00000000 gp=0x080403cd
[   3.559] iwl7260_c5: wlan0 present=1 ifindex=1 mac=6c:29:95:12:f0:d9
[   3.821] iwl7260_c5: start rc=0
[   3.825] iwl7260_c5: scan ssid=labwifi-ar-ht20s freq=2432 rc=0
[   3.830] iwl7260_c5: bss ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 freq=2432 signal=-40
[   3.840] iwl7260_c5: drv wlan0: authenticate with 000000000A656498M (local address=000000000A65863CM)
[   3.849] iwl7260_c5: drv wlan0: send auth to 000000000A656498M (try 1/3)
[   3.856] iwl7260_c5: connect ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 freq=2432 rc=0
[   3.867] iwl7260_c5: drv wlan0: authenticated
[   3.955] iwl7260_c5: drv wlan0: associate with 000000000A677468M (try 1/3)
[   3.968] iwl7260_c5: drv wlan0: RX AssocResp from 000000000A677468M (capab=0x401 status=0 aid=1)
[   3.978] iwl7260_c5: drv wlan0: associated
[   3.986] iwl7260_c5: connected bssid=1c:a7:70:de:29:a1 waited_ms=100
[   3.992] iwl7260_c5: PASS C5
[   4.195] iwl7260_c5: netif up name=wl0 ip=192.168.99.2/24 mac=6c:29:95:12:f0:d9
[   4.202] iwl7260_c5: peer window ms=6000
[  10.919] iwl7260_c5: ping tx=20 rx=20 payload=56 rtt_ms=2/3/15
[  11.635] iwl7260_c5: ping tx=20 rx=20 payload=1400 rtt_ms=2/4/16
[  11.641] iwl7260_c5: diag rx_frames=43 tx_frames=43 rx_bytes=30942 tx_bytes=30942 tx_fail=0
[  11.649] iwl7260_c5: PASS C6
[  11.652] iwl7260_c5: PASS
[  11.655] rtos: case iwl7260_c5 PASS
[  11.658] rtos: result pass cases=3/3 backend=freertos
[  11.663] rtos: resetting board
```

`[previous line repeated 2 times]` is the logger's own elision, not this page's:
the port repeats that `irq 8193 ...` line four times as the firmware comes out of
reset and prints it once with a count.

The four `pcie: irq 8193 bar0=...` lines are the one place the interrupt state is
read out: `csr int` is the pending word, `mask` the port's copy of
`CSR_INT_MASK`.  The second line is the receive that proves the RX path is
armed - `int=0x80000001` with `mask=0xba00008f` - and the third is the same
interrupt acknowledged (`int=0`).  When this pair does not appear, no frame will
arrive no matter what the join says.

### STA, WPA2, and the throughput phases - `rk3568-iwl7260-sta-wpa2`

Same bench and channel; the AP now runs WPA2-PSK/CCMP, and the profile adds the
`iwl7260_c6` traffic case after the join.  The NXP `wpa_supplicant` port lives in
the image, so the transcript interleaves the supplicant's own output and the
port's `lkpi:` seam with the case's lines.  Both are left in: they are the
handshake.

What it proves, in order: the interface is up before the supplicant starts
(`netif up ... 192.168.99.2`); the supplicant's sweep of `del_key` over six key
indices returns `-67` (`ENOTSUPP`) on an unassociated interface, which is
expected and not an error path; the scan, authenticate and associate complete;
`set_key` installs the pairwise CCMP key (`keyidx=0`) and the group key
(`keyidx=1`), both `rc=0`; the supplicant prints `Key negotiation completed ...
[PTK=CCMP GTK=CCMP]`; the case reads back `state=9`, `eapol tx=2 rx=2`,
`ptk=1 gtk=1 fail=0`; an address is bound; 20 of 20 pings come back at both
payload sizes; `PASS STAW`.


```
## Starting application at 0x0A000000 ...
boot: el2 entry
boot: dcache flushed
boot: el2 mmu off
boot: ic iallu
boot: eret to el1
boot: el1
30d00800 boot: sctlr_el1 above
boot: pre-bss
boot: post-bss
boot: mmu_enable
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=8d3d8fc-dirty
rtos: kernel ready tick=1000Hz
gicv3: rd frame off=00000000
gicv3: ready ctlr=00000012 waker=00000000
tick: source=cntv irq=27 freq=1000Hz
[   0.000] rtos: runner start cases=4 build=8d3d8fc-dirty
```

... (the `tick` and `pcie` cases, and the PCIe/MSI walk) ...

```
[   3.163] rtos: case iwl7260_sta_wpa2 start
[   3.167] iwl7260_sta_wpa2: core ready
[   3.170] iwl7260_sta_wpa2: fw registered=0
[   3.174] iwl7260_sta_wpa2: initcalls run
[   3.178] its: 03:00.0 got 1 message irq(s) (event 0 -> intid 8193)
[   3.184] msi: 8086:08b1 rid 0300, 1 vector(s), addr 0xfd450040 data 0
[   3.191] pcie: coherent pool 2048 KiB (non-cacheable)
[   3.197] iwl7260_sta_wpa2: pci devs=2
[   3.201] iwl7260_sta_wpa2: firmware 03:00.0: loaded firmware version 17.bfb58538.0 7260-17.ucode op_mode iwlmvm
[   3.223] pcie: irq 8193 bar0=0xf0300000 csr int=0x08000000 mask=0x08000000 fh=0x00000001 reset=0x00000001 gp=0x080403c5
[   3.239] [previous line repeated 2 times]
[   3.243] pcie: irq 8193 bar0=0xf0300000 csr int=0x80000001 mask=0xba00008f fh=0x40010000 reset=0x00000000 gp=0x080403c5
[   3.255] pcie: irq 8193 bar0=0xf0300000 csr int=0x00000000 mask=0xba00008f fh=0x00000000 reset=0x00000000 gp=0x080403cd
[   3.265] pcie: irq 8193 bar0=0xf0300000 csr int=0x00000000 mask=0x00000000 fh=0x00000000 reset=0x00000000 gp=0x080403cd
[   3.557] iwl7260_sta_wpa2: wlan0 present=1 ifindex=1 mac=6c:29:95:12:f0:d9
[   3.817] iwl7260_sta_wpa2: start rc=0
[   3.820] iwl7260_sta_wpa2: netif up name=wl0 ip=192.168.99.2/255.255.255.0 mac=6c:29:95:12:f0:d9
[   3.829] iwl7260_sta_wpa2: security wpa2=1 ssid=labwifi-ar-ht20s
supplicant_main_task: 634 Starting wpa_supplicant thread with debug level: 3


Successfully initialized wpa_supplicant

iface_cb: iface wl1 ifindex 2 6c:29:95:12:f0:d9

Using interface wl1


Initializing interface 0: wl1


lkpi: l2_packet ifname=wl1 mac=6c:29:95:12:f0:d9
lkpi: del_key keyidx=0 rc=-67
lkpi: del_key keyidx=1 rc=-67
lkpi: del_key keyidx=2 rc=-67
lkpi: del_key keyidx=3 rc=-67
lkpi: del_key keyidx=4 rc=-67
lkpi: del_key keyidx=5 rc=-67
lkpi: sta iface=wl1
lkpi: sta network ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1
[   3.960] iwl7260_sta_wpa2: sta start rc=0
lkpi: sta scan ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 freq=2432 signal=-40
wl1: Trying to associate with 1c:a7:70:de:29:a1 (SSID='labwifi-ar-ht20s' freq=2432 MHz)

[   4.069] iwl7260_sta_wpa2: drv wlan0: authenticate with 000000000A8ED7F8M (local address=000000000A69E63CM)
[   4.079] iwl7260_sta_wpa2: drv wlan0: send auth to 000000000A8ED7F8M (try 1/3)
[   4.089] iwl7260_sta_wpa2: drv wlan0: authenticated
[   4.102] iwl7260_sta_wpa2: drv wlan0: associate with 000000000A6DBD08M (try 1/3)
[   4.114] iwl7260_sta_wpa2: drv wlan0: RX AssocResp from 000000000A6DBD08M (capab=0x411 status=0 aid=1)
[   4.125] iwl7260_sta_wpa2: drv wlan0: associated
lkpi: sta assoc bssid=1c:a7:70:de:29:a1 freq=2432
wl1: Associated with 1c:a7:70:de:29:a1

lkpi: set_key keyidx=0 cipher=0xfac04 pairwise=1 default=0 rc=0 mac=1c:a7:70:de:29:a1
lkpi: set_key keyidx=1 cipher=0xfac04 pairwise=0 default=0 rc=0 mac=ff:ff:ff:ff:ff:ff
wl1: WPA: Key negotiation completed with 1c:a7:70:de:29:a1 [PTK=CCMP GTK=CCMP]

wl1: CTRL-EVENT-CONNECTED - Connection to 1c:a7:70:de:29:a1 completed [id=0 id_str=]

[   4.309] iwl7260_sta_wpa2: sta state=9 waited_ms=300 bssid=1c:a7:70:de:29:a1
[   4.316] iwl7260_sta_wpa2: sta eapol tx=2 rx=2 drop=0
[   4.321] iwl7260_sta_wpa2: sta key ptk=1 gtk=1 fail=0
[   4.326] iwl7260_sta_wpa2: mcast allmulti=1
[   4.330] iwl7260_sta_wpa2: static ip=192.168.99.2 mask=255.255.255.0 gw=192.168.99.1
[   4.868] iwl7260_sta_wpa2: peer ip=192.168.99.1
[   5.592] iwl7260_sta_wpa2: ping tx=20 rx=20 payload=56 rtt_ms=2/3/15
[   6.339] iwl7260_sta_wpa2: ping tx=20 rx=20 payload=1400 rtt_ms=2/5/17
[   6.345] iwl7260_sta_wpa2: diag rx_frames=49 tx_frames=49 rx_bytes=31286 tx_bytes=31534 tx_fail=0
[   6.354] iwl7260_sta_wpa2: peer window ms=5000
[  11.361] iwl7260_sta_wpa2: PASS STAW
[  11.364] rtos: case iwl7260_sta_wpa2 PASS
```

Then the traffic case, on the same boot and over the same link.  `phase 1` is a
loopback inside the board's own stack - a wiring check, not a measurement - and
the six phases after it are the ones the README table quotes: `2` and `4` are
the two TCP downlink repeats, `3` and `5` the UDP downlink repeats, `6` the TCP
uplink and `7` the UDP uplink.  Each `report` line is the stack's own byte count
over the phase's window; the `census` line after it is the port's counter set;
the `txhealth` line is the driver-side transmit ledger.


```
[  11.368] rtos: case iwl7260_c6 start
[  11.371] iwl7260_c6: plan=throughput bssid=1c:a7:70:de:29:a1 dev=wlan0
[  11.378] iwl7260_c6: ready netif=1 rx_frames=69 tx_frames=69
[  11.383] iwl7260_c6: server tcp=5001 udp=5002 ok=1
... (the loopback phase) ...
[  21.450] iwl7260_c6: phase 2 begin proto=tcp role=server port=5001 window_ms=30000 name=tcpdn
[  50.589] iwl7260_c6: phase 2 report bytes=148635672 ms=28134 kbit=42264 pkts=0
... (the per-phase census and txhealth lines) ...
[  50.635] iwl7260_c6: phase 2 done ok=1
[  50.639] iwl7260_c6: phase 3 begin proto=udp role=server port=5002 window_ms=20000 name=udpdn
[  70.662] iwl7260_c6: phase 3 report bytes=94572450 ms=20000 kbit=37828 pkts=64335
... (the per-phase census and txhealth lines) ...
[  70.708] iwl7260_c6: phase 3 done ok=1
[  70.712] iwl7260_c6: phase 4 begin proto=tcp role=server port=5001 window_ms=30000 name=tcpdn2
[  99.901] iwl7260_c6: phase 4 report bytes=146669592 ms=28105 kbit=41744 pkts=0
... (the per-phase census and txhealth lines) ...
[  99.947] iwl7260_c6: phase 4 done ok=1
[  99.951] iwl7260_c6: phase 5 begin proto=udp role=server port=5002 window_ms=20000 name=udpdn2
[ 119.974] iwl7260_c6: phase 5 report bytes=94569510 ms=20000 kbit=37827 pkts=64333
... (the per-phase census and txhealth lines) ...
[ 120.021] iwl7260_c6: phase 5 done ok=1
[ 120.025] iwl7260_c6: phase 6 begin proto=tcp role=client port=5001 window_ms=12000 name=tcpup
[ 130.091] iwl7260_c6: phase 6 report bytes=31012614 ms=10000 kbit=24808 pkts=0
... (the per-phase census and txhealth lines) ...
[ 130.137] iwl7260_c6: phase 6 done ok=1
[ 130.141] iwl7260_c6: phase 7 begin proto=udp role=client port=5001 window_ms=20000 name=udpup
[ 150.159] iwl7260_c6: phase 7 report bytes=102017832 ms=20000 kbit=40807 pkts=67117
... (the per-phase census and txhealth lines) ...
[ 150.206] iwl7260_c6: phase 7 done ok=1
[ 150.210] iwl7260_c6: PASS C6T
[ 150.213] rtos: case iwl7260_c6 PASS
[ 150.216] rtos: result pass cases=4/4 backend=freertos
```

The uplink census is worth reading once in full, because it is the shape behind
the README's uplink cell: on phase 7 the stack handed the port 8647 segments
while `tx_fail=125586` refusals were counted and the pool sat at its ceiling
(`skb_used=128 skb_high=128 data_high=128`).  Those refusals are back-pressure
into lwIP, not loss on the air.

### AP, open - `rk3568-iwl7260-ap-a4`

The roles are swapped: the board is the access point and the MLME is the ported
NXP hostapd.  What the transcript proves, in order: the card's own channel table
is consulted before anything is programmed (`chan freq=2462 present=1
flags=0x00c00190 can_beacon=1`); the interface is converted to AP
(`set_iface rc=0 type=3`) and started; an address is bound; hostapd reaches
`ENABLED` and prints its own `AP-ENABLED`; the Pi's MT7921AU authenticates,
`sta_add` lands with `aid=1`, the station associates and `AP-STA-CONNECTED` is
printed; both ping payloads are 20/20 with 1 ms round-trips; `PASS AP`.


```
## Starting application at 0x0A000000 ...
boot: el2 entry
boot: dcache flushed
boot: el2 mmu off
boot: ic iallu
boot: eret to el1
boot: el1
30d00800 boot: sctlr_el1 above
boot: pre-bss
boot: post-bss
boot: mmu_enable
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=353badc-dirty
rtos: kernel ready tick=1000Hz
gicv3: rd frame off=00000000
gicv3: ready ctlr=00000012 waker=00000000
tick: source=cntv irq=27 freq=1000Hz
[   0.000] rtos: runner start cases=3 build=353badc-dirty
```

... (the `tick` case, then PCIe enumeration and the NVMe/MSI walk) ...

```
[   3.163] rtos: case iwl7260_ap start
[   3.166] iwl7260_ap: core ready
[   3.169] iwl7260_ap: fw registered=0
[   3.173] iwl7260_ap: drv pcie: driver 'iwlwifi' registered
[   3.178] iwl7260_ap: initcalls run
[   3.181] its: 03:00.0 got 1 message irq(s) (event 0 -> intid 8193)
[   3.188] msi: 8086:08b1 rid 0300, 1 vector(s), addr 0xfd450040 data 0
[   3.194] pcie: coherent pool 2048 KiB (non-cacheable)
[   3.200] iwl7260_ap: drv pcie: 'iwlwifi' claimed 8086:08b1
[   3.206] iwl7260_ap: firmware 03:00.0: loaded firmware version 17.bfb58538.0 7260-17.ucode op_mode iwlmvm
[   3.215] iwl7260_ap: pci devs=2
[   3.230] pcie: irq 8193 bar0=0xf0300000 csr int=0x08000000 mask=0x08000000 fh=0x00000001 reset=0x00000001 gp=0x080403c5
[   3.247] [previous line repeated 2 times]
[   3.251] pcie: irq 8193 bar0=0xf0300000 csr int=0x80000001 mask=0xba00008f fh=0x40010000 reset=0x00000000 gp=0x080403c5
[   3.262] pcie: irq 8193 bar0=0xf0300000 csr int=0x00000000 mask=0xba00008f fh=0x00000000 reset=0x00000000 gp=0x080403cd
[   3.273] pcie: irq 8193 bar0=0xf0300000 csr int=0x00000000 mask=0x00000000 fh=0x00000000 reset=0x00000000 gp=0x080403cd
[   3.665] iwl7260_ap: wlan0 present=1 ifindex=1 mac=6c:29:95:12:f0:d9 iftype=2
[   3.672] iwl7260_ap: chan freq=2462 present=1 flags=0x00c00190 can_beacon=1
[   3.679] iwl7260_ap: set_iface rc=0 type=3
[   3.934] iwl7260_ap: start rc=0
[   3.937] iwl7260_ap: netif up name=wl0 ip=192.168.99.1/24 mac=6c:29:95:12:f0:d9
[   3.945] iwl7260_ap: security wpa2=0 ssid=rtos-7260-ap
supplicant_main_task: 634 Starting wpa_supplicant thread with debug level: 3


Successfully initialized wpa_supplicant

lkpi: hostapd daemon ready
wl1: interface state UNINITIALIZED->COUNTRY_UPDATE

lkpi: set_ap head=63 tail=87 freq=2462 rc=0
wl1: interface state COUNTRY_UPDATE->ENABLED

: AP-ENABLED 

[   8.981] iwl7260_ap: hostap start rc=0 ready=1
[   8.985] iwl7260_ap: peer window ms=12000
wl1: STA 90:de:80:4c:38:c5 IEEE 802.11: authenticated

lkpi: sta add params=000000000A991030 size=160 aid=1 cap=1057 li=5 rl=12 fl=0x66 set=0
lkpi: words 0a702bb0 00000000 04210001 00000000 0a702bdc 00000000
lkpi: sta add 90:de:80:4c:38:c5 rc=0 aid=1 set=0 fl=0x66
wl1: STA 90:de:80:4c:38:c5 IEEE 802.11: associated (aid 1)

: AP-STA-CONNECTED 90:de:80:4c:38:c5

[  15.513] iwl7260_ap: hostap up ready=1 sta=1
lkpi: sta authorized 90:de:80:4c:38:c5 1 rc=0
[  15.521] iwl7260_ap: sta add n=1 mac=90:de:80:4c:38:c5
[  19.228] iwl7260_ap: ping tx=20 rx=20 payload=56 rtt_ms=1/1/5
[  19.934] iwl7260_ap: ping tx=20 rx=20 payload=1400 rtt_ms=1/1/4
[  19.940] iwl7260_ap: hostap stats ready=1 sta=1
[  19.944] iwl7260_ap: PASS AP
```

Then the seven traffic phases.  `phase 1` is the loopback check again.  In this
role the downlink phases are the board *sending* (`air=down`) and the Pi
receiving, so the number that counts is the Pi's `iperf` client report; the
board's own `report` line is what it handed to the driver, and
`tools/rtos/ap-report.py` reconciles the two.  The uplink phases (`air=up`)
reverse that.


```
[  19.947] iwl7260_ap: server tcp=5001 udp=5002 ok=1
... (the loopback phase) ...
[  30.061] iwl7260_ap: phase 2 begin proto=tcp role=client air=down port=5001 window_ms=12000 name=tcpdn peer_ms=1500
[  41.640] iwl7260_ap: phase 2 report bytes=91872714 ms=10008 kbit=73432 pkts=0
... (the per-phase census, irq, workqueue and txhealth lines) ...
[  41.730] iwl7260_ap: phase 2 done ok=1
[  41.734] iwl7260_ap: phase 3 begin proto=udp role=client air=down port=5002 window_ms=20000 name=udpdn peer_ms=1500
[  63.273] iwl7260_ap: phase 3 report bytes=93904072 ms=20001 kbit=37559 pkts=61779
... (the per-phase census, irq, workqueue and txhealth lines) ...
[  63.364] iwl7260_ap: phase 3 done ok=1
[  63.368] iwl7260_ap: phase 4 begin proto=tcp role=client air=down port=5001 window_ms=12000 name=tcpdn2 peer_ms=1500
[  74.947] iwl7260_ap: phase 4 report bytes=92780834 ms=10000 kbit=74224 pkts=0
... (the per-phase census, irq, workqueue and txhealth lines) ...
[  75.038] iwl7260_ap: phase 4 done ok=1
[  75.042] iwl7260_ap: phase 5 begin proto=udp role=client air=down port=5002 window_ms=20000 name=udpdn2 peer_ms=1500
[  96.589] iwl7260_ap: phase 5 report bytes=93904072 ms=20004 kbit=37554 pkts=61779
... (the per-phase census, irq, workqueue and txhealth lines) ...
[  96.680] iwl7260_ap: phase 5 done ok=1
[  96.684] iwl7260_ap: phase 6 begin proto=tcp role=server air=up port=5001 window_ms=30000 name=tcpup peer_ms=1500
[ 125.576] iwl7260_ap: phase 6 report bytes=155058200 ms=28072 kbit=44184 pkts=0
... (the per-phase census, irq, workqueue and txhealth lines) ...
[ 125.667] iwl7260_ap: phase 6 done ok=1
[ 125.671] iwl7260_ap: phase 7 begin proto=udp role=server air=up port=5002 window_ms=20000 name=udpup peer_ms=1500
[ 147.198] iwl7260_ap: phase 7 report bytes=92252790 ms=20000 kbit=36901 pkts=62757
... (the per-phase census, irq, workqueue and txhealth lines) ...
[ 147.289] iwl7260_ap: phase 7 done ok=1
[ 147.293] iwl7260_ap: PASS APT
[ 147.296] rtos: case iwl7260_ap PASS
[ 147.299] rtos: result pass cases=3/3 backend=freertos
```

What the transcript does *not* show is the NAPI stall this profile fixed: it is
an absence (no repeating `CSR_INT` line, a pool that comes back to rest), and it
is diagnosed in `docs/decisions/2026-11-12-ap-a4-napi-budget.md`.  The
`[previous line repeated N times]` markers are again the logger's, not this
page's.

### AP, WPA2 - `rk3568-iwl7260-ap-wpa2`

Same AP role with `CONFIG_AP_WPA2=y` and a passphrase supplied by the case.
What it proves, in order: the AP is set up with `set_ap ... freq=2462 rc=0`; the
group key is installed before any station arrives (`keyidx=1 ...
default=1 rc=0`); the station authenticates and associates; the pairwise key is
installed (`keyidx=0 ... pairwise=1 rc=0`) and hostapd prints
`AP-STA-CONNECTED`; the supplicant side of hostapd reports
`pairwise key handshake completed (RSN)` and `EAPOL-4WAY-HS-COMPLETED`; the case
reads back `ptk=1 gtk=2` and `eapol tx=2 rx=2 drop=0`; both ping payloads are
20/20; `PASS APW`.  This is the profile where the software `ccm(aes)` provider
earns its keep: there is no AES-in-hardware path here, and the handshake is done
in software.


```
## Starting application at 0x0A000000 ...
boot: el2 entry
boot: dcache flushed
boot: el2 mmu off
boot: ic iallu
boot: eret to el1
boot: el1
30d00800 boot: sctlr_el1 above
boot: pre-bss
boot: post-bss
boot: mmu_enable
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=b411eae-dirty
rtos: kernel ready tick=1000Hz
gicv3: rd frame off=00000000
gicv3: ready ctlr=00000012 waker=00000000
tick: source=cntv irq=27 freq=1000Hz
[   0.000] rtos: runner start cases=3 build=b411eae-dirty
```

... (the `tick` case, then PCIe enumeration and the NVMe/MSI walk) ...

```
lkpi: hostapd daemon ready
wl1: interface state UNINITIALIZED->COUNTRY_UPDATE

lkpi: set_ap head=63 tail=109 freq=2462 rc=0
lkpi: set_key keyidx=1 cipher=0xfac04 pairwise=0 default=1 rc=0 mac=ff:ff:ff:ff:ff:ff
wl1: interface state COUNTRY_UPDATE->ENABLED

: AP-ENABLED 

[   9.232] iwl7260_ap: hostap start rc=0 ready=1
[   9.237] iwl7260_ap: peer window ms=12000
wl1: STA 90:de:80:4c:38:c5 IEEE 802.11: authenticated

lkpi: sta add params=000000000A991030 size=160 aid=1 cap=1073 li=5 rl=12 fl=0x66 set=0
lkpi: words 0a70fad0 00000000 04310001 00000000 0a70fafc 00000000
lkpi: sta add 90:de:80:4c:38:c5 rc=0 aid=1 set=0 fl=0x66
[  23.154] iwl7260_ap: hostap up ready=1 sta=1
[  23.158] iwl7260_ap: sta add n=1 mac=90:de:80:4c:38:c5
wl1: STA 90:de:80:4c:38:c5 IEEE 802.11: associated (aid 1)

lkpi: del_key keyidx=0 rc=-2
lkpi: sta authorized 90:de:80:4c:38:c5 0 rc=0
lkpi: del_key keyidx=0 rc=-2
lkpi: del_key keyidx=0 rc=-2
lkpi: sta authorized 90:de:80:4c:38:c5 0 rc=0
lkpi: set_key keyidx=1 cipher=0xfac04 pairwise=0 default=1 rc=0 mac=ff:ff:ff:ff:ff:ff
lkpi: set_key keyidx=0 cipher=0xfac04 pairwise=1 default=0 rc=0 mac=90:de:80:4c:38:c5
: AP-STA-CONNECTED 90:de:80:4c:38:c5

[  23.230] iwl7260_ap: wpa2 station sta=1 ptk=1 gtk=2 fail=0 wait_ms=20
lkpi: sta authorized 90:de:80:4c:38:c5 1 rc=0
wl1: STA 90:de:80:4c:38:c5 WPA: pairwise key handshake completed (RSN)

: EAPOL-4WAY-HS-COMPLETED 90:de:80:4c:38:c5

[  36.943] iwl7260_ap: ping tx=20 rx=20 payload=56 rtt_ms=2/1/6
[  37.648] iwl7260_ap: ping tx=20 rx=20 payload=1400 rtt_ms=1/1/5
[  37.654] iwl7260_ap: hostap stats ready=1 sta=1
[  37.659] iwl7260_ap: eapol tx=2 rx=2 drop=0
[  37.663] iwl7260_ap: PASS APW
[  37.666] rtos: case iwl7260_ap PASS
[  37.669] rtos: result pass cases=3/3 backend=freertos
```

### Monitor, receive only - `rk3568-iwl7260-mon`

The card becomes a user monitor interface and the image's own sink counts what
arrives.  What it proves, in order: the interface is converted with
`set_iface rc=0 type=6`, `dev_open` returns `netif_running=1 devtype=803`, and
the channel is set with `set_freq rc=0 freq=2432`; the sink reports every second
for 15 s while the bench AP beacons; at the end it prints the census by frame
type (`beacons=148 probes=51 probe_resp=40 auth=7 assoc=6 data=37 ctrl=39
other=9`), the radiotap `it_len` set (`56/64`, the same set the GT arm produced),
the per-channel histogram (**292 of 297 frames on 2432**; the five on 2412 are
the ones that arrived between `dev_open` and `set_freq`), four sample frames with
their `fc`, and then lwIP's counters: `rx_frames=0` on both the in and out
paths, and `tx_packets=0`.  Nothing was injected and nothing was handed to the
network stack.


```
## Starting application at 0x0A000000 ...
boot: el2 entry
boot: dcache flushed
boot: el2 mmu off
boot: ic iallu
boot: eret to el1
boot: el1
30d00800 boot: sctlr_el1 above
boot: pre-bss
boot: post-bss
boot: mmu_enable
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=80f95b1-dirty
rtos: kernel ready tick=1000Hz
gicv3: rd frame off=00000000
gicv3: ready ctlr=00000012 waker=00000000
tick: source=cntv irq=27 freq=1000Hz
[   0.000] rtos: runner start cases=3 build=80f95b1-dirty
```

... (the `tick` case, then PCIe enumeration and the NVMe/MSI walk) ...

```
[   3.719] iwl7260_mon: wlan0 present=1 ifindex=1 mac=6c:29:95:12:f0:d9 iftype=2
[   3.726] iwl7260_mon: sink enabled=1
[   3.730] iwl7260_mon: set_iface rc=0 type=6
[   3.981] iwl7260_mon: start rc=0 netif_running=1 devtype=803
[   3.993] iwl7260_mon: set_freq rc=0 freq=2432
[   3.997] iwl7260_mon: window ms=15000
[   5.001] iwl7260_mon: sample t=1000 frames=63 beacons=13 data=11
[   6.008] iwl7260_mon: sample t=2000 frames=73 beacons=23 data=11
[   7.014] iwl7260_mon: sample t=3000 frames=92 beacons=32 data=13
[   8.020] iwl7260_mon: sample t=4000 frames=115 beacons=41 data=13
[   9.027] iwl7260_mon: sample t=5000 frames=142 beacons=51 data=13
[  10.033] iwl7260_mon: sample t=6000 frames=152 beacons=61 data=13
[  11.040] iwl7260_mon: sample t=7000 frames=188 beacons=71 data=25
[  12.046] iwl7260_mon: sample t=8000 frames=198 beacons=81 data=25
[  13.053] iwl7260_mon: sample t=9000 frames=207 beacons=90 data=25
[  14.059] iwl7260_mon: sample t=10000 frames=219 beacons=100 data=25
[  15.066] iwl7260_mon: sample t=11000 frames=255 beacons=110 data=37
[  16.072] iwl7260_mon: sample t=12000 frames=265 beacons=120 data=37
[  17.079] iwl7260_mon: sample t=13000 frames=275 beacons=130 data=37
[  18.085] iwl7260_mon: sample t=14000 frames=286 beacons=139 data=37
[  19.092] iwl7260_mon: sample t=15000 frames=295 beacons=148 data=37
[  19.098] iwl7260_mon: sink frames=297 bytes=58505 beacons=148 probes=51 probe_resp=40 auth=7 assoc=6 data=37 ctrl=39 other=9
[  19.109] iwl7260_mon: radiotap it_len=56/64 freq=2432 bad=0 short=0
[  19.116] iwl7260_mon: freq 0 mhz=2412 frames=5
[  19.120] iwl7260_mon: freq 1 mhz=2432 frames=292
[  19.125] iwl7260_mon: frame 0 len=701 it_len=56 fc=0x0850 hex=000038002f4040a0200800a020080000
[  19.133] iwl7260_mon: frame 1 len=70 it_len=56 fc=0x00d4 hex=000038002f4040a0200800a020080000
[  19.142] iwl7260_mon: frame 2 len=142 it_len=56 fc=0x0040 hex=000038002f4040a0200800a020080000
[  19.150] iwl7260_mon: frame 3 len=505 it_len=56 fc=0x0050 hex=000038002f4040a0200800a020080000
[  19.159] iwl7260_mon: lwip in rx_frames=0 rx_foreign=0 tx_frames=0
[  19.165] iwl7260_mon: lwip out rx_frames=0 rx_foreign=0 tx_frames=0
[  19.171] iwl7260_mon: tx dev tx_packets=0 tx_bytes=0
[  19.176] iwl7260_mon: PASS MON
[  19.179] rtos: case iwl7260_mon PASS
[  19.182] rtos: result pass cases=3/3 backend=freertos
```

The second cold boot of the same image printed `sink frames=256 bytes=49851
beacons=156 ...` with `radiotap it_len=56/64 freq=2432 bad=0 short=0` and one
frame on 2412 - the same shape, one boot later.

### The 5 GHz refusal - `rk3568-iwl7260-ap5g` and `rk3568-iwl7260-sta5g`

Neither of these is a bring-up transcript; they are the two 5 GHz arms, kept
because the refusal is the result.  The AP arm walks its candidate list and
finds no channel it may beacon on.  The flags it prints are the card's own
(`NO_IR` is `0x2`, `NO_HT40PLUS` `0x10`, `NO_80MHZ` `0x80`, `NO_160MHZ`
`0x100`).  The GT arm - the same card on a mainline kernel, with a signed
`regulatory.db` and `iw reg set US` in effect - refuses identically, which is
what makes this a property of the card rather than of the port.


```
[   3.655] iwl7260_ap5g: wlan0 present=1 ifindex=1 mac=6c:29:95:12:f0:d9 iftype=2
[   3.662] iwl7260_ap5g: bands 2g=1 5g=1 2g_chans=13 5g_chans=25 5g_ht=1
[   3.669] iwl7260_ap5g: cand freq=5180 chan=36 present=1 flags=0x00c00722 can_beacon=0
[   3.677] iwl7260_ap5g: cand freq=5200 chan=40 present=1 flags=0x00c00712 can_beacon=0
[   3.684] iwl7260_ap5g: cand freq=5220 chan=44 present=1 flags=0x00c00722 can_beacon=0
[   3.692] iwl7260_ap5g: cand freq=5240 chan=48 present=1 flags=0x00c00712 can_beacon=0
[   3.700] iwl7260_ap5g: cand freq=5745 chan=149 present=1 flags=0x00c00522 can_beacon=0
[   3.708] iwl7260_ap5g: cand freq=5765 chan=153 present=1 flags=0x00c00512 can_beacon=0
[   3.715] iwl7260_ap5g: cand freq=5785 chan=157 present=1 flags=0x00c00522 can_beacon=0
[   3.723] iwl7260_ap5g: cand freq=5805 chan=161 present=1 flags=0x00c00512 can_beacon=0
[   3.731] iwl7260_ap5g: cand freq=5825 chan=165 present=1 flags=0x00c005b2 can_beacon=0
[   3.739] iwl7260_ap5g: FAIL no beaconable 5 GHz channel (flags above are the regulatory fact)
[   3.747] rtos: case iwl7260_ap5g FAIL rc=5
[   3.751] rtos: result fail cases=2/3 backend=freertos
```

The station side gets further and stops at association.  The band is real in the
wiphy (25 channels, `5g_ht=1`), the channel is set, and a passive scan finds the
Pi's 5 GHz BSS at -39 dBm.  The supplicant then asks the driver to associate and
is refused before a frame goes out; it retries once, the scan comes back empty
it is told `ret=-1`, and the case times out at 40 s with no EAPOL at all.  The
supplicant's own lines are kept here rather than elided: `Association request to
the driver failed` is the exact point of the block.


```
[   3.556] iwl7260_sta5g: wlan0 present=1 ifindex=1 mac=6c:29:95:12:f0:d9
[   3.562] iwl7260_sta5g: bands 2g=1 5g=1 5g_chans=25 5g_ht=1
[   3.568] iwl7260_sta5g: chan freq=5180 chan=36 present=1 flags=0x00c00722
[   3.813] iwl7260_sta5g: start rc=0
[   3.817] iwl7260_sta5g: netif up name=wl0 ip=192.168.99.2/255.255.255.0 mac=6c:29:95:12:f0:d9
[   3.825] iwl7260_sta5g: security wpa2=0 ssid=labwifi-5g freq=5180
supplicant_main_task: 634 Starting wpa_supplicant thread with debug level: 3


Successfully initialized wpa_supplicant

iface_cb: iface wl1 ifindex 2 6c:29:95:12:f0:d9

Using interface wl1


Initializing interface 0: wl1


lkpi: l2_packet ifname=wl1 mac=6c:29:95:12:f0:d9
lkpi: del_key keyidx=0 rc=-67
lkpi: del_key keyidx=1 rc=-67
lkpi: del_key keyidx=2 rc=-67
lkpi: del_key keyidx=3 rc=-67
lkpi: del_key keyidx=4 rc=-67
lkpi: del_key keyidx=5 rc=-67
lkpi: sta iface=wl1
lkpi: sta network ssid=labwifi-5g bssid=00:00:00:00:00:00
[   3.888] iwl7260_sta5g: sta start rc=0
lkpi: sta scan ssid=labwifi-5g bssid=1c:a7:70:de:29:a1 freq=5180 signal=-39
wl1: Trying to associate with 1c:a7:70:de:29:a1 (SSID='labwifi-5g' freq=5180 MHz)

lkpi: sta connect timeout ssid=labwifi-5g
wl1: Association request to the driver failed

wl1: Authentication with 1c:a7:70:de:29:a1 timed out.

wl1: CTRL-EVENT-DISCONNECTED bssid=1c:a7:70:de:29:a1 reason=3 locally_generated=1

BSSID 1c:a7:70:de:29:a1 ignore list count incremented to 2, ignoring for 10 seconds

wl1: CTRL-EVENT-DSCP-POLICY clear_all

wl1: CTRL-EVENT-SCAN-FAILED ret=-1 retry=1

lkpi: sta scan ssid=labwifi-5g bssid=1c:a7:70:de:29:a1 freq=5180 signal=-46
wl1: Trying to associate with 1c:a7:70:de:29:a1 (SSID='labwifi-5g' freq=5180 MHz)

[  43.943] iwl7260_sta5g: FAIL sta timeout state=5 waited_ms=40000 eapol_tx=0 rx=0 drop=0 ptk=0 gtk=0 fail=0
[  43.952] rtos: case iwl7260_sta5g FAIL rc=10
[  43.956] rtos: result fail cases=2/3 backend=freertos
```

## RTL8189FTV - station role

The 8189FTV is a second card and a second driver world: the vendor's
`rtl8189fs` full-MAC stack, brought up over the board's SDIO port rather than
over PCIe, and talking to the bench's RTL8812AU AP on channel 5.  What the
console shows in this role is a clean bring-up and one instrument line that
matters.

Bring-up anchors, in order, one cold boot:

```
rtl8189fs_sta: wlan0 present=1 ifindex=1 mac=8c:bd:37:95:95:48
rtl8189fs_sta: start rc=0
rtl8189fs_diag: bss ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 freq=2432 signal=-40
drv RTW: rtw_set_802_11_connect(wlan0)  fw_state=0x00000008
rtl8189fs_sta: connected bssid=1c:a7:70:de:29:a1 waited_ms=400
rtl8189fs_sta: PASS C5
rtl8189fs_sta: netif up name=wlan0 ip=192.168.99.2/24
rtl8189fs_sta: PASS C6
    rtl8189fs_c6t: PASS C6T
```

The one line worth reading twice is the per-second sampler the port prints
during the throughput phases - it is the card's own view of its receive path,
and it is the instrument that decides whether a low cell is the pump, the
driver or the air:

```
obs t=... claim=... pump=... irq=0 io=.../... err=0 drv recv=256 rxbuf=8
    min=253/8 rxfifo=0 txb=.../256 cmd=.../0 la=... lb=...
```

`err=0`, `rxfifo=0` and `rxbuf=8` hold for all 60 samples of every boot; when
the pump stops advancing it shows as `claim` going stale, not as an error, which
is why the pump-timing arms (adaptive, yield, prio, drain) are the ones that
moved the downlink and the SDIO interrupt arm did not.

Two console facts about this role.  The card prints nothing at AP-role volume
here: a station boot's serial log carries the bring-up above and the phase
anchors, and no driver warnings at all - in particular the
`rx_data compare failed` line that the AP role produces does **not** appear in
any station boot, which is how that warning was first localised to the
broadcast/multicast clone path rather than to receive in general.  And the peer
side has no console of its own in this role: the peer is an access point running
a stock driver, so every number from that end comes from `iperf2` and the
counting sink, not from a serial line.

## RTL8189FTV - access point role

## RTL8189FTV - access point role (2026-11-13)

Same card, same SDIO port, the other role: the port's `hostapd` brings the board
up on channel 5 and the bench's `MT7921AU` joins it.  The console is the whole
reason this role was worth a round, because the bring-up is a strict sequence
that fails quietly one step at a time.

The anchors, in the order they must appear, one cold boot:

```
rtl8189fs_ap: wlan0 present=1 ifindex=1 mac=8c:bd:37:95:95:48 iftype=2
rtl8189fs_ap: start rc=0
rtl8189fs_ap: chan freq=2432 ... can_beacon=1
rtl8189fs_ap: set_iface rc=0 type=3 order=up_first
rtl8189fs_ap: security wpa2=0 ssid=rtos-8189-ap channel=5 bcn_int=100 dtim_req=2
lkpi: hostapd daemon ready
wl1: interface state UNINITIALIZED->COUNTRY_UPDATE
lkpi: set_ap head=63 tail=87 freq=2432 rc=0
wl1: interface state COUNTRY_UPDATE->ENABLED
wl1: AP-ENABLED
hostap start rc=0 ready=1
rtl8189fs_ap: diag post-start valid=1 fwstate=0x00000011 ap_state=1 bstart=1
    ch=5 bcn_int=100 nr_ap_if=1 nr_sta=0
RTW: cfg80211_rtw_start_ap(wlan0) hidden_ssid:0, auth_type:0
rtl8189fs_ap: PASS A1
```

Two things in that sequence are worth knowing before reading any AP log from
this card.  The daemon calls its interface `wl1` while the driver's netdev is
`wlan0`; the port's `hostap` seam maps any `wl*` name onto `wlan0`, so a log
where the two names disagree is normal and not a mis-wiring.  And the requested
DTIM is 2 while the beacon that goes on the air carries 1 - the vendor's own
`rtw_set_tim_ie` is called with a fixed `(0, 1)` - so the log line is honest
about the request and the air is what the card does.

Association, from the board's side:

```
RTW: OnAssocReq(wlan0) allocate new AID=1
RTW: issue_asocrsp
RTW: indicate_sta_assoc_event to upper layer - hostapd
RTW: rtw_cfg80211_indicate_sta_assoc(wlan0)
rtl8189fs_ap: sta n=1 mac=90:de:80:4c:38:c5 aid=1 state=0x4001 flags=0x2a80
    rate_id=3 cur_rate=0 bw=0 sgi=1
rtl8189fs_ap: PASS A2
rtl8189fs_ap: link window rx_frames=85 ... rx_foreign=0 rx_no_netif=0
    rx_input_fail=0 tx_fail=0
lkpinetif: rx#1 dev=wlan0(ours) bound=wlan0 proto=0x0806 len=28
rtl8189fs_ap: PASS A3
rtl8189fs_ap: server tcp=5001 udp=5002 ok=1
rtl8189fs_ap: PASS APT
rtos: result pass cases=2/2 backend=freertos
```

The nine lines between `PASS A1` and `PASS A3` are the ones that make an AP
round diagnosable rather than a pass/fail: `OnAssocReq` -> `issue_asocrsp` is
the step that used to take ~110 ms of console time on this card and blow the
client's association budget, and the `link window` line is the port's own
count of what it accepted - `rx_foreign=0` and `rx_input_fail=0` say the frames
reached the right netdev and the stack took them.

The warning this role produces and the station role does not:

```
rtl8189fs_ap: rx_data compare failed
```

It appears on **every** AP boot (6 to 33 hits, and the console's repeat folder
collapses runs of identical lines, so a grep alone undercounts) and on **no**
station boot and never in the reference kernel's log.  It comes from the
broadcast/multicast clone path: the driver copies a received frame once per
registered interface and compares the copy against the original, and on this
port the copy differs.  What is established is where it lives and when it
fires; what is not established is whether it costs throughput, and it is
recorded here as an open port-side discrepancy rather than as a cause of the
AP role's numbers.

Beacon cadence, from the same logs, is a card fact rather than a port fact: the
mean interval is 116-125 ms with occasional gaps of 300-410 ms on this port and
114.9 ms with a 307 ms gap on the reference kernel, against an advertised 100 TU.
That is the vendor's software beacon timer, and it is why the AP role's timing
is compared between the two arms and not against the advertisement.

## What is not claimed here

Every transcript is one boot.  Where a cell is shown it is one cell: the
transcripts establish that the exchange happens and which counters account for
it, not that a rate repeats.  The performance table in the top-level README is
where a rate is allowed to stand, and it names the boots each cell came from.

Not claimed, each with its reason:

- **Any band other than 2.4 GHz** for the two connected roles, and **any channel
  width other than 20 MHz** anywhere.  The AP seam sets `secondary_channel = 0`
  and `ieee80211ac = 0`; a 40 MHz setup would send hostapd's coexistence check
  through `get_scan_results`, which this port does not provide.  "This card is a
  2x2 802.11ac card" is the card's specification, not a measurement here.
- **5 GHz in either role**: the AP's refusal is the card's, and the station's
  association is blocked with a reopen condition, not judged.
- **Transmit from a monitor interface** - injection, channel hopping and the
  five-minute monitor soak are untested.  The transcript above is receive only.
- **Power-save buffering.**  `ps_buffered` in the AP transcript is the driver's
  own ledger; there is no `SET_POWER_SAVE` A/B behind it.
- **The GT arm as a control for anything but throughput and the monitor frame
  rate.**  Its job is to be the same card on a mainline kernel; it is not a
  second measurement of the port.
- **Compliance.**  There is no `regulatory.db` in this image, so the wiphy runs
  on the world regdomain.  The channel flags above are the driver's
  `orig_flags`, not a country's rules.