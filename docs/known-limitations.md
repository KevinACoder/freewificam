# Known limitations

This is the current state of the stack. It is honest about what works, what is
weak, and what is not started. Nothing here should be read as a promise.

## The wireless lines

| Line | State |
|---|---|
| RTL8188EUS (`rtl8xxxu`) | Full chain works on hardware: probe, scan, WPA2, DHCP, ping, TCP data. Throughput is well below the Linux same-card figure. |
| RTL8821CU (`rtw88`) | Full chain works on hardware, including a CD-ROM to WLAN mode switch at boot. Throughput is well below Linux. |
| RTL8851BU (`rtw89`) | **Experimental.** The full chain has been reached on hardware, but join is intermittent and the link is poor. Not on the mainline branch; published as experimental. |
| RTL8189FTV (SDIO) | Full chain works on hardware: probe, scan, WPA2, DHCP, ping, TCP data. The vendor driver carries its own MLME and talks to the port's cfg80211 and netdev faces. Throughput is the closest to Linux of the lines, and its reverse direction is measured; it also runs as an open access point a station can associate to. Its access-point-mode reverse direction is still variable run to run. |

- **Throughput.** Every line is below the same card under Linux. The RTOS
  figure is roughly a tenth of the Linux figure on the 8188EUS line, about
  1/1.7 (down) and 1/1.9 (up) on the SDIO line after two adopted levers, and
  roughly a hundred-and-seventieth on the 8851BU line. The SDIO line's earlier
  nine-fold collapse has not recurred since the receive-aggregation work, but
  its access-point-mode reverse direction is variable across boots (14-18
  Mbit/s in three of four, one abort). The numbers are recorded, not gated;
  the project does not yet claim a target.
- **Long-run stability.** A long, unattended streaming run (the project's core
  goal) has not been demonstrated. The 8851BU line in particular is not stable
  enough for it.
- **Reverse direction.** Board-to-host throughput is measured on the SDIO line
  (the board runs the iperf client against a host server on the measurement
  host, so no host firewall change is involved). The other lines' reverse
  directions are not measured yet.

## The camera line

The USB camera (UVC / isochronous) line has board evidence: a Logitech Pro 9000
on the reference board is enumerated, opened and streaming MJPEG 640x480 over
its isochronous endpoint (267-275 frames per 20-second window over cold boots,
about 13.5 fps and 1.9 Mbit/s). The isochronous transfer engine and the video
class's streaming half are not in the vendored stack's open-source tier and
travel as patches to it.

What is not shown: the failure model is not understood well enough to trust it.
A stream stop that takes the camera out of its streaming setting (the class
close's `SET_INTERFACE(0)`) can leave this camera refusing the video-streaming
class requests for the rest of the boot, and the recovery sequences measured so
far are not reproducible. The workaround the port ships is to avoid provoking
it - a camera the caller opened is left open and in its streaming setting
across a stream, so the engine's later start issues no class requests - rather
than to establish why the camera behaves this way or to make the close path
protocol-perfect. Onboard-only, single camera model, and the reverse (camera to
WiFi) direction and long unattended runs are not demonstrated.

## The bus and platform coverage

- **USB** and **SDIO** are brought up on hardware.
- **PCIe** has an interface layer designed and drafted; no hardware bring-up.
- **One board.** All hardware results are from a single RK3568 ITX board. The
  MCU tier (a Cortex-M class target) is a stated goal and a design constraint
  (no assumption of an MMU, 64-bit or large memory) but has not been validated
  on hardware.

## Other

- **Log levels.** The console level is settable at run time, but the supplicant
  and the USB stack keep their own compile-time levels, which the console level
  does not reach.
- **The supplicant** is a port of a general-purpose supplicant; it is brought
  up for WPA2-PSK on the board, and its wider feature set is untested.
- **No upstream submission yet.** The driver interface faces and the port
  layers are written to be upstreamable, but none has been submitted.
