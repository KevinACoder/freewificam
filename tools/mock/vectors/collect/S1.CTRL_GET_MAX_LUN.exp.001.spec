# Descriptor for gen-vector.sh: the Get Max LUN request of the mass-storage
# transport (one byte back, device to host).
seg=S1
dir=exp
seq=1
anchor=S1.CTRL_GET_MAX_LUN
truth=usbmsc-bot-1.0-ch3.2
source=derived:spec
captured=2026-10-07
redact=none
route=n/a
norm=-
note=consumed by the linuxkpi-usb-test suite (setup byte comparison)
truth_note=USB MSC bulk-only transport rev 1.0 chapter 3.2 (class-specific request 0xFE Get Max LUN, interface recipient, device to host, wLength 1)
hex=a1fe000000000100
