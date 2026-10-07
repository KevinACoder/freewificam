# Descriptor for gen-vector.sh: the class reset request of the mass-storage
# transport (no data stage).
seg=S1
dir=exp
seq=1
anchor=S1.CTRL_MS_RESET
truth=usbmsc-bot-1.0-ch3.1
source=derived:spec
captured=2026-10-07
redact=none
route=n/a
norm=-
note=consumed by the usb-device-model suite (setup byte comparison)
truth_note=USB MSC bulk-only transport rev 1.0 chapter 3.1 (class-specific request 0xFF Mass Storage Reset, interface recipient, host to device, wLength 0)
hex=21ff000000000000
