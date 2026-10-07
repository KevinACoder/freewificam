# Descriptor for gen-vector.sh: the setup line of the enumeration's first
# descriptor read, GET_DESCRIPTOR(DEVICE) for eight bytes.
seg=S1
dir=exp
seq=1
anchor=S1.EP0_ENUM_SETUP
truth=usb2-ch9.4
source=derived:spec
captured=2026-10-07
redact=none
route=n/a
norm=-
note=consumed by the usb-device-model suite (setup byte comparison)
truth_note=USB 2.0 chapter 9.4 Table 9-2 (standard request fields: bmRequestType 0x80 device to host standard device request, bRequest 0x06 GET_DESCRIPTOR, wValue high byte 0x01 device descriptor, wLength 8)
hex=8006000100000800
