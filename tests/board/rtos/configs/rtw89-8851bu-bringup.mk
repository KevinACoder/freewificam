# B ladder, stage 1: USB bring-up + firmware + attach + start only.  The scan /
# join / data cases live in the full profile; staging keeps a bring-up failure
# from being hidden behind a ladder that needs an AP.
CONFIG_BACKEND := freertos
CONFIG_CASES := tick rtw89
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
CONFIG_RTW89_USB_BUS := 3
