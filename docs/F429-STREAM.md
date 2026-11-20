# F429 camera stream to the Pi 4B

The F429 captures the Logitech camera's MJPEG video and the camera
microphone's 16 kHz PCM and sends them to a Pi 4B over the onboard AP6181 in
station mode. The Pi runs the receiver, the H.264/AAC encoder and an RTSP
server. The F429 does not run an AP, a DHCP server, an RTSP/RTP stack or a
local player.

This page records the implemented shape and the measured state.

## Topology

```
camera --USB OTG_HS (FS)--> F429 --SDIO/AP6181 STA--> AP (Pi 4B) --> receiver
                                                                  --> ffmpeg H.264/AAC
                                                                  --> mediamtx :8554
```

The bench AP is the Pi's `labwifi-ar-ht20s` (2.4 GHz, channel 5, open,
192.168.99.1). The F429 takes a DHCP address from it.

## Wire format

Two TCP connections: video on 5000, audio on 5001. Every message is a
24-byte little-endian header followed by the payload:

| Field | Type | Meaning |
|---|---|---|
| magic `0x46574331` | u32 | fixed |
| version | u8 | 1 |
| type | u8 | 1 video, 2 audio |
| flags | u16 | 0 |
| length | u32 | payload bytes (video <= 128 KiB, audio <= 1280) |
| timestamp_us | u32 | FreeRTOS tick x 1000 |
| seq | u32 | per stream |
| crc32 | u32 | zlib CRC-32 of the payload |

One JPEG per video message; 640 B (20 ms) per audio message while the ring has
data.

## F429 profiles

| Profile | Stage | Content |
|---|---|---|
| `stm32f429-stream-link` | 1 | join + DHCP + 20 ICMP echoes |
| `stm32f429-stream-frame` | 2 | one captured JPEG |
| `stm32f429-stream-video` | 3 | 20 s MJPEG |
| `stm32f429-stream-av` | 4 | 20 s MJPEG + microphone PCM |

All four use the AP6181 WICED port, the first-party UVC/UAC classes and lwIP.
The 1 MiB FreeRTOS heap and the three 128 KiB camera buffers live in the FMC
SDRAM; lwIP's heap and pbuf pools stay in the 192 KiB SRAM, because the
AP6181 transmit path hands pbufs to the SDIO DMA. The image links about
370 KB.

The camera open uses the class's probe/commit and a short raw-ISO smoke before
the stream engine starts, because this camera stays silent if the streaming
endpoint is only opened. The DWC2 isochronous engine must be selected for the
`pi_stream` case (`platform/build_defs.h`).

The link brings the station up with one leave/rejoin retry: on this bench the
first join often associates but never reaches ready-to-transceive, and the
retry is what makes the link usable. DHCP is used for the link stage; a static
fallback is printed when it times out.

## Pi side

`tools/pi-stream/` carries the receiver, the ffmpeg publisher, a mediamtx
configuration and an install script. The receiver validates the header, length
and CRC, writes `video.mjpg` / `audio.pcm` plus the first frame, and feeds
named pipes through bounded queues. The publisher encodes H.264 (libx264
ultrafast, zero-latency) and AAC from the frame/sample counts; mediamtx serves
the RTSP path `cam` on port 8554 and records fMP4.  Both inputs use a larger
ffmpeg thread queue, and the receiver's bounded FIFO queues are sized so a
slow encoder start does not drop audio. The chain was self-tested
with a synthetic source (H.264 640x480 + AAC 16 kHz) before the board run.

## Measured state (2026-11-20)

All four gates passed on two relay cold boots each:

- Link: DHCP address and 20/20 ICMP, 2/2.
- One frame: 18,253 / 18,163 B, CRC match, valid SOI/EOI at the Pi, 2/2.
- Video: 269 frames in 20 s on both boots (13.5 fps), 268 delivered to the
  Pi, zero CRC/short errors, and a 19 s H.264 640x480 RTSP recording each
  time, 2/2.
- Audio+video: 298 frames in 20 s, 20,027 / 20,033 audio packets, `gaps=0`,
  `err=0`, `overrun=0`; the RTSP recording carries 19.6 s H.264 and 19.75 s
  AAC (307/308 AAC packets), 2/2.

The microphone stream runs as 64-packet isochronous urbs and the stream
window has no console output: the print lock masks interrupts, and a single
line is enough to cost the microphone a 1 ms packet.
