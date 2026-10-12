# Known limitations

This is the current state of the stack. It is honest about what works, what is
weak, and what is not started. Nothing here should be read as a promise.

## The wireless lines

| Line | State |
|---|---|
| RTL8188EUS (`rtl8xxxu`) | Full chain works on hardware: probe, scan, WPA2, DHCP, ping, TCP data. Throughput is well below the Linux same-card figure. |
| RTL8821CU (`rtw88`) | Full chain works on hardware, including a CD-ROM to WLAN mode switch at boot. Throughput is well below Linux. |
| RTL8851BU (`rtw89`) | **Experimental.** The full chain has been reached on hardware, but join is intermittent and the link is poor. Not on the mainline branch; published as experimental. |
| RTL8189FTV (SDIO) | Full chain works on hardware: probe, scan, WPA2, DHCP, ping, TCP data. The vendor driver carries its own MLME and talks to the port's cfg80211 and netdev faces. Throughput is the closest to Linux of the four lines, but the least stable. |

- **Throughput.** Every line is well below the same card under Linux. The RTOS
  figure is roughly a tenth of the Linux figure on the 8188EUS line, about a
  quarter on the SDIO line, and roughly a hundred-and-seventieth on the 8851BU
  line. The SDIO line is the least stable of the four: one of its measured runs
  collapsed about nine-fold, with the shape of a power-management stall. The
  numbers are recorded, not gated; the project does not yet claim a target.
- **Long-run stability.** A long, unattended streaming run (the project's core
  goal) has not been demonstrated. The 8851BU line in particular is not stable
  enough for it.
- **Reverse direction.** Board-to-host throughput needs a host firewall change
  to measure; it is not in the current figures.

## The camera line

The USB camera (UVC / isochronous) line is a headline milestone but has no
board evidence yet. The USB host stack in the tree does not include an
isochronous transfer path for the open-source controller, and the camera class
is not ported. Treat the camera side as not started.

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
