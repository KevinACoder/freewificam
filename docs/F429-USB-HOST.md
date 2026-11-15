# STM32F429 USB host — CherryUSB DWC2 + MSC

Date: 2026-11-15. Board: 野火 STM32F429 挑战者 V2 (STM32F429IGT6).
Image: `stm32f429-usb-msc` (`ARCH_ARM_V7M` + `BOARD_STM32F429_CHALLENGER`,
FreeRTOS CM4F, 168 MHz / PLLQ 48 MHz).

## 1. Hardware facts

The V2 base schematic (sheet 8, `ebf_stm32_tiaozhanzhe_hardware.zip`) and the
board photo give the wiring:

- P2 is the USB-A HOST socket. Its VBUS is tied to 5V; D-/D+ are the nets
  `HOST_DN` / `HOST_DP`.
- The micro USB is the OTG1 device socket; its VBUS is `5V_USB`, an input when
  the board is DC-powered, so it does not power a bus-powered U disk.
- `HOST_DN/DP` and `OTG1_DN/DP` both land on **PB14/PB15** through the
  **J74-J76** selector; the default position is OTG1, HOS selects the USB-A
  socket. A U disk on P2 needs the three caps on the HOS side.
- The controller is **OTG_HS in full-speed host mode** (`USB_OTG_HS_PERIPH_BASE`
  `0x40040000`, IRQ 77), not the OTG_FS core. `clock.c` already produces
  PLLQ = 48 MHz at 168 MHz.
- The USB HOST socket is not a hub: the root port sees one device directly.
  A **self-powered** hub works and is now the bench's multi-dongle topology
  (section 6); a bus-powered hub is not reliable (section 6).

Evidence for the selector/OTG1 gate: on the invalid boot the DWC2 port reported
`GSNPSID=0x4f54281a`, `GHWCFG2=0x229ed590` (12 host channels, INT DMA), host
mode, `HPRT=0x00001000` (PPWR=1, PCSTS=0) and `irq=0`. That is "controller up,
no D+ pull-up"; it is not a class-driver failure.

## 2. Register criteria

| Check | Where | Expected on this board |
|---|---|---|
| Core identity | `GSNPSID` @ `0x40040040` | `0x4f54281a` (DWC2) |
| Host channels / DMA | `GHWCFG2` @ `0x40040048` | `0x229ed590`: 12 channels, `arch=2` INT DMA |
| FIFO word depth | `GHWCFG3` @ `0x4004004c` | `0x03f403e8`: 1024 words |
| Host mode | `GINTSTS.CMOD` | 1 after `dwc2_set_mode(HOST)` |
| Port power | `HPRT.PPWR` @ `0x40040440` | 1 (`HPRT=0x00001000` before connect) |
| Device present | `HPRT.PCSTS` | 1 when a U disk presents D+ (the working reader shows connect) |
| IRQ delivery | OTG_HS IRQ 77 count | grows during enumeration/transfer |

A port with `PPWR=1`, `PCSTS=0` and `irq=0` is a wiring/power gate, not a
CherryUSB or MSC gate. A port with `PCSTS=1` but no class claim is the class
table; a class claim with no block zero is the MSC transport.

## 3. Code fixes that made it pass

1. **USB 3.x device on a USB 2 host.** `usbh_core.c` decoded
   `bcdUSB >= 3.0` as `1 << bMaxPacketSize0` unconditionally. A USB3 card
   reader reports `bMaxPacketSize0=9`, which is 512; on the FS host the
   subsequent SET_ADDRESS times out, and the timeout/kill path enters the
   channel-free critical section from the OTG_HS ISR. The fix uses
   `1 << bMaxPacketSize0` only when `hport->speed == USB_SPEED_SUPER`, else 64
   (the USB 2 EP0 maximum). This was the FAIL-to-PASS line.
2. **ISR-safe OSAL critical section.** The DWC2 `dwc2_chan_free()` path can run
   from `USBH_IRQHandler`; the CMSIS-RTOS2 facade's `osCriticalEnter()` used
   `taskENTER_CRITICAL()`, which the CM4F port asserts in an ISR. The F429
   `freertos_board_glue.c` now checks `__get_IPSR()` and uses
   `taskENTER_CRITICAL_FROM_ISR()` / `taskEXIT_CRITICAL_FROM_ISR()`.
3. **Control timeout.** `CONFIG_USBHOST_CONTROL_TRANSFER_TIMEOUT` is 5000 ms;
   the first response of a card reader after port reset can exceed 1 s.
4. **`CONFIG_BOARD_AUTO_RESET=n`.** The generator emitted
   `#define RTOS_BOARD_AUTO_RESET 0`, and the app tests `#if defined(...)`;
   every image therefore reset after the verdict. The generator now omits the
   macro when off, so the PASS/FAIL line stays on the wire and the board parks.
5. The U disk itself: the first stick did not present D+ and was replaced by a
   USB3.0 Card Reader (`05e3:0749`), which enumerated as
   `Block num 123596800, block size 512` (60350 MiB).

## 4. Acceptance

Two hardware resets of the same image (`c4dbe1e7…` ELF, `6b841a7a…` BIN,
56,548 B):

```text
usbh: msc bus=0 hcd=dwc2 vid=05e3 pid=0749 speed=2 blocks=123596800 bsize=512
usbh: msc ep in=0x81 type=bulk maxp=64 out=0x02 type=bulk maxp=64
usbh: msc blk0 ok sig=55aa
usbh: msc read lba=0 blocks=8 bytes=4096 ok
usbh: msc window lba=123594752 blocks=2048
usbh: msc roundtrip ok lba=123594752 len=512
usbh: msc PASS
rtos: result pass cases=2/2 backend=freertos
```

`judge.py` passes both boots; the write touched one 512 B sector in the disk's
last 1 MiB window (LBA 123,594,752), with 4 MiB in front never touched. The
test is raw MSC, not FatFs; the disk's exFAT/FAT32 formatting is irrelevant to
this verdict. Hot-plug, composite devices, multiple LUNs and write-protected
disks are out of scope. That run's evidence is 2/2 nRST resets; the bench
gained a **relay cold boot** later the same day (section 7), which is now the
standard reset (an MCU reset does not reset the dongle's USB state).

## 5. RTL8188EUS at the USB layer (2026-11-15)

A direct RTL8188EUS (`0bda:8179`, full speed, one interface, class 0xff)
enumerates on the F429 host.  The evidence is the core's own lines:

```text
[I/usbh_hub] New full-speed device on Bus 0, Hub 1, Port 1 connected
[I/usbh_core] New device found,idVendor:0bda,idProduct:8179,bcdDevice:0000
[I/usbh_core] The device has 1 interfaces
[E/usbh_core] Do not support Class:0xff, Subclass:0xff, Protocl:0xff on interface 0
```

The `mcu flash --expect "idVendor:0bda,idProduct:8179"` command returns 0.
This image has no rtl8xxxu/mac80211/linuxkpi-USB driver, so the vendor
interface is deliberately unclaimed and the MSC-specific `usb_dwc2` case then
fails its 30 s mass-storage wait.  That failure is the missing class driver,
not a failed enumeration; a separate generic-enumeration case is future work.

## 6. A hub on the host port: four wireless dongles (2026-11-15 evening)

The bench topology the F429 line now uses: the P2 host socket -> a **self-powered**
USB hub `05e3:0610` (4 ports) -> four Realtek wireless dongles.  All four are
recognised at the USB layer, and the 8188EUS runs the full W6 data plane
**through the hub** (`runs/2026-11-15-f429-wifi-hub-multi/`):

| hub port | USB ID | what it is | driver result |
|---|---|---|---|
| 1 | `0bda:8179` | RTL8188EUS | claimed by rtl8xxxu, wlan0, W6 PASS |
| 2 | `0bda:1a2b` -> `0bda:c820` | RTL8188CUS/8192CU family | CD-ROM persona; the modeswitch ejects it and expects `c820` back (the re-enumeration was not sampled yet) |
| 3 | `0bda:b720` | RTL8723BU (WiFi+BT) | enumerated; its vendor interface is claimed and the rtl8xxxu probe fails (`-11`) - this port has only the 8188E firmware paths |
| 4 | `0bda:b851` | RTL8851BU (WiFi+BT) | enumerated; no driver (`-19`, rtw89 is not ported) |

Two **static limits** had to be raised in the board's `usb_config.h`, or the
last three dongles fail config parsing before any driver sees them:

1. `CONFIG_USBHOST_MAX_INTF_ALTSETTINGS` 2 -> **8**.  The 8723BU, the 8188CUS
   and the 8851BU each present `bAlternateSetting 2` in interface 0; with 2 the
   parser returns `Interface altsetting num 2 overflow / Parse config
   descriptor fail`.
2. `CONFIG_USBHOST_MAX_ENDPOINTS` 4 -> **8**.  The 8723BU's config has 6
   endpoints in one interface and the 8851BU's has 8; with 4 they fail with
   `Endpoint num 6/8 overflow`.

Power was the other half: with a **bus-powered** hub the downstream devices
either never appeared within the 60 s attach window or took 15-19 s; with the
external supply they all appear within 2-5 s.  The W6 gate then passes with
`rx_err=0` and the AP's capture holds 44 echo requests + 44 replies.

The WiFi image (`stm32f429-wifi-c6`) is the one that claims the dongles: its
linuxkpi USB hook matches the vendor interface triple 0xff/0xff/0xff and runs
the driver's own id_table.  The other three dongles enumerate but are not
usable until the port grows those chips' probe paths (8723BU) or rtw89 is
ported (8851BU); that is driver work, not USB work.

## 7. Relay cold boot (2026-11-15 evening)

The bench now has a UStone relay (`192.168.0.44`) on the F429's supply, so a
**cold boot is controllable from the session**:

```
cd ~/workspace/project_linux_wcam
bash skills/rk3568-board/scripts/power.sh off|on|cycle --confirm
```

`cycle` cuts power for >=6 s, switches on, and settles 12 s.  This is the
standard reset for F429 rounds because an MCU reset does not reset the USB
devices (a wedged dongle keeps failing every RX bulk IN with `-EIO` after
`mcu reset`, the trap first seen in the W6 round).  The relay path was
validated with `off` (the console goes silent) -> `on` -> a fresh boot that
enumerates the hub + all four dongles and passes W6, twice
(`.../f429-wifi-hub-multi/boot7`, `boot8`).
