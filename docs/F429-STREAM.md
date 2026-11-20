# F429 camera stream to the Pi 4B (development status)

The F429 captures the Logitech camera's MJPEG video and the camera
microphone's 16 kHz PCM and sends them to a Pi 4B over the onboard AP6181 in
station mode. The Pi runs the receiver, the H.264/AAC encoder and an RTSP
server. The F429 does not run an AP, a DHCP server, an RTSP/RTP stack or a
local player.

This page records the implemented shape and the measured state. It is not a
claim that the full audio+video gate has passed.

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
ultrafast, zero-latency) and AAC with wall-clock timestamps; mediamtx serves
the RTSP path `cam` on port 8554 and records fMP4. The chain was self-tested
with a synthetic source (H.264 640x480 + AAC 16 kHz) before the board run.

## Measured state (2026-11-20)

- Link: 2/2 cold boots, DHCP address, 20/20 ICMP.
- One frame: 2/2 cold boots; 11,457 / 11,346 B, CRC match, valid SOI/EOI at
  the Pi.
- Video: the Pi received 62 frames in 20 s (3.1 fps), zero CRC/short errors,
  and mediamtx recorded a playable H.264 640x480 stream. The 200-frame gate
  was not met because the camera was pointed at a dark window; the same
  camera and the control UVC image produced 58 frames in 20 s at the same
  time, versus 248 frames in the earlier lit scene.
- Audio+video: not yet run.

## Next

Light the scene (or fix the camera's exposure), confirm the control image
reaches the frame floor, then rerun video and audio+video. The frame-rate
floor is a scene property, not a transport property; the transport's CRC and
continuity counters are clean.
