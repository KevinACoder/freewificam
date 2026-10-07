# Descriptor for gen-vector.sh: the device descriptor as the scripted device
# model serves it for an eighteen-byte read.
seg=S1
dir=in
seq=1
anchor=S1.EP0_DESC_DEV
truth=usb2-ch9.6.1
source=derived:spec
captured=2026-10-07
redact=none
route=n/a
norm=-
note=consumed by the usb-device-model suite (descriptor byte comparison)
truth_note=USB 2.0 chapter 9.6.1 Table 9-8 (device descriptor layout); the identifiers are test constants, not a claim about a real product
hex=120100020000004034127856000100000001
