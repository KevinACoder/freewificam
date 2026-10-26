# The full U-disk matrix under the diagnostic settings: short class timeout
# (800 ms) and the xHCI port's timeout dump.  Runs the same arms as the
# delivery profile; the short timeout turns a lost completion into a bounded
# 0.8 s wait plus a dump instead of a five-second one.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick usb_xhci
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 1M
CONFIG_FREERTOS_HEAP_SIZE := 1048576u
CONFIG_USB_MSC_DIAG := 1
CONFIG_USB_MSC_DIAG_FULL := 1
