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
