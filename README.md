# freewificam

Run the Linux wireless stack on an RTOS.

`freewificam` takes `net/mac80211` and `net/wireless` **verbatim** and runs them
on top of a small kernel-API layer, with the bus backend and the network egress
replaced. The MAC layer — MLME, aggregation, rate control, encryption, channel
management — is the Linux code, unmodified. What changes is what sits below it
(the bus) and above it (the network stack).

```
   lwIP                                     network egress, replaced
   ────────────────────────────────────
   net/mac80211 + net/wireless              upstream, verbatim
   ────────────────────────────────────
   components/liblinux80211                 kernel API + bus engines
   ────────────────────────────────────
   CherryUSB / PCIe / SDHCI host + driver   bus, replaced
```

## Where the line is

The work now spans two boards.  The **STM32F429** line is the current focus:
it is the first MCU carrying the real mac80211/cfg80211 stack, and it also
carries the UVC camera and the DWC2 full-speed host.  The **RK3568** line is
the Cortex-A55 reference bench where the stack and the per-card drivers were
brought up; its detailed performance records are in
[RK3568-PERFORMANCE.md](docs/RK3568-PERFORMANCE.md).

| board | line | state |
|---|---|---|
| **STM32F429** (Cortex-M4, 168 MHz, 1 MB flash, 192 KB SRAM + 32 MB SDRAM) | UVC camera on the full-speed OTG_HS host | **640x480 and 800x600 2/2**; 12.4-13.8 fps, 1.92-2.07 / 2.90 Mbit/s; **no 720-line mode on the bench camera** (see below) |
| **STM32F429** | UAC1 camera microphone on the same host | descriptor census + **10 s microphone 2/2** + **20 s video+microphone 2/2**; 16 kHz mono 16-bit, 32 B/ms, `gaps=0`, `err=0` (see below) |
| **STM32F429** | Wi-Fi: liblinux80211 + rtl8xxxu on USB | attach/scan/join/data 2/2; report-only 2.8 Mbit/s TCP down, 6.1 Mbit/s UDP down |
| **STM32F429** | USB host MSC + SDRAM | 2/2; U-disk block I/O and the DMA-to-SDRAM arm |
| **STM32F429** | AP6181 SDIO (Cypress WICED port) | attach/scan/join/ICMP 2/2; report-only TCP TX 6.06 / RX 9.83 Mbit/s |
| **STM32F429** | Wired Ethernet: LAN8720A (RMII) + the imported ST HAL ETH driver | 2/2; 100 Mbit/s full duplex, DHCP lease from the router, gateway 10/10, one public target 10/10 per boot (see below) |
| RK3568 (Cortex-A55) | mac80211/cfg80211 + iwlwifi, AC 7260 PCIe | C0-C6 2/2; TCP down 142.6%, up 93.3% of Linux; AP down 99.9%, up 57.3% |
| RK3568 | RTL8189FTV SDIO, vendor stack | station 2/2, AP 2/2; TCP down 69-73% of Linux |
| RK3568 | RTL8851BU USB, rtw89 | station/AP/monitor run; AP TCP down 116.9%, up 78.4% (one gate failed) |
| RK3568 | platform acceptance (net, PCIe/NVMe, eMMC, USB storage) | 2/2 each, judged |

`docs/STATUS.md` states each stage as what it is; `docs/BUILD.md` records the
build's design and how a new world is added.

## STM32F429 wired Ethernet

The board's RJ45 is a LAN8720A in RMII mode on the STM32F4 ETH MAC.  The MAC,
the DMA descriptor rings, MDIO and the PHY auto-negotiation are ST's STM32F4 HAL
ETH driver, imported verbatim into `third-party/stm32f4xx-hal/`; the board seam
(clocks, AF11 pins, UID-derived MAC, the RTOS HAL time base), the lwIP netif
bridge and the `f429_eth` ladder are written here.  One boot brings the link up
at 100 Mbit/s full duplex, takes a DHCP lease from the router, resolves one
name (report only) and pings the gateway and a public address; the reachability
verdict `f429_eth: PASS DATA` is the public echo reply.  Full details:
[F429-ETH.md](docs/F429-ETH.md).

## STM32F429 UVC camera

The F429's only host port is OTG_HS in **full-speed** mode (PB14/PB15, P2
USB-A, J74-J76 on HOS).  The bench camera is a **Logitech QuickCam Pro 9000
(`046d:0990`)**.  A full-speed isochronous endpoint carries at most 1023 bytes
per 1 ms frame - **8.184 Mbit/s** - and the camera's full-speed descriptor set
has no 720-line mode, so 720p is a camera-table negative here, not a bus
result.

### Adaptation parameters

| item | value |
|---|---|
| board / host | STM32F429 挑战者 V2, OTG_HS full-speed host, P2 USB-A, PB14/PB15, J74-J76 HOS |
| camera | Logitech QuickCam Pro 9000, `046d:0990` |
| UVC descriptors | 3 formats, 7 streaming alternate settings; MJPEG 160x120, 176x144, 320x240, 352x288, 640x480, 800x600 |
| frame intervals | 30 fps at 160x120/176x144; 15 fps at 320x240/352x288/640x480/800x600 |
| streaming alts | 192 / 384 / 512 / 640 / 800 / 944 bytes per frame, bInterval 1 |
| full-speed ceiling | 1023 B/1 ms = 8.184 Mbit/s |
| 640x480 open | alt 4, `mps=640`, `dmp=640`, interval 1 |
| 800x600 open | alt 6, `mps=944`, `dmp=944`, interval 1 |
| DWC2 ISO engine | patch 0011; one `iso_packet[]` per channel activation; per-packet `actual_length`/`errorcode`; one channel per endpoint; sync + async |
| stream ring | 1 URB x 64 packets (64 ms of bus time), one URB in flight |
| board limits | `MAX_INTF_ALTSETTINGS=16`, request buffer 2048 B, `MAX_VIDEO_CLASS=1` |
| memory | 1 MiB FreeRTOS heap + 3 x 128 KiB frame pool in FMC SDRAM (720p image: 3 x 512 KiB) |
| profiles | `stm32f429-uvc-detect`, `-480`, `-800x600`, `-720p` |

### Measured

| mode | result | frames / 20 s | rate | payload | drops / errors |
|---|---|---:|---:|---:|---:|
| 640x480 MJPEG | **2/2** | 248 / 276 | 12.38 / 13.76 fps | 1.92 / 2.07 Mbit/s | 1 / 0 each |
| 800x600 MJPEG | **2/2** | 275 / 275 | 13.71 / 13.73 fps | 2.90 / 2.90 Mbit/s | 1 / 0 each |

Every delivered frame carried a JPEG SOI/EOI; the raw ISO smoke is 16/16 URBs
with zero packet errors.  The 800x600 arm uses 2.9 of the 8.184 Mbit/s ceiling.

**720p:** the full-speed descriptor set has `720_line_modes=0` (no 1280x720 and
no 960x720).  The `-720p` image boots once as an expected negative:
`uvc: open FAIL mode 1280x720 mjpeg not in the camera's tables`.  A different
camera that exposes a 720-line mode at a low frame interval and reports
`dwMaxPayloadTransferSize <= 1023` would fit the bus; the profile is kept for
that camera.  Full details: [F429-UVC.md](docs/F429-UVC.md).

## STM32F429 UAC microphone

The same camera's interfaces 2/3 are a **USB Audio Class 1.0 microphone**
(`bcdADC=0x0100`): one channel, 16-bit samples, one 16 kHz rate, isochronous
IN endpoint `0x86` with `wMaxPacketSize=36` and `bInterval=1`.  The F429 line
carries a first-party UAC1 host class (patch 0012) and a PCM capture engine;
the camera has **no UAC OUT**, so playback is not part of this line.

A microphone-only image receives zero-length packets on this camera: its audio
function produces samples only after the video function has been claimed.  The
microphone image therefore carries the UVC class and its descriptor census;
the 10 s window itself measures the microphone only.

### Adaptation parameters

| item | value |
|---|---|
| audio function | AC interface 2, AS interface 3, `bcdADC=0x0100`, protocol 0x00 |
| microphone path | input terminal 1 (`0x0201`), output terminal 3 (`0x0101`), feature unit 5 (master mute+volume) |
| format | format type I PCM, 1 channel, 2 bytes/sample, 16 bits, one rate **16000 Hz** |
| endpoint | `0x86` ISO IN, `mps=36`, `bInterval=1`; sampling-frequency control present |
| class requests | `SET_INTERFACE` alt 1, `SET_CUR`/`GET_CUR` sampling frequency (3 bytes, endpoint recipient) |
| capture ring | 1 URB x 16 packets (16 ms), one URB in flight, 200 ms PCM ring (6,400 B) |
| resubmit | from the URB-completion callback (interrupt context), not a pump task |
| board limits | `MAX_AUDIO_CLASS=1`, 16-packet URB, 200 ms ring |
| memory | 1 MiB FreeRTOS heap in FMC SDRAM; the 6.4 KiB ring and 16 x 36 B packet buffers come from it |
| profiles | `stm32f429-uac-detect`, `-mic`, `-av` |

### Measured

| window | result | numbers |
|---|---|---|
| 10 s microphone | **2/2** | 10,000 packets, 319,994 / 319,992 bytes, `gaps=0`, `err=0`, non-zero samples 159,997 / 159,988 |
| 20 s video + microphone | **2/2** | video 297 / 297 frames, JPEG SOI/EOI 297/297, 0 packet/URB errors; audio 20,003 / 20,004 packets, 640,080 / 640,114 bytes, `gaps=0`, `err=0` |

Both microphone boots and both video+microphone boots are relay cold starts of
the same image.  The video stream is not degraded by the microphone; the two
isochronous endpoints each hold their own DWC2 channel.  Full details: the
descriptor bytes, the class's refusals, the engine's contract and the commands
are in [F429-UAC.md](docs/F429-UAC.md).

## Footprints

The F429 camera images are the small end of the line: 58,280-78,564 B of
flash, 44,540-107,260 B of static SRAM and up to 1.4 MiB of SDRAM.  The Wi-Fi delivery image
is the large end (`stm32f429-wifi-c6`, 726,080 B of flash, 81,356 B of static
SRAM - 18,496 B of it the DMA/no-cache staging window - and 4,456,448 B of
SDRAM: 4 MiB FreeRTOS heap + 256 KiB lwIP reserve).  LTO brings that flash to
689,648 B.  In it CherryUSB's host core + hub + DWC2 port is 12,918 B flash /
20,896 B RAM, and liblinux80211's URB engine + skb pool is 41,359 B flash /
30,723 B RAM / 18,440 B no-cache.

| image | FLASH | static SRAM | SDRAM |
|---|---:|---:|---:|
| `stm32f429-uvc-detect` | 58,836 | 107,260 | 0 |
| `stm32f429-uvc-480` | 69,644 | 50,188 | 1,441,792 |
| `stm32f429-uvc-800x600` | 69,652 | 50,188 | 1,441,792 |
| `stm32f429-uac-detect` | 58,280 | 107,260 | 0 |
| `stm32f429-uac-mic` | 71,512 | 44,540 | 1,048,576 |
| `stm32f429-uac-av` | 78,564 | 44,540 | 1,441,792 |
| `stm32f429-wifi-c6` delivery | 726,080 | 81,356 | 4,456,448 |
| `stm32f429-wifi-c6t` throughput | 735,936 | 81,484 | 4,456,448 |
| `stm32f429-wifi-c6` + `CONFIG_OPT_LTO` | 689,648 | 80,212 | 4,456,448 |

The per-component table, the pre-link per-file USB table and the measured task
stack high-water marks are in [docs/FOOTPRINTS.md](docs/FOOTPRINTS.md).

## Performance

The two boards are measured on separate benches and are not interchangeable.
The **STM32F429 (MCU)** line is the current focus: its camera parameters and
measured frames/rates are in the section above; its Wi-Fi numbers (2.8 Mbit/s
TCP down, 6.1 Mbit/s UDP down, report-only) are in
[F429-WIFI.md](docs/F429-WIFI.md).  The RK3568 line keeps a short selection
below; the full per-module tables, the case registry, the 30-minute stability
window and the table limits are in
[RK3568-PERFORMANCE.md](docs/RK3568-PERFORMANCE.md).

### Selected RK3568 results

| board / role | case | rtos (Mbit/s) | Linux / GT (Mbit/s) | vs reference |
|---|---|---|---|---|
| RK3568 / AC 7260 station | TCP down | 62.7 / 58.2 | 42.3 / 42.5 | **142.6%** |
| RK3568 / AC 7260 station | TCP up | 25.6 / 20.7 | 26.7 / 23.0 | 93.3% |
| RK3568 / AC 7260 AP | TCP down | 72.7 / 73.3 | 73.3 | **99.9%** |
| RK3568 / RTL8189FTV station | TCP down | 30.5 (best arm) | 41.8 | 73% (best arm) |
| RK3568 / RTL8851BU AP | TCP down | 6.64 / 5.91 | 5.37 | **116.9%** |

A cell is one cold boot (where two are printed, the pair); payload bytes over
the interval the flow actually ran, counted at the receiver; zero loss unless
stated.  The full tables give the per-boot repeats, loss figures and the
identities of the Linux/GT arms.

## Why

Porting a Linux wireless driver to an RTOS is well-trodden: the driver talks to
the kernel through a kernel-API compatibility layer, and the layer is small
because a driver uses a bounded slice of the kernel. Porting the *stack* is the
part that is usually avoided — a stack leans on scheduling, memory lifetime and
locking models that an RTOS does not have, and reimplementing it drifts from the
code being tested.

This repository tests the other split: keep the stack, replace the two edges.

## Requirements

- **Single core.** The RTOS side is a uniprocessor FreeRTOS port. Every
  read-copy-update primitive the stack uses is a plain load/store there, which
  is equivalent rather than approximate *because* there is no other core to be
  concurrent with. This is a precondition of the design, not an implementation
  detail; a multiprocessor build needs the whole RCU discipline re-done.
- **A bus backend** for the device you target (USB host stack, a PCIe host, an
  SDHCI/eMMC host).
- **An egress** to your network stack (lwIP is the intended one).

## Layout

| Path | Contents |
|---|---|
| `tests/board/rtos/platform/` | RK3568 AArch64 and STM32F429 armv7m board ports, boot layers, FreeRTOS/CMSIS-RTOS2 runtime |
| `tests/board/rtos/app/` | the case runner, the acceptance cases and the F429 UVC application half |
| `tests/board/rtos/build/` | the platform Makefile and the per-world fragments (`usb.mk`, `uvc.mk`, `kpi.mk`, ...) |
| `tests/board/rtos/configs/` | the profiles for both boards (one defconfig each) |
| `third-party/` | FreeRTOS, CMSIS, lwIP, CherryUSB (host classes + DWC2/EHCI/xHCI ports), fsl_sdmmc, the STM32F4 HAL ETH closure, printf, tlsf, ulog (compiled closures, pins in `docs/PROVENANCE.md`) |
| `components/` | first-party components; `liblinux80211` is the Linux stack import |
| `wireless-drivers/` | the vendored card drivers (`rtl8xxxu`, `rtw88`, `rtw89`, the AP6181 WICED port) |
| `tools/` | the Kconfig generator, the build gates, the judge, the commit checker |
| `docs/` | provenance, divergences, licenses, build design, status, the F429 camera/audio/Wi-Fi/USB facts and the RK3568 performance tables |

## Build and boot

```
# RK3568 (Cortex-A55, AArch64)
AARCH64_CROSS_PATH=<xpack prefix> ./make.rtos list
AARCH64_CROSS_PATH=<xpack prefix> ./make.rtos image PROFILE=rk3568-emmc

# STM32F429 (Cortex-M4, armv7m; xPack arm-none-eabi-gcc 13.2.1)
./make.rtos image PROFILE=stm32f429-uvc-detect
./make.rtos image PROFILE=stm32f429-uvc-480
./make.rtos image PROFILE=stm32f429-wifi-c6
```

The image is `tests/board/rtos/build/build/rtos-freertos.bin`.  On RK3568 it
is a flat raw image linked at `0xa000000`, loaded over TFTP and entered with
`go 0xa000000`; the board-side control (serial, power, TFTP, evidence) is the
project's RK3568 board tooling.  On STM32F429 it is a flat image linked at
`0x08000000`, flashed over the board's Pi/pyOCD path and relay-cold-booted; the
camera-side commands and the observed console anchors are in
[F429-UVC.md](docs/F429-UVC.md) and [F429-UAC.md](docs/F429-UAC.md), and the
Wi-Fi line's in [F429-WIFI.md](docs/F429-WIFI.md).

## License

The platform, the build and the tools written here are `BSD-3-Clause`. The
imported trees keep their own terms (MIT, Apache-2.0, BSD-3-Clause, ISC); see
`docs/LICENSES.md`.

The F429 UVC images link only the Apache-2.0 CherryUSB host stack/video class
and first-party BSD-3-Clause code; they contain no GPL unit.  Images that link
the real `net/mac80211`/`net/wireless` (`GPL-2.0-only`) are `GPL-2.0` combined
works regardless of the licenses around them.  That is why
`components/liblinux80211` is a separate build unit and profile family: the
Realtek BSD-option profiles must never link it.
