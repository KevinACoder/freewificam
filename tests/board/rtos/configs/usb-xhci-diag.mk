# Diagnostic arm for the xHCI mass-storage line: eight sequential 4 KiB reads
# with an 800 ms class timeout and the port's timeout dump compiled in.  Not a
# delivery profile - it names whether a completion interrupt stopped arriving
# (irq standing still, ms=800) or the device/stack is merely slow.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick usb_xhci
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 1M
CONFIG_FREERTOS_HEAP_SIZE := 1048576u
CONFIG_USB_MSC_DIAG := 1
