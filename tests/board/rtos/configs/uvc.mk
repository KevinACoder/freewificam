# The USB camera image: one USB2 root, the video class, the isochronous engine
# and the four camera cases.  No other class is linked (the hub is), so a
# bring-up failure names the camera rather than a neighbour on the controller.
#
# The controller is the first USB2 root; the profile names it because the
# camera's port is a board fact, not a driver fact - the run record holds the
# cable position it was measured at.
CONFIG_BACKEND := freertos
CONFIG_CASES := uvc_detect uvc_open uvc_iso_smoke uvc_stream
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 4M
CONFIG_FREERTOS_HEAP_SIZE := 1048576u
CONFIG_UVC_USB_BUS := 0
