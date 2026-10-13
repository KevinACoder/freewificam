# Gate1 suite registry: the container runs these suites, in this order.
#
# Written in make assignment syntax so the same file can be included by a Makefile
# aggregator as well as read by tools/mock/run.sh.
#
# Default set = the suites that cover this repository's own code.  Fast enough to
# run on every change; `make mock SUITES_FILE=...` or a named suite runs a subset.
SUITES := container-selftest ulog-level-test linuxkpi-time-semantics-test linuxkpi-wiphy-work-test linuxkpi-txq-test linuxkpi-80211-shape-test threadx-cmsis2-smoke freertos-cmsis2-smoke net80211-port-test net80211-frame-test linuxkpi-usb-test linuxkpi-body-test cherrysh-port-smoke skbuff-allocation-free-stress linuxkpi-usb-urb-stress lwip-port-build rtl8xxxu-port-build rtw89-port-build rtw88-port-build rtl8189fs-port-build wlan-bridge-test wpa-port-test fsl-sdmmc-port-build fsl-sdmmc-adapter-test linuxkpi-sdio-test linuxkpi-cfg80211-test pcie-backend-build pcie-storage-test linuxkpi-pcie-test pcie-adapter-test iwlwifi-port-build ehci-iso-schedule-test

# The upstream kernels are vendored and do not change with this repository's work,
# so their regression suites are a separate, deliberate run: `make mock-kernels`.
# They are the slow ones (tens of seconds each); a change under third-party/ is
# what warrants running them.
SUITES_KERNELS := threadx-regression freertos-kernel-unit
