# Descriptor for gen-vector.sh: the first command block of the run, a TEST
# UNIT READY (operation code 0x00), tag 1, no data stage, direction out.
seg=S1
dir=exp
seq=1
anchor=S1.BOT_CBW
truth=usbmsc-bot-1.0-ch5.1
source=derived:spec
captured=2026-10-07
redact=none
route=n/a
norm=-
note=consumed by the usb-device-model suite (command block byte comparison)
truth_note=USB MSC bulk-only transport rev 1.0 chapter 5.1 (command block wrapper: 31 bytes, signature USBC, tag, data transfer length, flags, LUN, CB length, command block)
hex=55534243010000000000000000000600000000000000000000000000000000
