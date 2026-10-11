# Known limitations

This is a 0.1 snapshot. It is honest about what works, what is weak, and what
is not started. Nothing here should be read as a promise.

## The wireless lines

| Line | State |
|---|---|
| RTL8188EUS (`rtl8xxxu`) | Full chain works on hardware: probe, scan, WPA2, DHCP, ping, TCP data. Throughput is well below the Linux same-card figure. |
| RTL8821CU (`rtw88`) | Full chain works on hardware, including a CD-ROM to WLAN mode switch at boot. Throughput is well below Linux. |
| RTL8851BU (`rtw89`) | **Experimental.** The full chain has been reached on hardware, but join is intermittent and the link is poor. Not on the mainline branch; published as experimental. |
| RTL8189FTV (SDIO) | Design only. No board evidence. |

- **Throughput.** All three lines are far below the same card under Linux. The
  gap is the current open problem, not a tuning detail: on the fastest line the
  RTOS figure is roughly a tenth of the Linux figure, and on the 8851BU line it
  is roughly a thousandth. The numbers are recorded, not gated; the project
  does not yet claim a target.
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

- **USB** is the only bus brought up on hardware.
- **PCIe** and **SDIO** interface layers are designed and drafted; no hardware
  bring-up.
- **One board.** All hardware results are from a single RK3568 ITX board. The
  MCU tier (a Cortex-M class target) is a stated goal and a design constraint
  (no assumption of an MMU, 64-bit or large memory) but has not been validated
  on hardware.

## Other

- **One RTOS-visible log level mechanism** is in progress, not in this tag.
- **The supplicant** is a port of a general-purpose supplicant; it is brought
  up for WPA2-PSK on the board, and its wider feature set is untested.
- **No upstream submission yet.** The driver interface faces and the port
  layers are written to be upstreamable, but none has been submitted.
