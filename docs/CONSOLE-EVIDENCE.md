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

## RTL8851BU (USB, mac80211 + rtw89)

Card: Realtek RTL8851BU, `0bda:b851`, MAC `90:de:80:1b:25:ed`, firmware
`0.29.41.5 (4bd6ebac)`, on the board's DWC3 xHCI at `0xFCC00000`.  The stack is
the ported `net/mac80211` + `net/wireless`; the driver is the GT pin's `rtw89`
tree with no source patch.  The bench is the standard one: Pi RTL8812AU
`wlan2` is `labwifi-ar-ht20s` on ch5/2432/HT20/open, and for the monitor round
the Pi's MT7921AU `wlan1` is a client.  Three roles below; each transcript is
one cold boot, and the two boots named 2/2 are the two in the run record.

Every boot also prints this line, as the other cards do:

```
[   3.063] rtw89_mon: drv failed to load regulatory.db
```

That is the world regdomain talking, not a card failure: the image has no
`regulatory.db`, so every channel's flags are the driver's `orig_flags` and
not a country's rules.  The channel the roles use is `flags=0x00080180` on
2432, which the AP case prints before it beacons.

### Station - `rk3568-rtw89-8851bu-c5` and `-c6`

What the transcript proves, in order: the USB engine and the compiled-in
firmware bring up `wlan0`; the real scan finds the bench BSS on 2432; the open
association completes in 600 ms; `PASS C5`; the lwIP netif is `wl0` with the
card's MAC and `192.168.99.2/24`; both ICMP payloads are 20/20 and `PASS C6`.

```
[   3.050] rtw89_c5: core ready
[   5.980] rtw89_c5: wlan0 present=1 ifindex=1 mac=90:de:80:1b:25:ed
[   8.256] rtw89_c5: scan ssid=labwifi-ar-ht20s freq=2432 rc=0
[   8.361] rtw89_c5: bss ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 freq=2432 signal=-18
[  10.033] rtw89_c5: connect ssid=labwifi-ar-ht20s bssid=1c:a7:70:de:29:a1 freq=2432 rc=0
[  10.759] rtw89_c5: connected bssid=1c:a7:70:de:29:a1 waited_ms=600
[  10.765] rtw89_c5: PASS C5
[  10.968] rtw89_c5: netif up name=wl0 ip=192.168.99.2/24 mac=90:de:80:1b:25:ed
[  23.725] rtw89_c5: ping tx=20 rx=20 payload=56 rtt_ms=2/4/20
[  24.450] rtw89_c5: ping tx=20 rx=20 payload=1400 rtt_ms=3/3/13
[  24.464] rtw89_c5: PASS C6
```

The clean C6T arm (`rk3568-rtw89-8851bu-c6`) adds the seven phases.  The phase
block is worth reading in full once; the short version here is the one the
README's station table comes from:

```
[  24.455] rtw89_c6: plan=throughput bssid=1c:a7:70:de:29:a1 dev=wlan0
[  24.462] rtw89_c6: ready netif=1 rx_frames=43 tx_frames=43
[  24.472] rtw89_c6: phase 1 begin proto=tcp role=client port=5001 window_ms=12000 name=loopback
[  34.487] rtw89_c6: phase 1 report bytes=498690034 ms=10001 kbit=398912 pkts=0
[  34.574] rtw89_c6: phase 2 begin proto=tcp role=server port=5001 window_ms=30000 name=tcpdn
[  63.794] rtw89_c6: phase 2 report bytes=91095064 ms=26736 kbit=27256 pkts=0
[  63.864] rtw89_c6: phase 3 begin proto=udp role=server port=5002 window_ms=20000 name=udpdn
[  85.349] rtw89_c6: phase 3 report bytes=94668000 ms=20000 kbit=37867 pkts=64400
[  85.419] rtw89_c6: phase 4 begin proto=tcp role=server port=5001 window_ms=30000 name=tcpdn2
[ 114.937] rtw89_c6: phase 4 report bytes=111280152 ms=26733 kbit=33296 pkts=0
[ 136.654] rtw89_c6: phase 6 begin proto=tcp role=client port=5001 window_ms=12000 name=tcpup
[ 146.722] rtw89_c6: phase 6 report bytes=56636344 ms=10007 kbit=45272 pkts=0
[ 166.886] rtw89_c6: PASS C6T
```

The two TCP-down repeats are 27.26 and 33.30 Mbit/s; the boot's cell is the
larger, and the 80% gate (33.8) is missed at the two-boot mean of 31.90.  The
TCP-up cell is 45.27 (81.3% of the reference), and the 40 M UDP downlink is
37.87 with 0% loss.  That is the station table's fail and pass; the console's
job is to show that the phases ran and which counters moved.

### AP - `rk3568-rtw89-8851bu-ap` and `-ap-tp`

The AP role is the other direction of the same seam: the ported NXP `hostapd`
owns the AP MLME, mac80211 owns the AP state machine, and rtw89 pushes the
beacon to the firmware.  The transcript below proves, in order: the interface
type change, the lwIP netif, hostapd's start and beacon, the Pi's association,
and both ICMP payloads in both directions.

```
[   5.999] rtw89_ap: set_iface rc=0 type=3
[   7.189] rtw89_ap: start rc=0
[   7.192] rtw89_ap: netif up name=wl0 ip=192.168.99.1/24 mac=90:de:80:1b:25:ed
[   7.200] rtw89_ap: security wpa2=0 ssid=rtos-8851-ap channel=5 bcn_int=100 dtim_req=2
[  14.019] rtw89_ap: hostap start rc=0 ready=1
[  14.023] rtw89_ap: link freq=2432
[  14.026] rtw89_ap: PASS A1
[  20.571] rtw89_ap: sta n=1 hostap=1 mac=90:de:80:4c:38:c5
[  20.576] rtw89_ap: PASS A2
[  36.669] rtw89_ap: ping payload=56 tx=20 rx=20 dup=0 rtt_ms=3/24/78
[  37.786] rtw89_ap: ping payload=1400 tx=20 rx=20 dup=0 rtt_ms=3/25/74
[  37.803] rtw89_ap: PASS A3
```

The A4 arm (`rk3568-rtw89-8851bu-ap-tp`, `PASS APT` twice) adds the five-phase
table.  The block for the first phase is the shape every phase has:

```
[  37.811] rtw89_ap: phase 1 begin proto=tcp role=client air=down port=5001 window_ms=12000 name=tcpdn peer_ms=3000 rate_kbps=0
[  50.883] rtw89_ap: phase 1 report bytes=8360714 ms=10007 kbit=6680 pkts=0
[  50.890] rtw89_ap: phase 1 udp offered=0 tx_err=0 rx_drop=0 first_ms=0 last_ms=0 beat=5548 stall_ms=250
[  50.900] rtw89_ap: phase 1 stack tcp_recv=5462 tcp_xmit=5729 tcp_drop=0 tcp_memerr=0 tcp_err=0 pbuf_err=0 tcpseg_err=0 udp_in=0 udp_in_err=0 udp_noport=0 ip_drop=0 ip_chkerr=0 link_drop=0 mbox_full=0
[  50.918] rtw89_ap: phase 1 census rx_frames=5462 rx_bytes=294956 tx_frames=5729 tx_fail=0 rxdlv_us=0 txndo_us=0 tx_status=5729 dequeue=5729 stopq=0 wakeq=0 skb_used=16 skb_high=128 alloc_fail=0 data_ovf=0
[  50.936] rtw89_ap: phase 1 usb submits=6865 completes=6865 refused=0 stage_overflow=0 slots_exhausted=0 rx=1130 rx_err=0
[  50.947] rtw89_ap: phase 1 net rx_frames=5462 tx_frames=5729 rx_foreign=0 rx_no_netif=0 rx_input_fail=0 rx_post_fail=0
[  50.957] rtw89_ap: phase 1 done ok=1
```

The A4 table's cells are a 10 s common window (the GT flow ends at 10.4 s);
the phase report is the full 24 s, so phase 2's 7.95 is not the table's 7.66.

### Monitor - `rk3568-rtw89-8851bu-mon`

What the transcript proves, in order: the type change, the open (devtype 803,
the radiotap type), the channel request, and a 15 s window in which the port's
sink, not lwIP, takes the frames.  `PASS MON` is gated on >=8 beacons/s,
`bad=0`, an unchanged lwIP `rx_frames` and `tx_packets=0`; the frame census is
reported, not gated.

```
[   5.986] rtw89_mon: wlan0 present=1 ifindex=1 mac=90:de:80:1b:25:ed iftype=2
[   5.993] rtw89_mon: chan freq=2432 present=1 flags=0x00080180
[   5.999] rtw89_mon: sink enabled=1
[   6.002] rtw89_mon: set_iface rc=0 type=6
[   7.088] rtw89_mon: start rc=0 netif_running=1 devtype=803
[   8.096] rtw89_mon: set_freq rc=0 freq=2432
[   8.100] rtw89_mon: window ms=15000
[  23.191] rtw89_mon: sample t=15000 frames=236 beacons=161 data=12
[  23.197] rtw89_mon: sink frames=236 bytes=42007 beacons=161 probes=7 probe_resp=3 auth=6 assoc=6 data=12 ctrl=35 other=9
[  23.208] rtw89_mon: radiotap it_len=26/39 freq=2432 bad=0 short=0
[  23.214] rtw89_mon: freq 0 mhz=2412 frames=10
[  23.218] rtw89_mon: freq 1 mhz=2432 frames=226
[  23.256] rtw89_mon: lwip in rx_frames=0 rx_foreign=0 tx_frames=0
[  23.262] rtw89_mon: lwip out rx_frames=0 rx_foreign=0 tx_frames=0
[  23.268] rtw89_mon: tx dev tx_packets=0 tx_bytes=0
[  23.283] rtw89_mon: PASS MON
```

The second boot prints 231 frames and the same 161 beacons, `it_len` 26/50,
221 frames on 2432, and the same lwIP and tx lines.  The ten frames on 2412
are the wiphy's default channel before the channel request lands; the sink
keeps a per-frequency histogram so that "the frequency" is the dominant one
and the minority is visible rather than hidden.  The reference is the AP's
100 TU (about 9.77 beacons/s), not a same-card Linux monitor arm; the two boots
see 10.7/s, which is the cadence check the README reports.  RX only: nothing
is injected, and channel hopping, 5 GHz and a five-minute soak are untested.

### The lines that mattered

**The an earlier A4 boot silence.**  The 2026-11-14 AP round's `rtos-an earlier A4 boot` boot ends at a
phase's begin line, with the next ~25 s of the capture carrying nothing:

```
[  82.358] rtw89_ap: phase 2 done ok=1
[  82.361] rtw89_ap: phase 3 begin proto=tcp role=client air=down port=5001 window_ms=12000 name=tcpdn2 peer_ms=3000 rate_kbps=0
```

The old record called this "tcpup / 12 minutes"; both were wrong.  It is phase
3 (`tcpdn2`), the capture ends about 25 s later at the host's own clock, and
the case never resumed.  It did not reproduce in the 2/2 clean A4 boots, and
the mechanism is not proven; the diag image that would name it
(`-ap-diag`, stage anchors plus the 2 s watchdog) was built but not booted.
The reopen condition is in the bench record.

**The invalid one invalid boot boot.**  This is the boot that exposed the two Pi TCP
servers racing port 5001.  Its phase 3 is a 0-byte TCP downlink with the
heartbeat stale, and phases 4/5 show the pool pressure that follows:

```
[  92.785] rtw89_ap: phase 3 fail rc=-1 lock=- held=0ms beat=20807 stall_ms=11900
[ 101.804] rtw89_ap: drv lkpiu: failed to alloc skb for h2c join
[ 107.819] [previous line repeated 1 times]
[ 120.029] rtw89_ap: drv lkpiu: failed to alloc skb for h2c join
[ 129.124] [previous line repeated 4 times]
[ 129.163] rtw89_ap: phase 5 census rx_frames=5636 rx_bytes=8521632 tx_frames=0 tx_fail=0 rxdlv_us=0 txndo_us=0 tx_status=0 dequeue=0 stopq=0 wakeq=0 skb_used=128 skb_high=128 alloc_fail=2454 data_ovf=7968
[ 129.206] rtw89_ap: FAIL traffic phases 4/5
```

The h2c line is the driver failing to allocate a join command because the skb
pool is full (`skb_used=128/128`); it is a symptom of the phase's TX pressure,
not a read on the air.  The boot is invalid for the 2/2 because phase 3 had no
server; it is kept as mechanism evidence.

**The C6T WARN flood.**  The previous C6T round misread a stalled receive
window as a mac80211 reorder-ring freeze.  The diagnostic arm
(`rk3568-rtw89-8851bu-c6-diag`, `diag1l`) says otherwise: the queue held 187
unparsed USB RX buffers, the pool was 128/128, and the receive worker spent the
window printing one warning per frame:

```
[  54.731] rtw89_c6: phase 2 report bytes=0 ms=0 kbit=0 pkts=0
[  54.780] rtw89_c6: pool phase 2 skb=128/128 data=3/64 af=8 df=0 sf=0
[  54.787] rtw89_c6: owner phase 2 q=000000000A521BF8 items=187 bytes=173990
[  54.816] rtw89_c6: pf phase 2 calls=4820
[  54.821] rtw89_c6: pfra phase 2 ra=000000000A0A6EF0 n=4808
[  54.840] rtw89_c6: mcmp phase 2 calls=305812919 bytes=4278978371
```

`pfra`'s 4,808 calls resolve to `ieee80211_rx_list`'s
`WARN_ON_ONCE(softirq_count()==0)`, and the 305.8 million `memcmp` calls
resolve to the log sink's `strstr`.  The port's `WARN_ON_ONCE` was a
`WARN_ON`; the RX runs in task context, so the warning was true on every
frame.  Making it per-site once removed the flood; the clean arm's seven
phases complete and `PASS C6T`, and the downlink is 0.75x the reference.  The
old "reorder freeze" classification is withdrawn.

**The repeated-line folder.**  The console collapses runs of identical lines
into `[previous line repeated N times]`.  It is the console's own compression,
not a case anchor, and a `grep` for the repeated text undercounts it.  The
an earlier A4 boot has no repeat lines because it went silent; the invalid one invalid boot
boot has two runs, in phases 4 and 5.

**How to read a phase block.**  `begin` names proto/role/air/port/window;
`report` is the client's or server's own bytes over the interval it measured;
`udp` is the sender's half (offered, `tx_err`, first/last datagram time,
heartbeat `beat` and `stall_ms`); `stack` is lwIP's counters; `census` is the
adapter plus the skb pool and TX health; `usb` is the CherryUSB completions;
`net` is the port's netdev view; `done ok=` is the phase verdict.  `stall_ms`
is the gap between lwIP heartbeat samples, not a card stall; `tx_err` is a
refused transmit (pool or queue), not an air loss; `data_ovf` is the data pool
falling back to the heap; `skb_used/high` is the pool's occupancy.  A phase
that reports 0 bytes with a stale `beat` never sent; one that reports bytes
with a rising `tx_fail` offers faster than the port can drain.

## RTL8188EUS and RTL8723BU (USB, mac80211 + rtl8xxxu)

Two cards, one vendored driver tree.  The RTL8188EUS (`0bda:8179`, MAC
`20:f4:1b:52:8f:01`) is the dongle behind the external hub on the board's
xHCI (`bus=3 hub=2 port=2`); the RTL8723BU (`0bda:b720`, MAC
`88:e6:28:53:10:9c`) is the board's EHCI dongle behind the `1a86:8091` hub
(`bus=1 hub=2 port=2`).  The stack is the ported `net/mac80211` +
`net/wireless`; the driver is the GT pin's `rtl8xxxu` tree, with the four
recorded patches for the 8188EU's AP/monitor roles and no patch for the
8723BU.  The 8723BU is a composite device - Bluetooth on interfaces 0/1,
Wi-Fi on interface 2 - which is why its claim line carries `intf=2` while
the 8188EU's is `intf=0`.

The 8723BU's firmware is `rtlwifi/rtl8723bu_nic.bin`, printed as
`Firmware revision 35.0 (signature 0x5301)`; the 8188EU asks for
`rtlwifi/rtl8188eufw.bin` and prints `28.0 (signature 0x88e1)`.  The two
profiles link only their own blob (`CONFIG_RTL8XXXU_CHIP_8723BU`), so a
boot's `fw registered=0 name=...` line says which image it is.

The GT arm is the same pin's in-tree `rtl8xxxu` on the same bench and the
same firmware image.  Both dongles are on the GT board at once, so the GT
boot's log carries **two** firmware lines; which one a parser picks is a
serial-identification question, not a card question (see the phenomena
below).

The throughput ruler is defined by the **board's direction**, not by the
phase name: 30 s board-receive TCP, 20 s board-send TCP, 20 s
board-receive UDP at 10M offered, 10 s board-send UDP at 10M paced.  The
GT arm runs the same table; the AP arm runs the mirrored names
(`tcpdn`/`tcpup` swapped) and is mapped back.  The raw `serial.log` files and
the peer-side iperf outputs for every boot named here are in
the 2026-11-14 bench run and the 2026-11-15 bench run
(workspace ledger); this section carries the transcript excerpts and the
numbers they support.

### RTL8188EUS - station and AP

What the transcript proves, in order: the driver claims `0bda:8179` on the
xHCI hub's port 2, loads `rtl8188eufw.bin` (28.0 / 0x88e1), the open
association completes, and the seven phases run over the lwIP netif.

```
[   3.061] rtl8xxxu_c5: usb buses=3,2
[   4.829] rtl8xxxu_c5: drv usbh: lkpi claim attempt 0bda:8179 bus=3 hub=2 port=2 intf=0 spd=3
[   4.988] rtl8xxxu_c5: firmware none: Firmware revision 28.0 (signature 0x88e1)
[   6.295] rtl8xxxu_c5: drv wlan0: authenticate with 000000000A3A7528M (local address=000000000A3A274CM)
[   6.666] rtl8xxxu_c5: connected bssid=1c:a7:70:de:29:a1 waited_ms=300
[  23.917] rtl8xxxu_c6: phase 1 report bytes=499015614 ms=10000 kbit=399208 pkts=0
[  53.322] rtl8xxxu_c6: phase 2 report bytes=141033496 ms=27906 kbit=40424 pkts=0
[  73.543] rtl8xxxu_c6: phase 3 report bytes=23755200 ms=20000 kbit=9502 pkts=16160
[ 103.877] rtl8xxxu_c6: phase 4 report bytes=137625624 ms=27901 kbit=39456 pkts=0
[ 124.098] rtl8xxxu_c6: phase 5 report bytes=23752260 ms=20000 kbit=9500 pkts=16158
[ 134.330] rtl8xxxu_c6: phase 6 report bytes=6693394 ms=10010 kbit=5344 pkts=0
[ 144.516] rtl8xxxu_c6: phase 7 report bytes=12499410 ms=10000 kbit=9999 pkts=8503
[ 144.681] rtl8xxxu_c6: PASS C6T
```

`authenticate with 000000000A3A7528M` is the `%pM` artifact described at the
top of this file: the port's `printf` has no `%pM`, so the address is not a
MAC in the log.  The case's own `mac=`/`bssid=` lines are the identity
evidence.

The two clean boots (the two clean boots) and the same-day GT arm (the same-day GT arm) give the four cells:

| board direction | 8188EU station | GT | ratio |
|---|---|---|---|
| board-receive TCP | 40.54 | 39.10 | **104%** |
| board-send TCP | 5.26 | 37.65 | **14%** |
| board-receive UDP (10M) | 9.49 (0% loss) | 10.5 (0%) | 90% |
| board-send UDP (10M) | 10.0 | 10.5 | 95% |

The AP role (`rk3568-rtl8xxxu-8188eu-ap-tp`, `ap1`/`ap3`) runs the same
table with the roles swapped: the ported NXP `hostapd` owns the AP MLME and
`rtl8xxxu` sends the host-composed beacon.

```
[   4.993] rtl8xxxu_ap: firmware none: Firmware revision 28.0 (signature 0x88e1)
[   6.097] rtl8xxxu_ap: set_iface rc=0 type=3
[  11.316] rtl8xxxu_ap: hostap start rc=0 ready=1
[  11.321] rtl8xxxu_ap: link freq=2432
[  53.242] rtl8xxxu_ap: phase 1 report bytes=8192084 ms=10017 kbit=6536 pkts=0
[  78.791] rtl8xxxu_ap: phase 2 report bytes=125567000 ms=23778 kbit=42240 pkts=0
[ 101.883] rtl8xxxu_ap: phase 3 report bytes=13788600 ms=20000 kbit=5515 pkts=9380
[ 114.932] rtl8xxxu_ap: phase 4 report bytes=12499410 ms=10000 kbit=9999 pkts=8503
[ 114.964] rtl8xxxu_ap: PASS APT
```

The AP table maps the phase names to the board's direction (`tcpdn` is
board-send here, `tcpup` is board-receive):

| board direction | 8188EU AP | GT | ratio |
|---|---|---|---|
| board-receive TCP | 38.16 | 39.10 | 98% |
| board-send TCP | 6.38 | 37.65 | **17%** |
| board-receive UDP (10M) | 5.42 (12.4/15.4% loss) | 10.5 (0%) | **52%** |
| board-send UDP (10M) | 10.0 | 10.5 | 95% |

AP/STA same-direction: receive TCP 94%, send TCP 121%, receive UDP 57%,
send UDP 100%.  Three of four directions align; the two open mechanisms are
the board-send TCP gap (14-17%) and the AP-role UDP receive (12-15% loss
where the station role loses none).

### RTL8723BU - station

The 8723BU claims on the EHCI root; the composite device's Bluetooth
interfaces are enumerated and declined, and the Wi-Fi interface is `intf=2`.

```
[   4.311] rtl8xxxu_c5: drv usbh: modeswitch eject attempt 1: 31 byte(s) sent
[   5.377] rtl8xxxu_c5: drv usbh: modeswitch accepted (device left the bus)
[   5.392] [E/usbh_hub] Port 1 enumerate fail
[   5.725] rtl8xxxu_c5: drv usbh: lkpi claim attempt 0bda:b720 bus=1 hub=2 port=2 intf=2 spd=3
[   6.071] rtl8xxxu_c5: firmware none: Firmware revision 35.0 (signature 0x5301)
[   7.965] rtl8xxxu_c5: drv wlan0: authenticate with 000000000A3AFDE8M (local address=000000000A3AB0BCM)
[   8.355] rtl8xxxu_c5: connected bssid=1c:a7:70:de:29:a1 waited_ms=300
[  25.701] rtl8xxxu_c6: phase 1 report bytes=493985914 ms=10001 kbit=395144 pkts=0
[  55.490] rtl8xxxu_c6: phase 2 report bytes=59637784 ms=28354 kbit=16824 pkts=0
[  75.693] rtl8xxxu_c6: phase 3 report bytes=23709630 ms=20000 kbit=9483 pkts=16129
[ 105.917] rtl8xxxu_c6: phase 4 report bytes=0 ms=0 kbit=0 pkts=0
[ 126.128] rtl8xxxu_c6: phase 5 report bytes=23671410 ms=20000 kbit=9468 pkts=16103
[ 136.361] rtl8xxxu_c6: phase 6 report bytes=26923154 ms=10019 kbit=21496 pkts=0
[ 146.539] rtl8xxxu_c6: phase 7 report bytes=12499410 ms=10000 kbit=9999 pkts=8503
[ 146.713] rtl8xxxu_c6: PASS C6T
```

The two clean boots (the two clean boots) and the same-day GT arm (the same-day GT arm):

| board direction | 8723BU station | GT | ratio |
|---|---|---|---|
| board-receive TCP | 16.94 | 27.1 (tcpdn 21.4/28.7; repeat 25.4/27.7) | **62.5%** |
| board-send TCP | 21.16 | 18.35 (18.0/18.7) | **115%** |
| board-receive UDP (10M) | 9.49 (0% loss) | 10.5 (0%) | 90% |
| board-send UDP (10M) | 10.0 | 10.4 (10.3/10.5) | 96% |

Three cells align; the board-receive TCP is the one open cell.  The GT's own
two `tcpdn` sessions scatter 21.4/28.7 (it decays from 37.5 to 13-19 inside
the 29 s window), so the cell is reported as 62.5% of the max-of-two with
the raw range beside it, not as a stable single rate.

Two defects are visible in the serial, not hidden:

- one boot stopped at `phase 6 begin` (126.201 s) and printed nothing for
  more than three minutes; the board needed a power cycle.  the two clean boots
  did not reproduce it (1/3 boots).
- the repeat session `tcpdn2` is 0 bytes in **3/3** RTOS boots while the
  adapter's counters keep moving in the same window (e.g. one boot phase 4:
  `rx_frames=39170`, report 0 bytes).  The GT's second session is normal
  (25.4/27.7).  It is a lwiperf/second-session defect, not a rate gate.

### The serial phenomena in these transcripts

- **The `%pM` artifact.**  The port's `printf` has no `%pM`, so MACs print as
  `000000000A3A7528M`.  This is in every 8188/8723 transcript (the
  `authenticate with` line above) and in the AC 7260 ones; the case's own
  `mac=`/`bssid=` lines are the identity evidence.
- **Two dongles in the GT log.**  The GT board carries both cards, so its
  boot has two `Firmware revision` lines.  The 2026-11-14 the GT arm summary's
  `GT_RTL8XXXU_FW` is the **8723BU's** line
  (`usb 1-1.2: ... 35.0 (0x5301)`), not the 8188EU's
  (`usb 4-1.2: ... 28.0 (0x88e1)`); the arm's WLAN was selected by MAC, but
  the firmware sentinel was the other card.  The runner now anchors on the
  firmware request name (`EXPECT_FWREQ`) rather than the first
  `Firmware revision`, and the 8723 arm passes `EXPECT_FWREQ` explicitly.
- **GT lines duplicated.**  The GT bootargs are
  `console=ttyS2,115200 earlycon=uart8250,mmio32,0xfe660000 keep_bootcon`;
  with `keep_bootcon` the early console and `ttyS2` both write to the same
  UART, so every kernel line after `printk: console [ttyS2] enabled`
  appears twice.  The RTOS logs have one copy each.  The duplication is an
  instrument artifact; the reports' parsers take the last/ anchored line,
  but a naive count doubles.
- **`none:` as the device prefix.**  The RTOS printk has no device name for
  the USB interface, so lines read `firmware none: ...` and `drv none: ...`
  where Linux prints `usb 1-1.2: ...`.  It is a log-prefix gap, not a
  missing firmware: the firmware line follows and names the blob.
- **The 8723 image ejects the 8821CU.**  The common USB glue runs the
  `0bda:1a2b -> c820` modeswitch even in the rtl8xxxu-only image (the
  sibling dongle on the hub's port 1), so the 8723 transcript has
  `modeswitch ...` and `Port 1 enumerate fail` before the 8723BU claim.
  It is a profile leak in the instrument, not a 8723 step; the 8723 claim
  on port 2 is unaffected.

## STM32F429 USB host (DWC2 + MSC, 2026-11-15)

This is the F429 OTG_HS full-speed host arm (`PROFILE=stm32f429-usb-msc`,
case `usb_dwc2`), a **2/2 PASS** on the second U disk (USB3.0 Card Reader,
`05e3:0749`).  The transcript is one of the two boots; both are byte-identical
in their judged anchors and are `2026-11-15 bench run{2,3}-hos/`.

```
rtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=...
rtos: kernel ready tick=1000Hz
...
tick: PASS
rtos: case tick PASS
rtos: case usb_dwc2 start
usbh: clock hz=168000000 fallback=0
usbh: core init bus=0 hcd=dwc2 base=0x40040000
usbh: bus0 event init hub=0 port=0 intf=255
[I/USB] ========== dwc2 hcd params ==========
[I/USB] CID:00001100
[I/USB] GSNPSID:4f54281a
[I/USB] GHWCFG1:00000000
[I/USB] GHWCFG2:229ed590
[I/USB] GHWCFG3:03f403e8
[I/USB] GHWCFG4:17f00030
[I/USB] dwc2 has 12 channels and dfifo depth(32-bit words) is 1024
...
[I/usbh_hub] New full-speed device on Bus 0, Hub 1, Port 1 connected
[I/usbh_core] New device found,idVendor:05e3,idProduct:0749,bcdDevice:1532
[I/usbh_core] Enumeration success, start loading class driver
usbh: bus0 event device-configured hub=1 port=1 intf=255
[I/usbh_core] Loading msc class driver on interface 0
[I/usbh_msc] Get max LUN:1
[I/usbh_msc] Ep=81 Attr=02 Mps=64 Interval=00 Mult=00
[I/usbh_msc] Ep=02 Attr=02 Mps=64 Interval=00 Mult=00
[I/usbh_msc] Register MSC Class:/dev/sda
[E/usbh_msc] csw bStatus 1
[W/usbh_msc] Device not ready, try again...
[I/usbh_msc] Capacity info:
[I/usbh_msc] Block num:123596800,block size:512
usbh_msc: ready bus=0 dev=2 blocks=123596800 bsize=512 (60350 MiB)
usbh: bus0 event interface-start hub=1 port=1 intf=0
usbh: msc bus=0 hcd=dwc2 vid=05e3 pid=0749 speed=2 blocks=123596800 bsize=512 serial=-
usbh: msc ep in=0x81 type=bulk maxp=64 out=0x02 type=bulk maxp=64
usbh: msc blk0 ok sig=55aa
usbh: msc read lba=0 blocks=8 bytes=4096 ok
usbh: msc window lba=123594752 blocks=2048
usbh: msc roundtrip ok lba=123594752 len=512
usbh: msc PASS
rtos: case usb_dwc2 PASS
rtos: result pass cases=2/2 backend=freertos
```

What it proves: the OTG_HS core is reachable at `0x40040000` (GSNPSID
`0x4f54281a`, 12 host channels, 1024-word FIFO), the DWC2 port switched it to
host mode, the USB3 card reader enumerated at full speed, the MSC class claimed
interface 0 (subclass 6, protocol 0x50), and the raw block path read LBA 0 /
eight blocks and wrote/read back one 512 B sector at LBA 123,594,752 (the
1 MiB-aligned start of the disk's last 1 MiB window, with 4 MiB in front never
touched).  `judge.py` passes the `usb_dwc2` branch on both boots: identity +
bulk endpoints + block zero + roundtrip, no RK throughput arms.

Two code fixes are part of this transcript.  First, a USB 3.x device's
`bMaxPacketSize0=9` is the SuperSpeed encoding; the F429 is a full-speed host,
so `usbh_core.c` now uses 64 for EP0 unless the port actually negotiated
SuperSpeed.  Second, the DWC2 channel-free path can enter the OSAL critical
section from the OTG_HS ISR; the F429 `osCriticalEnter/Exit` now selects the
FromISR pair by `IPSR`, because the CM4F port asserts on `taskENTER_CRITICAL()`
from an interrupt.  The control-transfer timeout was also raised to 5 s for a
card reader's first response.

The earlier, **invalid** boot is kept in an earlier invalid boot: the first stick did
not present D+ (`HPRT=0x00001000`, PCSTS=0, `irq=0`) while the U disk was on
the micro USB OTG1 port; that port's VBUS is a `5V_USB` input and does not power
a bus-powered stick, and P2 USB-A's `HOST_DN/DP` need J74-J76 on the HOS side.
It is not part of the 2/2.  The vendor's own `S-ST-USB-HOST读写U盘` example uses
the same OTG_HS/PB14/PB15 and enumerated the same card reader, which is how the
hardware/clock/pin side was separated from this port's code.

There is no cold boot on this bench (`mculab` `power.type=none`); both passes
are nRST hardware resets.  The test is raw MSC, not FatFs: the card reader's
exFAT/FAT32 formatting is not part of this judgement.  Hot-plug, composite
CD-ROM+storage devices, multiple LUNs and write-protected sticks are not
covered.

### RTL8188EUS at the USB layer (2026-11-15)

A direct RTL8188EUS enumerates on the F429 host even though this image has no
WiFi class driver:

```text
[I/usbh_hub] New full-speed device on Bus 0, Hub 1, Port 1 connected
[I/usbh_core] New device found,idVendor:0bda,idProduct:8179,bcdDevice:0000
[I/usbh_core] The device has 1 interfaces
[E/usbh_core] Do not support Class:0xff, Subclass:0xff, Protocl:0xff on interface 0
```

The `mcu` expectation on `idVendor:0bda,idProduct:8179` matches; the later
`usbh: msc FAIL no mass-storage instance` is the MSC-specific case refusing a
non-storage device, not an enumeration failure.  No rtl8xxxu/mac80211/linuxkpi
USB path is loaded; the vendor interface is deliberately unclaimed.  Run:
the 2026-11-15 bench run.

### USB3 hub (2026-11-15)

A USB3 hub (`05e3:0610`) on the FS host enumerates, and its two devices are
visible: RTL8188EUS `0bda:8179` on hub port 2 (class 0xff, unsupported) and
U disk `05e3:0749` on hub port 4.  In a USB_DBG_LOG diagnostic boot the U disk
reads block 0 and eight blocks through the hub, then the 512 B WRITE10 stops
after the CBW with no CSW/PASS.  The same code and disk direct on P2 with the
normal INFO image pass 2/2, so the direct link is not the issue.  The hub is
**not** an accepted *storage* topology yet; the leftover is the hub-path
bulk-OUT or power question.  (A later boot on the same day showed that a
**self-powered** hub changes the picture for the WiFi path: four dongles
enumerate and the 8188EUS passes the data-plane gate through the hub - next section.)  Run:
the 2026-11-15 bench run.

### Four wireless dongles behind a self-powered hub (2026-11-15 evening)

The WiFi image (`stm32f429-wifi-c6`) with a self-powered 4-port hub
(`05e3:0610`) on P2 and four Realtek dongles on its ports.  All four enumerate
at the USB layer, and the 8188EUS runs the whole data plane through the
hub (the AP's own capture has 44 echo requests + 44 replies).  One boot, after a relay cold boot:

```text
[   3.913] f429_wifi: wlan0 present=1 after=2900 ms
[   3.920] f429_wifi: wlan0 present=1 ifindex=1 mac=20:f4:1b:52:8f:01
[   4.285] f429_wifi: PASS SCAN
[   6.688] f429_wifi: wlan0: associated
[   6.896] f429_wifi: PASS ASSOC
[  10.000] f429_wifi: ping small tx=20 rx=20 payload=56 rtt_ms=3/5/11 rc=0
[  10.960] f429_wifi: ping big tx=20 rx=20 payload=1400 rtt_ms=6/8/25 rc=0
[  10.986] f429_wifi: PASS DATA
[  10.996] f429_wifi: usb submits=170 completes=169 refused=0 stage_overflow=0 slots_exhausted=0 stage_size=4096 slots=4 rx=121 rx_err=0 tx=48 tx_err=0 tx_last_err=0 tx_len=1494
[  11.018] rtos: result pass cases=1/1 backend=freertos
```

AP-side ledger for the same boot (`tcpdump -i wlan2 icmp`): 44 echo requests
and 44 echo replies, from/to `20:f4:1b:52:8f:01`, payload split 3 x 16 B
(warmup), 21 x 64 B, 20 x 1408 B - exactly the case's traffic. The MT7921AU
monitor on this bench shows none of those frames (0 packets with the board's
MAC), so the AP kernel's capture is the air truth for this line, not the
monitor.

The wedge's mechanism, named by the tick-PC sampler (`CONFIG_F429_TICK_PC`,
polled UART): every sample during the freeze sat in `print_try_lock`
(`platform/os/baremetal/log.c:147`) while the tick kept advancing - a
console-lock spin, not a fault.

```
tickpc: tick=000370dc pc=0x08000a7c
tickpc: tick=000372d0 pc=0x08000a86
...
```

Census instrumentation that carried the "chip state is normal" half of the
proof (`CONFIG_RTL8XXXU_TX_CENSUS`, default n):

```
rtl8xxxu_diag: h2c n=0 macid=0 role=2 conn=1 hmtfr_before=0x00 hmtfr_after=0x00 next=1 mbox0=0x00002101 ...
rtl8xxxu_diag: chip tag=post-assoc out_eps=2 ep_tx=2 hmtfr=0x00 txpause=0x00 cr=0x06ff fwhw_txq=0x7f301f80 trxdma=0xfaf0 rqpn=0x00630029 rqpn_npq=0x00001c1c txrpt=0x33 hisr=0x00000000 hisre=0x00000000
```

Full record: the 2026-11-15 bench run.

### STM32F429 wi-fi data-plane throughput (c6t image, same AP)

The `stm32f429-wifi-c6t` image runs the four report-only phases after the data plane
gate in the same boot (the supported sample; the console's phase
timestamps are not monotonic - read the order and the `bytes`/`ms`/`kbit`
fields, not the clock):

```
[   9.644] f429_wifi: iperf server start rc=0 port=5001
[  13.170] f429_wifi: phase tcpdn rc=0 done=1 bytes=9043992 ms=15027 kbit=4808
[   7.493] f429_wifi: phase tcpup rc=0 done=1 bytes=4607784 ms=10002 kbit=3680
[   2.929] f429_wifi: phase udpdn rc=0 done=1 bytes=15508500 ms=20283 kbit=6116 pkts=10550 drop=0
[  15.627] f429_wifi: phase udpup rc=0 done=1 offered=241080 bytes=241080 ms=12000 kbit=160
[  15.639] f429_wifi: throughput phases moved=4/4
[  15.681] rtos: result pass cases=1/1 backend=freertos
```

The peer's view of the same phases (Pi, iperf 2.2.1 and the UDP sink) is the
cross-check that makes these numbers end-to-end rather than the board's own
accounting: tcpdn **2.84 Mbit/s** (8.63 MB / 25.5 s), tcpup **1.85 Mbit/s**
(4.39 MB / 19.9 s), udpdn 14.7 MB offered / 6.86 Mbit/s with the board
counting 10,550 packets and 0 drops. The AP's `tcpdump -i wlan2` in the same
boot carries the whole round (44 ICMP requests + 44 replies, then the TCP/UDP
flows).  Full record: the 2026-11-15 bench run.

### STM32F429 wi-fi: serial phenomena worth recognising

Four console signatures that each cost a round; recognise them before
re-deriving the diagnosis:

1. **`rx_complete status -5` on every bulk IN (`rx_err=32`)** - the MCU was
   reset while the 8188EUS was left in a stuck bulk-IN state. The next boot's
   scan/association fail (`FAIL bss timeout`), and the *known-good* c5 image
   fails the same way: this is the dongle, not the image. An MCU reset does
   not power-cycle VBUS, so the recovery is a board power cycle (or replugging
   the dongle), not another reset.
2. **The print-lock wedge**: the console stops **mid-line** (a partial
   `[   7.` with no newline), there is **no `fatal:` line**, the periodic
   heartbeat keeps printing, and the tick-PC sampler shows every sample in
   `print_try_lock` - the console lock's holder was starved by a higher
   priority printer (fixed in `platform/os/baremetal/log.c`).
3. **`tickpc: tick=… pc=0x…`** - the polled-UART PC sampler
   (`CONFIG_F429_TICK_PC`, diagnostic only). It prints every 500 ticks and
   deliberately does not take the console lock, so it survives the state that
   silences everything else; symbolize the PCs against the image's ELF.
4. **`hb3 n=… R=… D=… L=- s=… c=… x=… t=…`** - the case's polled-UART
   heartbeat (also lock-free). `L=-` means the lwIP core lock is not held by
   anyone; `s`/`c` are submit/complete counts, `x`/`t` the RX/TX frame counts.
   It is the instrument that says "the CPU is alive" while the console is
   frozen, so it disambiguates a wedge from a dead board.

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
  five-minute monitor soak are untested.  The monitor transcripts (AC 7260 and
  RTL8851BU) are receive only.
- **The 8851BU monitor rate is a cadence check, not a GT parity claim.**  The
  two boots see 10.7 beacons/s against the AP's 100 TU (9.77/s); there is no
  same-card Linux monitor arm this round, and the 10 frames on 2412 are the
  default channel before the request landed.
- **The 8851BU AP table's GT column is a fallback denominator.**  The
  same-ruler GT arm failed three times on instruments; the TCP column is
  `gt-ap5` and the UDP-uplink column is the `gt-ap1`+`gt-ap5` mean.  The TCP
  uplink is 78.4% and fails the gate; the three non-UDP-uplink cells scatter
  11-16%, so the table reports means and ranges, not stable rates.  The an earlier A4 boot
  silence's mechanism is unproven and the invalid one invalid boot boot is not part
  of the 2/2.
- **The 8851BU station downlink is below the gate.**  The C6T cells are
  33.30/30.51 (mean 31.90) against 42.3, 75.4%; the 40 M UDP downlink has no
  same-offer GT cell, so only its 0% loss is judged, and the UDP uplink is
  void for the instrument reason above.
- **Power-save buffering.**  `ps_buffered` in the AP transcript is the driver's
  own ledger; there is no `SET_POWER_SAVE` A/B behind it.
- **The GT arm as a control for anything but throughput and the monitor frame
  rate.**  Its job is to be the same card on a mainline kernel; it is not a
  second measurement of the port.
- **The 8188EU/8723BU throughput is a port-vs-GT comparison, not a chip
  ceiling.**  The 8723's board-receive TCP is 62.5% of the GT, but the GT's
  own `tcpdn` scatter is 21.4/28.7 and decays within the window; the gap is
  the EHCI receive path/RX aggregation candidate, not a proven card limit.
  The 8188 AP's UDP receive (52%, 12-15% loss) and both roles' board-send
  TCP (14-17%) are open mechanisms, not alignment.
- **8723BU AP and monitor are unverified.**  The chip's fileops carries
  `supports_ap` in the pin, but neither role was run; the monitor role and
  both cards' WPA2/multi-client/5 GHz paths are not measured here.  The
  8723 one boot stop (1/3) and the repeat `tcpdn2` 0 bytes (3/3) are open
  defects, listed above, not rate cells.
- **Compliance.**  There is no `regulatory.db` in this image, so the wiphy runs
  on the world regdomain.  The channel flags above are the driver's
  `orig_flags`, not a country's rules.
- **The F429's four throughput cells are samples of one harness, not a table.**
  The downlink TCP rate is stable across the two boots (2.78 / 2.84 Mbit/s),
  but the uplink TCP (0.36 vs 1.85) and the UDP blast (0.16 vs 0.71 Mbit/s
  offered) are not, and neither is explained.  The board's USB host is full
  speed (12 Mbit/s signalling, bulk ceiling near 9.7), so the 6-7 Mbit/s UDP
  cell is the USB link being approached, not the radio; none of the four
  numbers transfers to the RK3568 line or a GT arm.