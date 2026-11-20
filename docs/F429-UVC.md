# STM32F429 UVC camera — CherryUSB video class + a DWC2 full-speed ISO engine

Date: 2026-11-20. Board: 野火 STM32F429 挑战者 V2 (STM32F429IGT6, 168 MHz,
1 MB flash, 192 KB SRAM + 64 KB CCM, 32 MB W9825G6 SDRAM on FMC bank 2).
Link: OTG_HS in **full-speed host mode** (PB14/PB15, P2 USB-A, J74–J76 on the
HOS side). Camera on the bench: **Logitech QuickCam Pro 9000, `046d:0990`**,
the same model the previous camera line used. Stack: CherryUSB at pin
`51fef88d` (the same pin the rest of this tree uses), its host video class and
the F429 tree's DWC2 host port.

The point of this line is the bus question, not a camera application: the
F429's only host port is full speed, and a UVC camera's streaming endpoint is
isochronous. The round adds the isochronous engine the DWC2 port was missing,
then measures what the camera can actually put through a **1023-byte-per-1 ms**
full-speed isochronous pipe.

The README carries the one-screen adaptation table; this document is the full
record: the raw descriptors, the engine's contract, the measured windows and the
limits.

## 1. The full-speed ceiling, stated before the measurements

A full-speed isochronous endpoint carries at most one 1023-byte transaction per
1 ms frame:

```
1023 B/frame / 1 ms = 1.023 MB/s = 8.184 Mbit/s
```

That is the whole bus, not a per-camera budget. 720p30 MJPEG (typically
10–20 Mbit/s) is above it. A 720-line mode can only fit at a low frame rate,
and only if the camera exposes such a setting **and** its UVC probe reports a
payload at or below the largest streaming alternate setting. This camera is
checked against that rule in section 3.

## 2. The camera's full-speed mode table

`uvc_detect` prints the class's parsed table and the raw frame descriptors
(including the interval arrays the class does not keep). The camera enumerates
as:

```
uvc: connect vid=046d pid=0990 bus=0 dev=2 minor=0 formats=3 alts=7
```

MJPEG (format 1) at full speed:

| frame | mode | default interval | default fps |
|---:|---|---:|---:|
| 1 | 160x120 | 333333 (100 ns) | 30 |
| 2 | 176x144 | 333333 | 30 |
| 3 | 320x240 | 666666 | 15 |
| 4 | 352x288 | 666666 | 15 |
| 5 | 640x480 | 666666 | 15 |
| 6 | 800x600 | 666666 | 15 |

Format 2 is uncompressed 160x120/176x144; format 3 has no frame descriptors.
The streaming interface's alternate settings are:

| alt | wMaxPacketSize (bytes/frame) | interval |
|---:|---:|---:|
| 1 | 192 | 1 |
| 2 | 384 | 1 |
| 3 | 512 | 1 |
| 4 | 640 | 1 |
| 5 | 800 | 1 |
| 6 | 944 | 1 |

**There is no 720-line frame descriptor at full speed** — not 1280x720 and not
960x720. The `uvc_detect` census prints `720_line_modes=0`. The largest mode is
800x600, and its probe reports `want=944` bytes per frame, exactly the largest
alternate setting. This is a camera-mode-table limit, not a bandwidth
subtraction: the camera never asks the FS host for a 720p configuration.

The census's 640x480 and 800x600 probes both report `want=944` (the camera's
maximum-payload answer in that probe state). The class's open sequence re-probes
after its `SET_CUR`, and the two arms then negotiated different values: 640x480
opened alt 4 with `mps=640`, `dmp=640`; 800x600 opened alt 6 with `mps=944`,
`dmp=944`. The open value is what the stream ran at. Both fit the 1023-byte
full-speed bound; the census is the conservative upper bound.

## 3. The 720p verdict

For this camera on this board: **no 720p**. The full-speed descriptor set has
zero 720-line modes, so the `stm32f429-uvc-720p` profile cannot open a mode
that does not exist. A boot of that profile confirms it:

```text
uvc: open FAIL mode 1280x720 mjpeg not in the camera's tables
uvc: iso smoke FAIL camera not open
uvc: stream FAIL camera not open
rtos: result fail cases=1/4 backend=freertos
```

`uvc_detect` passes (identity + mode census); the three stream cases fail at
open, which is the expected result for a camera without a 720-line mode. The
720p profile is kept as the target image for a different camera; the code's
open path fails with the mode table, not with a transport error.

What *is* possible on this bus and this camera:

| mode | result | frames / 20 s | rate | payload rate | drops | errors |
|---|---:|---:|---:|---:|---:|---:|
| 640x480 MJPEG | **2/2 PASS** | 248 / 276 | 12.383 / 13.760 fps | 1917 / 2065 kbit/s | 1 / 1 | 0 / 0 |
| 800x600 MJPEG | **2/2 PASS** | 275 / 275 | 13.709 / 13.732 fps | 2907 / 2899 kbit/s | 1 / 1 | 0 / 0 |

Both arms run the same ladder (`uvc_detect` → `uvc_open` → `uvc_iso_smoke` →
`uvc_stream`) and leave the camera open between cases. Every delivered frame
had a JPEG SOI and EOI (`jpeg soi=N eoi=N`); the ISO smoke is 16/16 URBs with
zero packet errors. The 800x600 arm uses 2.9 Mbit/s of the 8.184 Mbit/s bus
ceiling, so the bus is not the reason 720p is unavailable — the camera's mode
table is.

A 720p-capable UVC camera that exposes a 720-line mode at a low frame interval
could, in principle, fit: the rule is `probe.dwMaxPayloadTransferSize <= 1023`
and the largest alternate setting's `wMaxPacketSize` at least that value. That
is a camera-selection question, not a DWC2 question.

## 4. The DWC2 isochronous engine (the code the round added)

The DWC2 host port at the pin has no working isochronous path: `usbh_submit_urb()`
leaves the ISO case empty and the channel-halt interrupt has an empty ISO
branch. The camera image needs both.

The core reports only the **total** length of the transaction it halted, but a
UVC payload is a sequence of packets whose first bytes are a variable-length
payload header. The engine therefore programs **one `iso_packet[]` entry per
channel activation** (`HCTSIZ.PKTCNT=1`), records that entry's `actual_length`
from the remaining `XFRSIZ`, and re-arms the same channel for the next entry
from the channel-halt interrupt. This is the per-frame model Linux's dwc2 host
driver uses for isochronous URBs.

- `usbh_submit_urb()` validates the packet array (non-empty, each buffer
  4-byte aligned and no longer than the endpoint's `wMaxPacketSize`), refuses
  a second in-flight URB for the same endpoint with `-USB_ERR_BUSY`, sets
  `urb->start_frame`, and arms the first packet. `timeout > 0` is the
  synchronous path the ISO smoke case uses; `timeout == 0` returns after arming
  and completes through the callback.
- The halt handler records `actual_length` and a per-packet error (`XFRC`
  absent or a transport error costs exactly one packet, never the whole URB),
  advances to the next packet, and completes the URB at the end through
  `dwc2_urb_waitup()`.
- `usbh_kill_urb()` marks the remaining packets `-USB_ERR_SHUTDOWN` and frees
  the channel, so a stream stop is bounded.
- The code is compiled only under `CONFIG_USB_DWC2_ISO`, which the UVC cases
  select in `platform/build_defs.h`. Every non-camera F429 USB image keeps the
  port's pin bytes and pays no code size.

The engine is one channel per endpoint by design. The stream engine is
configured for one 64-packet URB (`CONFIG_UVC_STREAM_URBS=1`,
`CONFIG_UVC_STREAM_PACKETS=64`), resubmitted by its pump after each completion;
64 packets is 64 ms of bus time, small against a 15 fps frame. The 1 dropped
frame per 20 s window is consistent with that resubmit gap (the second boot had
the same count at both resolutions), not with a transport error.

## 5. Memory and board configuration

The frame pool lives in the FMC SDRAM because three 720-line-capable buffers
do not fit the 192 KB SRAM part:

- 640x480 and 800x600 profiles: 3 x 128 KiB frame buffers, 1 MiB FreeRTOS
  heap in SDRAM (the 800x600 arm's largest measured frame was 27,474 B).
- 720p profile: 3 x 512 KiB frame buffers, 1 MiB heap in SDRAM, for a camera
  whose 720-line MJPEG frame is larger than 128 KiB.
- The DWC2 host DMA's SDRAM reachability was already proven by the
  `stm32f429-sdram` case (a U-disk bulk-IN read into SDRAM 2/2); the UVC
  packet ring is allocated from the same SDRAM heap and is 4-byte aligned.

Board limits the camera needed in `platform/board/stm32f429-challenger/usb/usb_config.h`:

| key | value | why |
|---|---:|---|
| `CONFIG_USBHOST_MAX_INTF_ALTSETTINGS` | 16 | the camera's streaming interface has 7 alternate settings; the previous 8 was already tight for the Realtek dongles and wrong for a larger UVC ladder |
| `CONFIG_USBHOST_REQUEST_BUFFER_LEN` | 2048 | the UVC configuration descriptor carries the control interface plus the streaming format/frame descriptors |
| `CONFIG_USBHOST_MAX_VIDEO_CLASS` | 1 | one camera |
| `CONFIG_UVC_STREAM_URBS` | 1 | one channel per endpoint in the DWC2 ISO engine |
| `CONFIG_UVC_STREAM_PACKETS` | 64 | 64 ms of bus time per URB, 64 KiB packet ring |
| `CONFIG_UVC_STREAM_STACKSIZE` | 4096 | the pump parses payload headers and assembles frames |

## 6. How to reproduce

```bash
cd freewificam
./make.rtos image PROFILE=stm32f429-uvc-detect     # descriptor + probe census only
./make.rtos image PROFILE=stm32f429-uvc-480        # 640x480 control arm
./make.rtos image PROFILE=stm32f429-uvc-800x600    # largest FS mode
./make.rtos image PROFILE=stm32f429-uvc-720p       # target for a 720-line camera
```

Flash the BIN through the board's Pi-based pyOCD path and cold-boot the
board; the standard reset is the relay cold boot. The camera is on P2, the
USB-A host socket, with J74–J76 in the HOS position. The console is
115200 8N1 on the CH340; the SWD ring mirrors it.

`uvc_detect` is the first boot for a new camera: its `uvc: mode`, `uvc: alt`,
`uvc: raw frame`, and `uvc: probe ... fits=yes|no` lines are the mode table and
the bandwidth answer. Do not build a target profile before reading
`720_line_modes` and the probe line.

## 7. Limits and open items

- One camera model, one bus (FS), one URB in flight; no hot-plug, no
  disconnect/reconnect, no second camera.
- No 720p claim for this camera. The 720p image exists for a camera whose FS
  descriptor set actually contains a 720-line mode; it has not been run
  against such a camera.
- The 20 s window is a rate sample, not a stability run. The camera is left in
  its streaming setting on purpose; a stop can leave this family refusing the
  video-streaming class requests for the rest of the boot.
- No UVC bulk-endpoint camera arm. The class supports a bulk streaming
  interface, but this camera and this round use isochronous.
- The 800x600 arm is report-only for rate; its gate is the same four cases
  (frames > 0, JPEG SOI/EOI, zero transport errors, drops within the window's
  budget), not a bit-rate floor.

## 8. The microphone half

The same camera's interfaces 2 and 3 are a UAC1 microphone.  The line carries a
first-party UAC1 host class (patch 0012) and a PCM engine on the second
isochronous IN endpoint (`0x86`, 16 kHz mono 16-bit); the video and microphone
streams run together for 20 s with no video degradation.  The descriptor
census, the class's refusals, the capture engine's contract and the measured
windows are in `docs/F429-UAC.md`.  The camera's audio function only produces
samples after its video function has been claimed, so the microphone image
carries the video class and this document's census path.
