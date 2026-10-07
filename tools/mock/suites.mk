# Gate1 suite registry: the container runs these suites, in this order.
#
# Written in make assignment syntax so the same file can be included by a Makefile
# aggregator as well as read by tools/mock/run.sh.

SUITES := container-selftest threadx-host-smoke threadx-regression freertos-kernel-unit net80211-frame-mock freertos-host-smoke usb-device-model skbuff-stress net80211-port-contract
