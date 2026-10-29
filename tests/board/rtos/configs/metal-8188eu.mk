# Baremetal cooperative kernel (no tick, no preemption) driving the RTL8188EUS
# stack. First milestone: the driver probe/firmware path runs from a thread and
# the scheduler loop stays alive; the networked cases come after that.
CONFIG_BACKEND := metal
CONFIG_CASES := rtl8xxxu
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_RTL8XXXU_USB_BUS := 3
CONFIG_RTL8XXXU_EXPECT_MAC := 20:f4:1b:52:8f:01
CONFIG_RTL8XXXU_AP_SSID := labwifi-mtk24
CONFIG_RTL8XXXU_AP_FREQ := 2432
