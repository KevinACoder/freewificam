# STM32F429 UAC microphone — a first-party UAC1 host class and PCM engine

Date: 2026-11-20. Board: 野火 STM32F429 挑战者 V2 (STM32F429IGT6, 168 MHz,
1 MB flash, 192 KB SRAM + 64 KB CCM, 32 MB W9825G6 SDRAM on FMC bank 2).
Link: OTG_HS in **full-speed host mode** (PB14/PB15, P2 USB-A, J74–J76 on the
HOS side). Camera on the bench: **Logitech QuickCam Pro 9000, `046d:0990`**.
Stack: CherryUSB at the tree's pin, its host video class, and a first-party
UAC1 host class added for this line (patch 0012).

The point of this line is the camera's microphone, not board audio: the
camera's interface 2/3 are a USB Audio Class 1.0 microphone, and the question
is whether the F429's full-speed host can capture its PCM while the UVC video
stream is running. **Playback is out of scope**: this camera has no UAC OUT,
and local playback (WM8978/I2S) belongs to a separate board-audio line.

## 1. What the camera carries

The camera enumerates four interfaces: UVC video control (0), UVC video
streaming (1), AudioControl (2, class 0x01/subclass 0x01/protocol 0x00) and
AudioStreaming (3, 0x01/0x02/0x00). At full speed its audio function is:

| field | value |
|---|---|
| `bcdADC` | `0x0100` (UAC 1.0) |
| AudioControl / AudioStreaming interfaces | 2 / 3 |
| input terminal | ID 1, type `0x0201` (microphone), 1 channel |
| output terminal | ID 3, type `0x0101` (USB streaming), source ID 5 |
| feature unit | ID 5, source ID 1, master mute + volume (`bmaControls` 0x0003) |
| AS general | terminal link 3, delay 1, format tag `0x0001` (PCM) |
| format type I | 1 channel, 2 bytes per sample, 16 bits, one discrete rate **16000 Hz** |
| streaming endpoint | `0x86` (ISO IN), `wMaxPacketSize=36`, `bInterval=1` |
| endpoint companion | attributes `0x01` (sampling-frequency control supported) |

The AC class-specific bytes are
`09 24 01 00 01 26 00 01 03` (header),
`0c 24 02 01 01 02 00 01 00 00 00 00` (input terminal),
`09 24 03 03 01 01 01 05 00` (output terminal) and
`08 24 06 05 01 01 03 00` (feature unit); the AS block is
`07 24 01 03 01 01 00`, `0b 24 02 01 01 02 10 01 80 3e 00` and
`07 25 01 01 00 00 00`.

The declared 36-byte maximum is four bytes above the 32 bytes a 16 kHz mono
16-bit frame carries; the device sends 32-byte packets, so every audio packet
is one millisecond of PCM.

## 2. The host class (patch 0012)

The pin ships no host audio class (its host-UAC framework is the commercial
tier), so this line carries its own subset in
`third-party/cherryusb/class/audio/`.  It is written from the USB Audio Class
1.0 specification, not from the vendor host-audio source.

- Two `usbh_class_info` rows claim class 0x01 subclass 0x01 (AC) and 0x01/0x02
  (AS).  The match is class+subclass; the connect checks `bcdADC` and the
  interface protocol, so a UAC2 device is refused with a named reason rather
  than half-opened.
- The AC connect copies and parses both the AudioControl and AudioStreaming
  class-specific descriptors **while `hport->raw_config_desc` is alive**: the
  core frees that buffer at the end of enumeration, so a census that read it
  later would race the hub thread.  The AS connect is a no-op.
- The parsed view is refused as a whole when it is outside this line's subset:
  UAC2, an OUT streaming endpoint, a non-PCM format tag, a format other than
  type I, a sample size other than 2 bytes/16 bits, a `bInterval` other than
  one frame, an out-of-range max packet, or a non-microphone input terminal.
  The refusal text is what the census case prints.
- `usbh_uac_open()` sends `SET_INTERFACE(streaming interface, alt)` and then,
  when the endpoint's companion descriptor advertises it,
  `SET_CUR(SAMPLING_FREQ_CONTROL)` with a three-byte rate to the endpoint,
  followed by `GET_CUR` readback.  A refused `SET_CUR` is recorded
  (`setcur=stall`) and capture continues at the device's own default; the
  captured byte count is the evidence of the actual rate.
- The feature unit's mute state is readable and clearable
  (`GET_CUR`/`SET_CUR` on the master channel); the microphone window clears a
  set mute before it starts, never in the middle of the stream.
- The streaming interface is deliberately **not** returned to alternate
  setting 0 on stop.  This camera family is known to refuse class requests
  after leaving a streaming setting, and the next session can run on the open
  endpoint.

## 3. The capture engine

`usbh_uac_stream.c` keeps one isochronous URB of **16 one-millisecond
packets** on endpoint `0x86` and copies each packet's `actual_length` bytes
into a 200 ms single-producer/single-consumer PCM ring (6,400 bytes at
16 kHz mono 16-bit).  The design points below are measured, not assumed:

- **The next URB is armed from the URB-completion callback, in interrupt
  context.**  The DWC2 engine completes an isochronous URB at its last packet,
  and the next URB must be in the frame list before the next 1 ms frame or that
  frame's audio is lost.  A pump task can be delayed past the boundary by
  another runnable task (the video pump in the video+microphone image); an
  immediate resubmit inside the same interrupt has only register writes between
  the two URBs.  The callback's work is bounded (16 packets × 32 bytes plus one
  submit).
- **A 50 ms settle follows `SET_INTERFACE`/`SET_CUR` before the first URB.**
  Arming it earlier yields a whole URB of empty packets; the camera's audio
  engine needs a few milliseconds after the interface is selected.  The delay
  is inside `start`, so the caller's measurement window begins after the
  device is streaming.
- **Nothing may print while an URB is in flight.**  The console sink holds the
  print lock with interrupts masked; a diagnostic line printed between the
  first submit and the measurement window cost seven 1 ms frames on this
  board (`gaps=7`, `gap_max=7`).  The start banner now prints before the
  first submit; the microphone and video+microphone windows then report
  `gaps=0`.
- Stopping the stream marks the in-flight URB's remaining packets with
  `-USB_ERR_SHUTDOWN`.  Those packets were never attempted and are counted as
  `shutdown`, not as transport errors; the transport-error counter stays 0 on
  a clean run.

The counters are the ledger: `packets`, `urbs`, `bytes`, `gaps` (frames the
host did not schedule between URBs), `err` (packets the controller halted
without a transfer), `empty`, `overrun`, and at stop `shutdown`.  A gap and a
transport error are different defects and are never folded together.

## 4. The camera needs its video function claimed

A microphone-only image (the UAC class alone, no UVC class) enumerates the
audio interfaces, opens alternate setting 1 and sets 16 kHz, but receives
**zero-length packets for the whole window** (`empty=10000`, `bytes=0`).  The
same image with the UVC class loaded and its descriptor census run — no video
stream, just the class claiming interfaces 0/1 and the census reading the mode
table — captures 32 bytes every frame.  This is a camera/firmware property: its
audio function produces samples only after the video function has been
claimed.  The microphone delivery image therefore carries the UVC class and
the `uvc_detect` census; the 10 s window itself measures the microphone only.

A different UAC1 camera may not need this; it is recorded as a bench fact of
`046d:0990`, not as a UAC requirement.

## 5. Video and microphone together

The video+microphone image runs the UVC stream (640x480 MJPEG, interface 1,
endpoint `0x81`) and the microphone stream (interface 3, endpoint `0x86`) in
the same 20 s window.  Each endpoint has its own DWC2 channel (the core
reports 12); the video stream keeps its 64-packet URB ring and the audio stream
its 16-packet one.  Two relay cold boots:

| side | boot 1 | boot 2 |
|---|---:|---:|
| video frames / 20 s | 297 | 297 |
| video bytes | 4,859,221 | 5,352,435 |
| video JPEG SOI/EOI | 297/297 | 297/297 |
| video packet errors / URB errors | 0 / 0 | 0 / 0 |
| video frames dropped | 1 | 1 |
| audio packets / 20 s | 20,003 | 20,004 |
| audio bytes | 640,080 | 640,114 |
| audio gaps / errors / overruns | 0 / 0 / 0 | 0 / 0 / 0 |
| audio non-zero samples | 319,992 | 319,993 |

The video numbers are not below the camera's video-only arm (248–276 frames in
the earlier 20 s windows); the audio is the exact 32 bytes per millisecond
(640,000 bytes in 20 s, plus the in-flight URB at the edges).  The two
isochronous channels do not starve each other.

## 6. Memory and build

| image | FLASH (bin) | static SRAM | SDRAM |
|---|---:|---:|---:|
| `stm32f429-uac-detect` | 58,280 | 107,260 | 0 |
| `stm32f429-uac-mic` | 71,512 | 44,540 | 1,048,576 |
| `stm32f429-uac-av` | 78,564 | 44,540 | 1,441,792 |

The detect image keeps its 64 KiB FreeRTOS heap in SRAM and allocates no PCM
ring.
The microphone and video+microphone images place a 1 MiB heap in SDRAM; the
6,400-byte PCM ring and the 16 × 36-byte packet buffers come out of it at
runtime (so they are inside the SDRAM figure).  The video+microphone image also
places three 128 KiB video frame buffers in SDRAM.  Board limits in
`platform/board/stm32f429-challenger/usb/usb_config.h`: one audio class
instance, 16-packet single-URB ring, 200 ms ring depth.

The F429 RAM-margin gate reports 89,344 B free (detect) and 152,064 B (the
two stream images) between `end` and `_estack`; the 1 MiB flash part is not a
constraint.

## 7. How to reproduce

```bash
cd freewificam
./make.rtos image PROFILE=stm32f429-uac-detect     # descriptor census only
./make.rtos image PROFILE=stm32f429-uac-mic        # 10 s microphone window
./make.rtos image PROFILE=stm32f429-uac-av         # 20 s video + microphone
```

Flash the BIN through the board's Pi-based pyOCD path and cold-boot the
board; the standard reset is the relay cold boot.  The camera is on P2, the USB-A host socket, J74–J76 in the HOS position.  The console is 115200
8N1 on the CH340; the SWD ring mirrors it.

The round helper builds, flashes and judges one image:

```bash
cd freewificam
PROFILE=stm32f429-uac-mic \
UAC_CASES="uac_detect uvc_detect uac_mic" \
tools/rtos/f429-uac-round.sh /tmp/f429-uac/mic
```

`uac_detect` is the first boot for a new camera: its `uac: desc`,
`uac: terminal` and `uac: raw` lines are the descriptor table and the
class-specific bytes.  Do not build a capture profile before reading
`bcdADC`, `dir`, `fmt`, `sub`/`bits` and `mps`.

## 8. Limits and open items

- One camera model, one bus (FS), one UAC1 microphone stream; no hot-plug, no
  disconnect/reconnect, no second audio device.
- No playback: the camera has no UAC OUT, and board audio (WM8978/I2S) is a
  separate line.
- UAC2, 24/32-bit samples, more than one channel, 48 kHz and multi-rate
  switching are outside the class's subset and are refused by the census.
- The camera's audio function needs its video function claimed (section 4);
  the microphone image carries the UVC class for that reason.
- The PCM ring is the handoff point to the network line: the F429 RTSP/TCP
  arm reads 20 ms blocks from `usbh_uac_stream_read()` and sends them to the
  Pi.  This work stops at the ring, not at a network stream.
- The 200 ms ring and the 16-packet URB are a capture cadence, not a jitter
  buffer policy; the future network arm may choose a different block size.