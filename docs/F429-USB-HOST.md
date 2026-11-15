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
  A self-powered hub can be inserted but is not needed.

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
disks are out of scope. There is no cold boot on this bench
(`mculab` `power.type=none`), so the evidence is 2/2 nRST resets.

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

## 6. USB3 hub leftover (2026-11-15)

A USB3 hub (`05e3:0610`) is visible and USB2-compatible on this FS host: the
hub itself, the RTL8188EUS (`0bda:8179`, hub port 2) and the U disk
(`05e3:0749`, hub port 4) all complete USB enumeration.  The U disk's block 0
and eight-block read pass through the hub, but the 512 B WRITE10 roundtrip
stops after the CBW (no CSW/PASS).  The same image and disk direct on P2 pass
2/2, so this is a hub-path/out-transfer or power issue, not the direct link.

The hub is therefore **not** an accepted storage topology yet.  Reopen with a
self-powered hub (or external 5 V) first; if the write still hangs, instrument
the DWC2 OUT channel/HCINT path rather than adding a longer timeout.  One INFO
boot registered the hub but saw no downstream device within 30 s, while a
USB_DBG_LOG build saw both; that timing is also unclassified.  Evidence:
`runs/2026-11-15-f429-usb-hub/`.

## 7. Can the hub carry multiple wireless modules later?

Structurally the stack can carry **one external hub**:
`CONFIG_USBHOST_MAX_EXTHUBS=1`, `CONFIG_USBHOST_MAX_EHPORTS=4`, and the DWC2
core has 12 host channels.  That is enough endpoints/channels for a few FS
devices on one hub.  But it is not a promise that multiple WiFi modules work:

1. The hub path is only proven for enumeration and U-disk reads.  The U-disk
   512 B WRITE10 through the hub still stops after the CBW, while the direct
   P2 path is 2/2.  WiFi transmit is bulk OUT, the same path that hangs, so
   the hub OUT path (and its power/current budget) has to be closed first.
2. The F429 is a **single full-speed host**: all devices behind the hub share
   12 Mbit/s.  Two or more WiFi modules doing scan/assoc/data will compete for
   that one FS frame budget, before any driver/CPU cost.
3. This image has no WiFi class driver.  A module is only enumerated
   (`0bda:8179`, class 0xff); claiming the vendor interface needs the
   rtl8xxxu/linuxkpi USB stack, which is not in the F429 image.
4. The F429 line's documented wireless target is **SDIO RTL8189FS**, not USB
   HS.  Multiple USB WiFi modules are a separate, later porting question; do
   not treat the hub as a supported multi-radio bus yet.

Practical order: self-powered hub/external 5 V and a passing U-disk write
through the hub; one WiFi module's interface claim; only then a second module
with a measured power/bandwidth/RAM budget.  Record the hub write result in
`runs/2026-11-15-f429-usb-hub/` when it is rerun.
