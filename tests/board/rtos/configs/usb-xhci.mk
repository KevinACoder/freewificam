# A1: the U-disk on an xHCI root (USB3 Type-A ports, High-Speed only on this
# board).  One controller family per image: the mass-storage instance lookup
# returns the first ready disk, so a second bus in the same image could be
# measured by mistake.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick usb_xhci
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 1M
CONFIG_FREERTOS_HEAP_SIZE := 1048576u
