# A2: the U-disk on the panel's USB2 roots - the controller family the 8851BU
# sits on, so this is the target path's throughput ceiling.  The wrapper tries
# busid 1 then 0 and names the root it measured.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick usb_ehci
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 1M
CONFIG_FREERTOS_HEAP_SIZE := 1048576u
