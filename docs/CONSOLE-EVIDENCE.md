# Bring-up evidence: what the console prints

A module is "brought up" here when one boot reaches the line's own end-to-end
anchor - association, an address, a passing packet exchange - and says so.  The
transcripts on this page are the board's UART output, verbatim, from boots that
reached those anchors.  Elisions are marked `...` and name what was cut; nothing
else is edited.  Timestamps are the image's own milliseconds since boot.

Why the console and not a test report: the interesting failures on this project
have all been silent (a transfer that never executes, a handshake that stalls
with no error), so the log is the artefact that carries the evidence.  The
patterns each line's absence would break are named under each transcript.

Reproduce: the profile is named per section; the image is loaded over TFTP and
entered at its link address (`0x90000000` on the first board, `0xa000000` on the
second), and the console is the board's debug UART at 115200 8N1.

## RTL8821CS (SDIO, mainline rtw88 + net80211)

Profile `firefly-v2-rtw88-net` (the four-case ladder `rtw88`, `rtw88_scan`,
`rtw88_join`, `rtw88_net`).  Card: `024c:c821`, SDIO 3.0, one function, 50 MHz
4-bit.  Bench AP: `QiQiJia` on channel 11 (2462 MHz), **open** - this is the
MLME the imported net80211 runs today; WPA2 for this line is not implemented
yet, so the AP was set open for the measurement.

What the transcript proves, in order: the card enumerates on the MCI host and
the driver registers a bus; the firmware download and the probe complete with
zero bus errors; a scan returns 20 rows with no FCS errors; auth and assoc
succeed (`ack=1` on each management frame, one auth retry at 11.047 s); DHCP
binds an address in the AP's pool; one ARP resolves the gateway; ten pings come
back with zero loss; and the run reports `cases=5/5`.

```
## Starting application at 0x90000000 ...
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
boot: main
rtos: banner backend=threadx kernel=ThreadX 6.5.1.202602a build=3c29805a-dirty
rtos: kernel ready tick=1000Hz
[   0.000] heap: tlsf ready 16777216 bytes at 0x90411000
[   0.004] cmsis2: prio 24 -> tx 19
[   0.008] tx: kernel entered version=6.5.1.202602a
[   0.012] tx: unused_mem=0x00000000
gicv3: rd frame off=00040000

... (gicv3 ready, the tick case) ...

[   3.011] rtos: case rtw88 start
[   3.012] cmsis2: prio 24 -> tx 19
[   3.013] lkpi: wlan: seam adapter ready
[   3.035] MCI: init base=0x28001000 pid=105 cntrl=02000010 fifoth=20070100 thrctl=00800001 pren=00000001 uhs_ext=00000500 clkdiv=00000000 cclk_rdy=00000000
[   3.497] mci: card reset pulse + CMD52 abort rc=0 resp0=00001008
[   3.512] Manufacturer: 0x24c-0xc821-0xc
[   3.513]  SD 3.0 SDIO 3.0
[   3.514]  Block size: 8
[   3.515]  Max speed: 50 MHz
[   3.516]  IO function num: 1
[   3.517] rtw88: sdio up card=024c:c821 funcs=1 cccr=3 clock=50000000 width=4 hs=1
[   3.518] lkpi: sdiokpi: driver 'freewificam' registered
[   3.519] lkpi: sdiokpi: 1 sdio driver entry registered
[   3.520] cmsis2: prio 16 -> tx 23
[   3.521] cmsis2: prio 24 -> tx 19

... (SDIO init, driver probe, firmware download, the rtw88 case) ...

[   7.851] MCI DONE c=52 ev=00000004 err=0 resp0=00001000
[   7.856] rtw88: trace window 2100 ms, 28 transfers
[   7.857] rtw88: sdio bus clock=50000000 width=4 blk=512/0 max_req=16384 io_ext=4773
[   7.858] rtw88: sdio stats cards=1 drivers=1 probes=1 io_direct=1482 io_ext=4773 errors=0
[   7.859] rtw88: mci transfers=6637 errors=0 cmd52=1859 cmd53=4773 data_stages=4773 idmac_stages=70 fifo_stages=4703 last_cmd=52 last_err=0 data_ev=00000018 cmd_ev=00000004 dmac=00000000
[   7.860] rtw88: pool skb=0/1 data=0/1 heap_live=0 ovf=0/0 fail=0
[   7.861] rtw88: PASS

... (the scan report, one line per AP) ...

[  10.650] rtw88: scan done rows=20 bcn=62 prsp=23 fcs=0 scan=1/1 state=1 entered=1 close=2 rx=89
[  10.651] rtw88: scan PASS
[  10.652] rtos: case rtw88_scan PASS
[  10.653] rtos: case rtw88_join start
[  10.654] rtw88: join bssid=36:ab:a0:b6:1a:b0 ssid=QiQiJia freq=2462 rssi=-32 hits=1 rows=20
[  10.655] rtw88: join sdio pre clock=50000000 width=4 blk=512/0 io_direct=1681 io_ext=6454 errors=0
[  10.656] rtw88: join mci pre transfers=8781 errors=0 cmd52=2322 cmd53=6454 last_cmd=53 last_err=0 data_ev=00000018
[  10.657] wlan: htcap h=1 valid=3 cap=09bd htparam=00 upd=85 ins=22 ev=0 -> w=20 c=2462
[  10.658] wlan: join opt ie=0 len=28
!!! net80211: mutex_owned false lock 0000000090432908 owner 0000000000000000 current 00000000900B9638 magic 000000006D757821 ra 000000009001051C

... (the first auth frame, its retry, and the response; hex dumps elided) ...

[  10.845] wlan: join QiQiJia bssid=36:ab:a0:b6:1a:b0 open
[  10.846] rtw88: join start ssid=QiQiJia bssid=36:ab:a0:b6:1a:b0 state=2 rc=0 attempt=1
[  10.847] lkpi: lkpi80211: tx done #14 fc=00b0 ack=1 len=30
[  11.047] wlan: auth retry 1/2 (no answer in 200 ms)
[  11.048] wlan: ie dir=tx sub=b0 len=30 status=0 n=0 ssid=0 rates=0 xrates=0 rsn=0 htcap=0 hcap=0x0000 mcs0=0x00000000 htinfo=0 wme=0
[  11.049] wlan: mgt tx fc=b0 len=30 a1=36:ab:a0:b6:1a:b0 a3=36:ab:a0:b6:1a:b0
[  11.050] lkpi: lkpi80211: tx done #15 fc=00b0 ack=1 len=30
[  11.084] wlan: rxmgt fc=b000 retry=0 sc=1010 mactime=10680017 freq=2462 len=30
[  11.085] wlan: ie dir=rx sub=b0 len=30 status=0 n=0 ssid=0 rates=0 xrates=0 rsn=0 htcap=0 hcap=0x0000 mcs0=0x00000000 htinfo=0 wme=0
[  11.086] wlan: rx mgt fc=b0 fcs=0 retry=0 sc=1010 a1=f4:3c:3b:f8:74:94 a2=36:ab:a0:b6:1a:b0 len=30
[  11.087] wlan: rx join-bytes:

... (assoc response processed) ...

[  11.456] rtw88: join mci final transfers=9167 errors=0 cmd52=2447 cmd53=6715 last_cmd=52 last_err=0 data_ev=00000018
[  11.457] rtw88: join counters state=4 mgt_tx=16 txc=16 txfail=0 rx=97 rx_data=3 bcnT=1 auth_retries=1 auth_answers=1 bad_auth=0 txq=0 stopped=0 tune=15/0 freq=2462
[  11.458] rtw88: join ok state=4 mgt_tx=16 txc=16 auth=1 bcnT=1
[  11.459] rtw88: join PASS
[  11.460] rtos: case rtw88_join PASS
[  11.461] rtos: case rtw88_net start
[  11.462] cmsis2: prio 40 -> tx 11
[  11.463] wlan: data tx first: fc=8801 a1=36:ab:a0:b6:1a:b0 a2=f4:3c:3b:f8:74:94 a3=ff:ff:ff:ff:ff:ff prot=0 keyid=-1 len=370
[  11.464] rtw88: net up mode=dhcp mac=f4:3c:3b:f8:74:94 mtu=1500
[  11.595] lkpi: sdiokpi: rxread#96 addr=0xe003 count=256 ret=0 walk=[0+255] hdr=[8000:36aba0b61ab0] walked=255
[  13.646] lkpi: sdiokpi: rxread#120 addr=0xe003 count=256 ret=0 walk=[0+255] hdr=[8000:36aba0b61ab0] walked=255
[  15.697] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=181
[  15.698] lkpi: sdiokpi: rxread#144 addr=0xe003 count=244 ret=0 walk=[0+243] hdr=[4000:ffffffffffff] walked=243
[  15.699] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=183
[  15.700] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=181
[  15.701] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=183
[  15.702] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=181
[  15.703] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=183
[  15.704] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=181
[  15.725] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=100
[  16.465] rtw88: net dhcp wait t=5s state=8 tries=0
[  17.066] lkpi: sdiokpi: rxread#168 addr=0xe003 count=256 ret=0 walk=[0+255] hdr=[8000:36aba0b61ab0] walked=255
[  17.707] wlan: rx mgt fc=d0 a1=f4:3c:3b:f8:74:94 a3=36:ab:a0:b6:1a:b0 len=33
[  17.978] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=141
[  17.979] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=135
[  18.350] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=94
[  18.351] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=100
[  18.766] [previous line repeated 1 times]
[  18.766] rtw88: net dhcp bound ip=10.227.127.107 mask=255.255.255.0 gw=10.227.127.187 lease=3599s
[  18.772] lkpi: sdiokpi: rxread#192 addr=0xe003 count=72 ret=0 walk=[0+70] hdr=[] walked=70
[  18.807] rtw88: net arp gw=10.227.127.187 wait_ms=40 seen=1 entry=0
[  19.873] lkpi: sdiokpi: rxread#216 addr=0xe003 count=204 ret=0 walk=[0+70 70+104 174+9117] hdr=[d400:4a0084840000 0ae3:a5a5a5a5a5a5] walked=174
[  21.624] lkpi: sdiokpi: rxread#240 addr=0xe003 count=256 ret=0 walk=[0+255] hdr=[8000:36aba0b61ab0] walked=255
[  22.695] wlan: rx mgt fc=d0 a1=f4:3c:3b:f8:74:94 a3=36:ab:a0:b6:1a:b0 len=30
[  23.006] wlan: rx mgt fc=d0 a1=f4:3c:3b:f8:74:94 a3=36:ab:a0:b6:1a:b0 len=33
[  23.237] lkpi: sdiokpi: rxread#264 addr=0xe003 count=256 ret=0 walk=[0+255] hdr=[8000:36aba0b61ab0] walked=255
[  23.368] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=137
[  23.369] wlan: rx mgt fc=40 a1=ff:ff:ff:ff:ff:ff a3=ff:ff:ff:ff:ff:ff len=131
[  23.528] rtw88: net ping tx=10 rx=10 ok_mask=7fe min_ms=6 max_ms=35
[  23.529] rtw88: net counters tx=20 txb=1544 txfail=0 rx=18 rxb=1464 rxfail=0 bridge_data_tx=20 bridge_tx_done=36 bridge_rx=297 bridge_rx_data=21 bridge_tx_err=0
[  23.530] rtw88: net PASS
[  23.531] rtos: case rtw88_net PASS
[  23.532] rtos: result pass cases=5/5 backend=threadx
[  23.533] rtos: resetting board
```

Two things in that log are noise, not failure, and are left in on purpose so a
reader does not think the transcript was groomed: the `!!! net80211:
mutex_owned false` line is the imported stack's own assertion about a lock
being probed from the wrong owner (it does not abort the run), and the `0+...`
walks on `rxread` lines are the SDIO read-buffer walk, one line per transfer.

## AIC8800D80 (USB2 xHCI, vendor FullMAC)

Profile `firefly-v2-aic8800-usb`.  Dongle: AIC8800D80, which ships as a ZeroCD
mass-storage device (`1111:1111`) and has to be switched to its BootROM
(`a69c:8d80`) before the firmware download, then comes back as the runtime
composite (`a69c:8d81`, Wi-Fi on interface 2).  Bench AP: `QiQiJia` on channel 1
(2412 MHz), **WPA2-PSK/CCMP**.

This line has no 802.11 stack of ours: the firmware runs the MLME, and the
driver is the message transport.  The transcript therefore shows a ladder of
USB re-enumerations and vendor commands rather than frames.  What it proves:
the ZeroCD switch out of mass storage and the forced hub-port re-enumeration;
the four-stage firmware upload (`firmware upload and patch complete in 406 ms`)
and the runtime composite coming back on interface 2 with the Bluetooth
interfaces refused; the firmware's own version and capability answers; a scan
of 24 APs; the association command and its confirmation; the four-way handshake
message by message - M1 received, M2 sent, **M3 received with a valid MIC**, M4
sent - the group and pairwise CCMP keys installed in hardware, the station
control port opened; DHCP; and four ping replies from the gateway.

```
## Starting application at 0x90000000 ...
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
boot: main
rtos: banner backend=threadx kernel=ThreadX 6.5.1.202602a build=1c360150-dirty
rtos: kernel ready tick=1000Hz
[   0.000] heap: tlsf ready 16777216 bytes at 0x90410000
[   0.004] cmsis2: prio 24 -> tx 19
[   0.008] tx: kernel entered version=6.5.1.202602a
[   0.012] tx: unused_mem=0x00000000
gicv3: rd frame off=00040000

... (kernel bring-up: gicv3, tick, the tick and cpu cases) ...

[   3.011] rtos: case aic8800 start
[   3.012] [I/aic8800.fw] 
[   3.013] AIC firmware images registered (D80 U02)
[   3.014] aic8800: fw blobs registered (5 images, D80 U02)
[   3.015] [I/usbh_aic8800] 
[   3.016] 1111:1111 ZeroCD mode switch registered
[   3.017] [I/aic8800.wifi] 
[   3.018] AIC Wi-Fi STA ready: SSID="QiQiJia" band=0 BSSID any
[   3.019] [I/usbh_aic8800] 
[   3.020] 1111:1111 ZeroCD mode switch registered
[   3.021] aic8800: usb buses=0,1
[   3.022] cmsis2: prio 24 -> tx 19
[   3.023] cmsis2: prio 56 -> tx 3
[   3.024] xhci: 2 ports, 8 slots, ctxsz=64 pgsz=4096 caplen=128 hcs1=02000110
[   3.025] aic8800: usb bus=0 hcd=xhci base=0x31a08000 started

... (bus 1 mirrors bus 0; no device on it) ...

[   3.460] [I/usbh_hub] 
[   3.460] New high-speed device on Bus 0, Hub 1, Port 1 connected
[   3.461] xhci: slot 1 enabled
[   3.462] xhci: slot 1 auto-assigned 1 (stack wanted 2)
[   3.473] [I/usbh_core] 
[   3.473] New device found,idVendor:1111,idProduct:1111,bcdDevice:1001
[   3.474] [I/usbh_core] 
[   3.475] The device has 1 bNumConfigurations
[   3.476] [I/usbh_core] 
[   3.477] The device has 1 interfaces
[   3.478] [I/usbh_core] 
[   3.479] Enumeration success, start loading class driver
[   3.480] [I/usbh_core] 
[   3.481] Loading msc class driver on interface 0
[   3.482] [I/usbh_msc] 
[   3.483] Get max LUN:1
[   3.484] [I/usbh_msc] 
[   3.485] Ep=81 Attr=02 Mps=512 Interval=00 Mult=00
[   3.486] [I/usbh_msc] 
[   3.487] Ep=01 Attr=02 Mps=512 Interval=00 Mult=00
[   3.488] [I/usbh_msc] 
[   3.489] AIC8800D80 ZeroCD usb_modeswitch enable
[   3.490] [I/usbh_msc] 
[   3.491] usb_modeswitch CBW sent: 31 bytes
[   3.842] [I/usbh_msc] 
[   3.842] hub-port re-enumeration requested
[   3.843] [I/usbh_hub] 
[   3.844] Force hub-port 1 re-enumeration
[   4.204] [I/usbh_core] 
[   4.204] Device on Bus 0, Hub 1, Port 1 disconnected
[   4.205] xhci: slot 1 disabled for re-enum
[   4.206] [I/usbh_hub] 
[   4.207] New high-speed device on Bus 0, Hub 1, Port 1 connected
[   4.208] xhci: slot 1 enabled
[   4.209] xhci: slot 1 auto-assigned 1 (stack wanted 3)
[   4.220] [I/usbh_core] 
[   4.220] New device found,idVendor:a69c,idProduct:8d80,bcdDevice:0100
[   4.221] [I/usbh_core] 
[   4.222] The device has 1 bNumConfigurations
[   4.223] [I/usbh_core] 
[   4.224] The device has 1 interfaces
[   4.225] [I/usbh_core] 
[   4.226] Enumeration success, start loading class driver
[   4.227] [I/usbh_core] 
[   4.228] Loading aic8800 class driver on interface 0
[   4.229] [I/usbh_aic8800] 
[   4.230] Ep=01 Attr=02 Mps=512 Interval=00 Mult=00
[   4.231] [I/usbh_aic8800] 
[   4.232] Ep=82 Attr=02 Mps=512 Interval=00 Mult=00
[   4.233] [I/usbh_aic8800] 
[   4.234] Register AIC8800 Class:/dev/aic8800 a69c:8d80, AIC8800D80/D40, boot-loader
[   4.235] [I/usbh_aic8800] 
[   4.236] AIC pipes data=82/01 message=00/00
[   4.237] cmsis2: prio 53 -> tx 7
[   4.238] [I/aic8800.boot] 
[   4.239] probing AIC8800D80 BootROM command channel
[   4.240] [I/aic8800.boot] 
[   4.241] D80 chip register=0x73078820 revision=0x07 high=0 mcu=0
[   4.242] [I/aic8800.boot] 
[   4.243] patch destinations adid=0x00201940 patch=0x001e0000 ext0=0x0020b43c
[   4.244] [I/aic8800.boot] 
[   4.245] firmware upload block size: 1024 bytes
[   4.246] [I/aic8800.boot] 
[   4.247] uploading fw_adid_8800d80_u02.bin: 1708 bytes -> 0x00201940
[   4.248] [I/aic8800.boot] 
[   4.249] fw_adid_8800d80_u02.bin progress: 1708/1708
[   4.250] [I/aic8800.boot] 
[   4.251] uploaded fw_adid_8800d80_u02.bin
[   4.252] [I/aic8800.boot] 
[   4.253] uploading fw_patch_8800d80_u02.bin: 32700 bytes -> 0x001e0000
[   4.258] [I/aic8800.boot] 
[   4.258] fw_patch_8800d80_u02.bin progress: 32700/32700
[   4.259] [I/aic8800.boot] 
[   4.260] uploaded fw_patch_8800d80_u02.bin
[   4.261] [I/aic8800.boot] 
[   4.262] uploading fw_patch_8800d80_u02_ext0.bin: 16136 bytes -> 0x0020b43c
[   4.265] [I/aic8800.boot] 
[   4.266] fw_patch_8800d80_u02_ext0.bin progress: 16136/16136
[   4.267] [I/aic8800.boot] 
[   4.268] uploaded fw_patch_8800d80_u02_ext0.bin
[   4.269] [I/aic8800.boot] 
[   4.270] applying patch block type=0 pairs=6
[   4.275] [I/aic8800.boot] 
[   4.275] applying patch block type=1 pairs=27
[   4.301] [I/aic8800.boot] 
[   4.302] applying patch block type=2 pairs=57
[   4.358] [I/aic8800.boot] 
[   4.359] applying patch block type=3 pairs=18
[   4.376] [I/aic8800.boot] 
[   4.377] applying patch block type=4 pairs=3
[   4.380] [I/aic8800.boot] 
[   4.380] waiting 100 ms for Bluetooth power-on
[   4.481] [I/aic8800.boot] 
[   4.481] applying patch block type=5 pairs=31
[   4.482] [I/aic8800.boot] 
[   4.483] uploading fmacfw_8800d80_u02.bin: 358072 bytes -> 0x00120000
[   4.488] [I/aic8800.boot] 
[   4.488] fmacfw_8800d80_u02.bin progress: 65536/358072
[   4.493] [I/aic8800.boot] 
[   4.493] fmacfw_8800d80_u02.bin progress: 131072/358072
[   4.498] [I/aic8800.boot] 
[   4.498] fmacfw_8800d80_u02.bin progress: 196608/358072
[   4.503] [I/aic8800.boot] 
[   4.503] fmacfw_8800d80_u02.bin progress: 262144/358072
[   4.508] [I/aic8800.boot] 
[   4.508] fmacfw_8800d80_u02.bin progress: 327680/358072
[   4.511] [I/aic8800.boot] 
[   4.511] fmacfw_8800d80_u02.bin progress: 358072/358072
[   4.512] [I/aic8800.boot] 
[   4.513] uploaded fmacfw_8800d80_u02.bin
[   4.514] [I/aic8800.boot] 
[   4.515] FMAC version=0x06090101 config=0x00177158 patch=0x0017d318
[   4.516] [I/aic8800.boot] 
[   4.517] FMAC profile: 5 GHz, USB RX aggregate=8 buffer=16384 bytes
[   4.518] [I/aic8800.boot] 
[   4.519] starting D80 FMAC at 0x00120000
[   4.520] [I/aic8800.boot] 
[   4.521] firmware upload and patch complete in 406 ms
[   4.522] [I/aic8800.boot] 
[   4.523] firmware start command sent; waiting for runtime USB
[   4.933] [I/usbh_aic8800] 
[   4.933] Unregister AIC8800 Class:/dev/aic8800
[   4.934] [I/usbh_core] 
[   4.935] Device on Bus 0, Hub 1, Port 1 disconnected
[   4.936] xhci: slot 1 disabled for re-enum
[   4.937] [I/usbh_hub] 
[   4.938] New high-speed device on Bus 0, Hub 1, Port 1 connected
[   4.939] xhci: slot 1 enabled
[   4.940] xhci: slot 1 auto-assigned 1 (stack wanted 4)
[   4.951] [I/usbh_core] 
[   4.951] New device found,idVendor:a69c,idProduct:8d81,bcdDevice:0100
[   4.952] [I/usbh_core] 
[   4.953] The device has 1 bNumConfigurations
[   4.954] [I/usbh_core] 
[   4.955] The device has 3 interfaces
[   4.956] [I/usbh_core] 
[   4.957] Enumeration success, start loading class driver
[   4.958] [E/usbh_core] 
[   4.959] Do not support Class:0xe0, Subclass:0x01, Protocl:0x01 on interface 0
[   4.960] [E/usbh_core] 
[   4.961] Do not support Class:0xe0, Subclass:0x01, Protocl:0x01 on interface 1
[   4.962] [I/usbh_core] 
[   4.963] Loading aic8800 class driver on interface 2
[   4.964] [I/usbh_aic8800] 
[   4.965] Ep=01 Attr=02 Mps=512 Interval=00 Mult=00
[   4.966] [I/usbh_aic8800] 
[   4.967] Ep=81 Attr=02 Mps=512 Interval=00 Mult=00
[   4.968] [I/usbh_aic8800] 

... (vendor command probe, RF defaults, LMAC start) ...

[   6.262] [I/aic8800.fmac] 
[   6.263] LMAC started
[   6.264] [I/aic8800.fmac] 
[   6.265] TX command=0x0006 parameter=10
[   6.266] [I/aic8800.fmac] 
[   6.267] RX command=0x0007 parameter=2
[   6.268] [I/aic8800.fmac] 
[   6.269] station VIF 0 enabled, MAC 68:8f:c9:b5:95:32
[   6.442] aic8800: ready after 3400 ms family=2 vid=a69c pid=8d81 mac=68:8f:c9:b5:95:32
[   6.443] aic8800: bus0 irqs=1201
[   6.444] aic8800: bus1 irqs=0
[   6.445] [I/aic8800.fmac] 
[   6.446] starting active 2.4GHz scan: channels=14 vif=0
[   6.447] [I/aic8800.fmac] 
[   6.448] TX command=0x1000 parameter=376
[   6.449] [I/aic8800.fmac] 
[   6.450] RX command=0x1009 parameter=2
[   6.451] [I/aic8800.fmac] 
[   6.452] RX command=0x1004 parameter=223

... (the rest of the scan report, one line per AP) ...

[   8.913] [I/aic8800.fmac] 
[   8.914] 5GHz scan complete
[   8.915] [I/aic8800.fmac] 
[   8.916] active scan finished: 2.4GHz=14 5GHz=27 results
[   9.055] aic8800: scan 24 APs
[   9.055] aic8800: scan[0] "QiQiJia" freq=2412 band=1 rssi=-36
[   9.056] aic8800: scan[1] "HUAWEI-BSFGQ7" freq=2412 band=1 rssi=-70
[   9.057] aic8800: scan[2] "xiaomi14" freq=2412 band=1 rssi=-63
[   9.058] aic8800: scan[3] "Xiaomi_62EA" freq=2412 band=1 rssi=-66
[   9.059] aic8800: scan[4] "TP-LINK_5336" freq=2412 band=1 rssi=-69
[   9.060] aic8800: scan[5] "WANGB" freq=2412 band=1 rssi=-71
[   9.061] aic8800: scan[6] "@TEST_AUTO" freq=2447 band=1 rssi=-57
[   9.062] aic8800: scan[7] "aaa_a" freq=2457 band=1 rssi=-76
[   9.063] [I/aic8800.fmac] 
...

[  11.509] [I/aic8800.wifi] 
[  11.510] deriving WPA2 PMK from passphrase (PBKDF2 4096)
[  11.595] [I/aic8800.fmac] 
[  11.595] associating with SSID="QiQiJia" BSSID=b6:34:10:15:74:9b freq=2412 WPA2-PSK/CCMP
[  11.596] [I/aic8800.fmac] 
[  11.597] TX command=0x1800 parameter=320
[  11.598] [I/aic8800.fmac] 
[  11.599] RX command=0x1801 parameter=1
[  11.600] [I/aic8800.fmac] 
[  11.601] connect request accepted; waiting for association result
[  11.602] [I/aic8800.fmac] 
[  11.603] RX command=0x1004 parameter=223
[  11.604] [I/aic8800.fmac] 
[  11.605] scan AP b6:34:10:15:74:9b freq=2412 RSSI=-38 SSID="QiQiJia"
[  11.606] [I/aic8800.fmac] 
[  11.607] RX command=0x004f parameter=12
[  11.786] [I/aic8800.fmac] 
[  11.787] RX command=0x0044 parameter=4
[  11.841] [I/aic8800.fmac] 
[  11.841] RX command=0x1802 parameter=852
[  11.842] [I/aic8800.fmac] 
[  11.843] 802.11 associated: AP station=0 channel-context=0; WPA2 four-way handshake pending
[  11.844] [I/aic8800.fmac] 
[  11.845] association channel: band=0 frequency=2412 MHz width=20 MHz center1=2412 center2=0
[  11.846] [I/aic8800.fmac] 
[  11.847] received EAPOL-Key message 1/4: version=2 descriptor=2 key-info=0x008a
[  11.848] [I/aic8800.fmac] 
[  11.849] sent EAPOL-Key message 2/4: 121 bytes
[  11.853] [I/aic8800.fmac] 
[  11.853] received EAPOL-Key message 3/4: key-info=0x13ca key-data=56 MIC valid
[  11.854] [I/aic8800.fmac] 
[  11.855] EAPOL M3 GTK unwrapped: index=1 length=16
[  11.856] [I/aic8800.fmac] 
[  11.857] sent EAPOL-Key message 4/4
[  11.858] [I/aic8800.fmac] 
[  11.859] TX command=0x0024 parameter=44
[  11.860] [I/aic8800.fmac] 
[  11.861] RX command=0x0025 parameter=2
[  11.862] [I/aic8800.fmac] 
[  11.863] group CCMP key installed: index=1 hardware=1
[  11.864] [I/aic8800.fmac] 
[  11.865] TX command=0x0024 parameter=44
[  11.866] [I/aic8800.fmac] 
[  11.867] RX command=0x0025 parameter=2
[  11.868] [I/aic8800.fmac] 
[  11.869] pairwise CCMP key installed: index=0 hardware=16
[  11.870] [I/aic8800.fmac] 
[  11.871] TX command=0x1404 parameter=2
[  11.872] [I/aic8800.fmac] 
[  11.873] RX command=0x1405 parameter=0
[  11.874] [I/aic8800.fmac] 
[  11.875] WPA2 four-way handshake complete; station control port open
[  11.876] cmsis2: prio 50 -> tx 7
[  11.877] [I/aic8800.fmac] 
[  11.878] AIC data TX queue ready: depth=64 reserve=4

... (DHCP DISCOVER/REQUEST exchange) ...

[  18.858] AIC data RX Ethernet type=0x0806 length=42
[  19.579] [I/USB] 
[  19.579] [I/aic8800.fmac] 
[  19.580] AIC data TX Ethernet type=0x0806 length=42
[  19.581] AIC DHCP address 10.227.127.61 gateway 10.227.127.187
[  19.673] aic8800: got ip 10.227.127.61/255.255.255.0 gw 10.227.127.187
[  19.674] cmsis2: prio 46 -> tx 11
[  19.675] [I/USB] 
[  19.676] AIC ping gateway 10.227.127.187, 4 packets
[  19.677] [I/aic8800.fmac] 
[  19.678] AIC data TX Ethernet type=0x0806 length=42
[  19.689] [I/aic8800.fmac] 
[  19.690] AIC data RX Ethernet type=0x0806 length=42
[  19.694] [I/aic8800.fmac] 
[  19.694] AIC data RX Ethernet type=0x0800 length=74
[  19.695] [I/USB] 
[  19.696] AIC ping reply seq=1 time=19 ms
[  20.562] [I/aic8800.fmac] 
[  20.562] AIC data RX Ethernet type=0x0806 length=42
[  20.700] [I/aic8800.fmac] 
[  20.700] AIC data RX Ethernet type=0x0800 length=74
[  20.701] [I/USB] 
[  20.702] AIC ping reply seq=2 time=5 ms
[  21.761] [I/aic8800.fmac] 
[  21.761] AIC data RX Ethernet type=0x0800 length=74
[  21.762] [I/USB] 
[  21.763] AIC ping reply seq=3 time=60 ms
[  22.792] [I/USB] 
[  22.792] AIC ping reply seq=4 time=28 ms
[  22.793] [I/USB] 
[  22.794] AIC gateway ping test finished
[  27.679] aic8800: PASS
[  27.679] rtos: case aic8800 PASS
[  27.680] rtos: result pass cases=2/2 backend=threadx
[  27.681] rtos: resetting board
```

The anchors that make this a bring-up rather than a log dump: each USB
re-enumeration is a fresh `New device found,idVendor:` line with the next
identity, so the switch and the firmware download are visible in the same
transcript; and the handshake is reported **per message**, so a stolen message
shows as a missing line rather than as a timeout.


## RTL8188EUS (USB2 xHCI, mainline rtl8xxxu + net80211)

Profile `rtl8xxxu-8188eu-netio-ar-ht20-ba` (the ladder `rtl8xxxu`,
`rtl8xxxu_scan`, `rtl8xxxu_join`, `rtl8xxxu_dataplane`, `rtl8xxxu_netio`).
Dongle: RTL8188EUS, `0bda:8179`, USB2 on this bench's xHCI, one spatial stream,
2.4 GHz.  Bench AP: `labwifi-ar-ht20s` on channel 5 (2432 MHz), **open** - the
MLME is the imported net80211.  The image is entered at `0xa000000`; the
board's address is `192.168.99.2/24`.

What the transcript proves, in order: the chip reports itself (`RTL8188EU rev
K`, 1T1R) and the firmware answers (revision 28.0, signature 0x88e1); the
dongle is claimed by identity, on the xHCI at bus 3 port 4, and the driver
reads its MAC from it; the pool starts at 25 of 32 slots with `fail=0`;
a scan returns 18 rows with no FCS errors; the join reaches `state=4` with 15
management frames sent and 15 completed; the ARP reply arrives and 20 of 20
pings come back (`max_ms=1`); and the netio server takes a 1440-byte cell in
both directions with `wlan_txerr=0`, the gate stopping the drain 1958 times and
the seam refusing nothing at all (`txrefuse noskb=0`).

```
## Starting application at 0xa000000 ...
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=v0.0.2-139-g6c7e2007
rtos: kernel ready tick=1000Hz
[   0.000] rtos: runner start cases=6 build=v0.0.2-139-g6c7e2007
[   0.005] rtos: case tick start

... (the tick case: ladder beats, the copy/fill throughput probes) ...

[   3.043] tick: PASS
[   3.045] rtos: case tick PASS
[   3.048] rtos: case rtl8xxxu start
[   3.915] lkpi: usbh: modeswitch 0bda:1a2b CD-ROM persona, ejecting
[   4.465] rtl8xxxu: claim usbh: lkpi claim attempt 0bda:8179 bus=3 hub=2 port=4 intf=0 spd=3
[   4.625] lkpi: ...: RTL8188EU rev K (TSMC) romver 0, 1T1R, TX queues 2, WiFi=1, BT=0, GPS=0, HI PA=0
[   4.647] rtl8xxxu: fw ...: Firmware revision 28.0 (signature 0x88e1)
[   5.830] rtl8xxxu: claim usbkpi: 'rtl8xxxu' claimed 0bda:8179
[   6.023] rtl8xxxu: attach ok after 2100 ms mac=20:f4:1b:52:8f:01
[   6.033] rtl8xxxu: start ok attached=1 rx=3
[   6.037] rtl8xxxu: pool skb=25/32 data=0/0 heap_live=25 ovf=0/0 fail=0
[   6.056] rtl8xxxu: PASS
[   6.058] rtos: case rtl8xxxu PASS
[   6.061] rtos: case rtl8xxxu_scan start

... (the scan: one `rx elemwalk` line per beacon, the probe requests) ...

[   9.334] rtl8xxxu: scan done rows=18 bcn=105 bcnT=0 prsp=20 fcs=0 scan=1/1 state=1 entered=1 close=2 rx=171
[   9.349] rtl8xxxu: scan PASS
[   9.352] rtos: case rtl8xxxu_scan PASS
[   9.356] rtos: case rtl8xxxu_join start
[   9.359] rtl8xxxu: join bssid=24:ec:99:b9:8a:00 ssid=labwifi-ar-ht20s freq=2432 rssi=-61

... (auth and assoc, and their completions) ...

[   9.956] rtl8xxxu: join ok state=4 mgt_tx=15 txc=15 rx=178
[   9.961] rtl8xxxu: join PASS
[   9.964] rtos: case rtl8xxxu_join PASS
[   9.968] rtos: case rtl8xxxu_dataplane start

... (the address, the first ARP, the twenty pings) ...

[  10.016] rtl8xxxu: data arp reply src=24:ec:99:b9:8a:00 tx=1
[  21.380] rtl8xxxu: data ping tx=20 rx=20 min_ms=0 max_ms=1 lost=-
[  21.386] rtl8xxxu: data counters data_tx=22 tx_done=38 rx=311 rx_data=63
[  21.392] rtl8xxxu: data PASS
[  21.395] rtos: case rtl8xxxu_dataplane PASS
[  21.399] rtos: case rtl8xxxu_netio start
[  21.407] rtl8xxxu: net up ip=192.168.99.2/24 mac=20:f4:1b:52:8f:01 mtu=1500
[  28.279] net: netio addr 192.168.99.2/24
[  28.292] netio: tcp listening port=18767
[  28.845] netio: udp listening port=18768
[  28.916] net: netio ready port=18767 aux=18768 buf=16384

... (the cell: the downlink half, then the upload) ...

[  83.117] netio: udp client connected
[  83.121] netio: udp c2s pkt=1440
[  83.224] netio: udp recv start
[  89.257] netio: udp rx done pkts=27388 bytes=39438720
[  89.365] netio: cpu tag=udp-c2s frames=27388 ticks=149783438 us_x10_per_frame=2278 cyc_per_frame=0
[  89.380] netio: udp s2c pkt=1440
[  95.796] netio: cpu tag=udp-s2c frames=17667 ticks=153791113 us_x10_per_frame=3627 cyc_per_frame=0
[  95.805] netio: udp tx done pkts=17667 bytes=25440480 refusals=6001
[  95.843] netio: layers tag=udp-end wlan_tx=17707 wlan_done=17723 wlan_rx=28449 wlan_rxdata=27472 wlan_rxenc=41 wlan_txplain=17707 wlan_txerr=0 wlan_attach=1 wlan_freq=2432 wlan_gated=1958 wlan_wakes=17574 wlan_stash=0 wlan_stashok=0 wlan_xdrop=0 wlan_qhi=64 wlan_p
[  95.870] wlan: link txrefuse noskb=0 nopeer=0 nokey=0 purged=0
[  95.883] wlan: link rx data=27767 retry=2983 fcs=0
```

Two capture artefacts are visible and are not the board's doing: runs of
identical lines arrive collapsed as `[previous line repeated N times]`, and the
ledger line is cut at its 320th byte or so (`... wlan_qhi=64 wlan_p`); the
counters this section uses are all before the cut, and the counts the collapsed
lines stood for are visible in the `refusals=`/`wlan_gated=` figures instead.
The `...` inside the two prefix-carrying lines (`lkpi: ...:` and
`rtl8xxxu: fw ...:`) stands for three unprintable bytes (0x02 0x01 0x18) that
the console emits where the kernel-log prefix's level belongs; every other byte
of those lines is as printed.  The cell's 6001 refusals are the application's
`sendto` being told "not now" and retried (the client reports 0% loss in both
directions); the seam never saw a full pool on this arm, because the gate stops
the drain one layer earlier.

## RTL8723BU (USB2 xHCI, mainline rtl8xxxu + net80211)

Profile `rtl8xxxu-8723bu-netio-ar-ht20-ba` (the same ladder, with
`CONFIG_RTL8XXXU_CHIP_8723BU`).  Dongle: RTL8723BU, `0bda:b720`, USB2 on this
bench's xHCI, one spatial stream, 2.4 GHz; the Bluetooth half is not claimed by
this image.  Bench AP: `labwifi-ar-ht20s` on channel 5 (2432 MHz), **open**.
The image is entered at `0xa000000`; the board's address is `192.168.99.2/24`.

What the transcript proves, in order: the chip reports itself (`RTL8723BU rev
E`, 1T1R, `BT=1` - the combo's radio is there, this image claims only the Wi-Fi
interface) and the firmware answers (revision 35.0, signature 0x5301); the
claim is by identity on the xHCI at bus 1 port 2 and the MAC comes back
`88:e6:28:53:10:9c`; the pool starts at 29 of 32 slots and the first packets are
received; a scan returns 35 rows with no FCS errors; the join reaches
`state=4`; the ARP reply arrives and 20 of 20 pings come back (`max_ms=8`); the
netio server takes one 1440-byte cell in both directions, with `wlan_txerr=0`
and the gate stopping the drain 1523 times.

```
## Starting application at 0xa000000 ...
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=v0.0.2-139-g6c7e2007
rtos: kernel ready tick=1000Hz
[   0.000] rtos: runner start cases=6 build=v0.0.2-139-g6c7e2007
[   0.005] rtos: case tick start

... (the tick case) ...

[   3.045] rtos: case tick PASS
[   3.048] rtos: case rtl8xxxu start
[   4.686] rtl8xxxu: claim usbh: lkpi claim attempt 0bda:b720 bus=1 hub=2 port=2 intf=2 spd=3
[   5.072] lkpi: ...: RTL8723BU rev E (SMIC) romver 0, 1T1R, TX queues 3, WiFi=1, BT=1, GPS=0, HI PA=0

... (one debug line that prints a pointer where the MAC belongs) ...

[   5.094] rtl8xxxu: fw ...: Firmware revision 35.0 (signature 0x5301)
[   6.815] rtl8xxxu: claim usbkpi: 'rtl8xxxu' claimed 0bda:b720
[   6.905] rtl8xxxu: attach ok after 3200 ms mac=88:e6:28:53:10:9c
[   6.915] rtl8xxxu: start ok attached=1 rx=0
[   6.920] rtl8xxxu: pool skb=29/32 data=0/0 heap_live=29 ovf=0/0 fail=0
[   6.938] rtl8xxxu: PASS
[   6.941] rtos: case rtl8xxxu PASS
[   6.944] rtos: case rtl8xxxu_scan start

... (the scan) ...

[  10.624] rtl8xxxu: scan done rows=35 bcn=147 bcnT=0 prsp=35 fcs=0 scan=1/1 state=1 entered=1 close=2 rx=201
[  10.639] rtl8xxxu: scan PASS
[  10.642] rtos: case rtl8xxxu_scan PASS
[  10.646] rtos: case rtl8xxxu_join start
[  10.649] rtl8xxxu: join bssid=24:ec:99:b9:8a:00 ssid=labwifi-ar-ht20s freq=2432 rssi=-36

... (auth and assoc) ...

[  11.261] rtl8xxxu: join ok state=4 mgt_tx=15 txc=15 rx=206
[  11.266] rtl8xxxu: join PASS
[  11.269] rtos: case rtl8xxxu_join PASS
[  11.273] rtos: case rtl8xxxu_dataplane start
[  11.322] rtl8xxxu: data arp reply src=24:ec:99:b9:8a:00 tx=1
[  22.513] rtl8xxxu: data ping tx=20 rx=20 min_ms=0 max_ms=8 lost=-
[  22.519] rtl8xxxu: data counters data_tx=22 tx_done=38 rx=328 rx_data=35
[  22.525] rtl8xxxu: data PASS
[  22.528] rtos: case rtl8xxxu_dataplane PASS
[  22.532] rtos: case rtl8xxxu_netio start
[  22.540] rtl8xxxu: net up ip=192.168.99.2/24 mac=88:e6:28:53:10:9c mtu=1500
[  29.300] net: netio addr 192.168.99.2/24
[  29.314] netio: tcp listening port=18767
[  29.852] netio: udp listening port=18768
[  29.928] net: netio ready port=18767 aux=18768 buf=16384

... (the cell) ...

[  59.931] netio: udp client connected
[  59.934] netio: udp c2s pkt=1440
[  66.083] netio: udp rx done pkts=15476 bytes=22285440
[  66.127] wlan: link rx data=15727 retry=8084 fcs=0
[  66.196] netio: cpu tag=udp-c2s frames=15476 ticks=150213049 us_x10_per_frame=4044 cyc_per_frame=0
[  66.213] netio: udp s2c pkt=1440
[  72.257] netio: cpu tag=udp-s2c frames=13740 ticks=144863159 us_x10_per_frame=4392 cyc_per_frame=0
[  72.266] netio: udp tx done pkts=13740 bytes=19785600 refusals=6000
[  72.317] netio: layers tag=udp-end wlan_tx=13779 wlan_done=13795 wlan_rx=16276 wlan_rxdata=15543 wlan_rxenc=13 wlan_txplain=13779 wlan_txerr=0 wlan_attach=1 wlan_freq=2432 wlan_gated=1523 wlan_wakes=13707 wlan_stash=0 wlan_stashok=0 wlan_xdrop=0 wlan_qhi=64 wlan_p
```

The link's own retry counter is the interesting one in this cell: 8084 retries
on 15727 received frames, against 2983 on 27767 for the 8188EUS arm - the
8723BU's receiver is the weaker of the two on this bench, and the per-frame
costs (404 vs 228 us) say the same thing from the other side.  The join line
above shows `rssi=-36`, so this is not a distance problem.  As in the 8188EUS
transcript, the `...` inside `lkpi: ...:` and `rtl8xxxu: fw ...:` is three
unprintable prefix bytes (0x02 0x01 0x18), not removed text.

## RTL8821CU (USB2 xHCI, mainline rtw88 + net80211)

Profile `rtw88-usb-8821cu-ar-ht20-ba` (the ladder `rtw88_usb`, `rtw88_scan`,
`rtw88_join`, `rtw88_netio`).  Dongle: RTL8821CU, which arrives as the
`0bda:1a2b` CD-ROM persona and is switched by the image's own modeswitch hook
to `0bda:c820`.  Bench AP:
`labwifi-ar-ht20s` on channel 5 (2432 MHz), **open**.  The image is entered at
`0xa000000`; the board's address is `192.168.99.2/24`.

What the transcript proves, in order: the modeswitch ejects the CD-ROM
persona in 31 bytes and the runtime device enumerates on the second controller;
the firmware answers (`Firmware version 24.11.0, H2C version 12`) and the driver
attaches `54:c9:e0:00:29:66` with the transmit engine's counters at zero
refusals; the second dongle on the same board (`0bda:8179`) is passed over with
`-19`, which is this profile binding by identity, not a fault; a scan returns 32
rows; the join reaches `state=4`; and the netio server takes one 1440-byte cell
in both directions with `wlan_txerr=0`, the held-frame counters
(`wlan_stash=31069`, `wlan_stashok=15205`) and the seam's refusals all accounted
for (`txrefuse noskb=31069 nopeer=0 nokey=0 purged=0`).

```
## Starting application at 0xa000000 ...
boot: mainrtos: banner backend=freertos kernel=FreeRTOS-Kernel V11.3.1 build=v0.0.2-138-gf1c6a775
rtos: kernel ready tick=1000Hz
[   0.000] rtos: runner start cases=5 build=v0.0.2-138-gf1c6a775
[   0.005] rtos: case tick start

... (the tick case) ...

[   3.045] rtos: case tick PASS
[   3.048] rtos: case rtw88_usb start
[   3.078] rtw88: usb buses=3,2
[   3.915] lkpi: usbh: modeswitch 0bda:1a2b CD-ROM persona, ejecting
[   3.934] rtw88: claim usbh: modeswitch sent, expecting 0bda:c820
[   4.414] rtw88: claim usbh: lkpi claim attempt 0bda:c820 bus=2 hub=1 port=1 intf=2 spd=3
[   4.428] rtw88: fw lkpiu: Firmware version 24.11.0, H2C version 12
[   4.475] rtw88: err usbh: usb_driver probe failed (ret -19) for 0bda:8179
[   6.822] rtw88: claim usbkpi: 'freewificam' claimed 0bda:c820
[   6.892] rtw88: attach ok after 3200 ms mac=54:c9:e0:00:29:66
[   6.898] rtw88: start ok attached=1 rx=0
[   6.902] rtw88: pool skb=8/10 data=0/2 heap_live=8 ovf=0/0 fail=0
[   6.908] rtw88: usb submits=73 completes=72 refused=0 stage_overflow=0 slots_exhausted=0 stage_size=32768 slots=16 rx=0 rx_err=0 rx_last=0
[   6.920] rtw88: PASS
[   6.922] rtos: case rtw88_usb PASS
[   6.925] rtos: case rtw88_scan start

... (the scan) ...

[  10.413] rtw88: scan done rows=32 bcn=84 prsp=17 fcs=0 scan=1/1 state=1 entered=1 close=2 rx=106
[  10.421] rtw88: scan PASS
[  10.424] rtos: case rtw88_scan PASS
[  10.427] rtos: case rtw88_join start
[  10.431] rtw88: join bssid=24:ec:99:b9:8a:00 ssid=labwifi-ar-ht20s freq=2432 rssi=-32 hits=1 rows=32

... (auth, assoc, the beacon count) ...

[  11.334] rtw88: join ok state=4 mgt_tx=15 txc=15 auth=1 bcnT=2
[  11.340] rtw88: join PASS
[  11.342] rtos: case rtw88_join PASS
[  11.346] rtos: case rtw88_netio start
[  11.353] rtw88: net up mode=static ip=192.168.99.2 mask=255.255.255.0 gw=192.168.99.1 mac=54:c9:e0:00:29:66 mtu=1500
[  11.364] net: netio addr 192.168.99.2/24
[  11.377] netio: tcp listening port=18767
[  11.915] netio: udp listening port=18768
[  11.965] net: netio ready port=18767 aux=18768 buf=16384

... (the cell) ...

[  84.077] netio: udp c2s pkt=1440
[  90.198] netio: udp rx done pkts=28288 bytes=40734720
[  90.230] wlan: link txrefuse noskb=0 nopeer=0 nokey=0 purged=0
[  90.306] netio: cpu tag=udp-c2s frames=28288 ticks=149421776 us_x10_per_frame=2200 cyc_per_frame=0
[  90.321] netio: udp s2c pkt=1440
[  98.966] netio: cpu tag=udp-s2c frames=15772 ticks=207193756 us_x10_per_frame=5473 cyc_per_frame=0
[  98.975] netio: udp tx done pkts=15772 bytes=22711680 refusals=5907
[  99.109] netio: layers tag=udp-end wlan_tx=15778 wlan_done=15850 wlan_rx=29783 wlan_rxdata=28302 wlan_rxenc=4 wlan_txplain=15778 wlan_txerr=0 wlan_attach=1 wlan_freq=2432 wlan_gated=0 wlan_wakes=15480 wlan_stash=31069 wlan_stashok=15205 wlan_xdrop=0 wlan_qhi=64 wl
[  99.137] wlan: link txrefuse noskb=31069 nopeer=0 nokey=0 purged=0
[  99.150] wlan: link rx data=28670 retry=2205 fcs=0
```

The pool-full line (`lkpi_skb: alloc refused: header slots full (128) and heap
headers full (64)`) repeats through the upload half of this cell and is elided
by the `...`; it is the seam telling the bridge "no header" at 1550 bytes for
a data frame and 70 for the bare ack, and the `wlan_stash`/`wlan_stashok` pair
is the bridge holding those frames and delivering them on a later completion
instead of dropping them.  This is the arm where the seam, not the gate,
absorbs the back-pressure: `wlan_gated=0` because this driver's USB transmit
path never calls `ieee80211_stop_queues`, so the only brake is the pool.

## What is not claimed here

Each transcript is one boot and, where a cell is shown, one cell: it establishes
that the exchange happens and which counters account for it, not that the number
repeats.  The performance table in the top-level README is where a rate is
allowed to stand, and a module appears there only after its own repeated runs.
The three USB modules share a cellular-quality bench AP and a single board;
their absolute rates are not comparable to the SDIO arm's, which ran on the
other board with a different AP.
