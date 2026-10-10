# Suite declaration: the mac80211 ops-shape dispatch.
#
# The harness binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it.  It drives the substitute body
# with scripted drivers that declare each op family the interface allows, and
# reports every case as an anchor line (`SHAPE: <case> PASS|FAIL`).

SUITE_NAME := linuxkpi-80211-shape-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -C tests/host/linuxkpi-80211-shape-test
SUITE_RUN := sh tools/mock/linuxkpi-80211-shape-test/check.sh
SUITE_TRUTH_SOURCE := upstream net/mac80211 driver-ops.c:drv_sta_state (sta_state for every transition; sta_add at AUTH->ASSOC and sta_remove at ASSOC->AUTH for a driver without it) and main.c:203-254 (the vif-cfg bits to vif_cfg_changed, the rest to link_info_changed with the vif's own bss_conf, and bss_info_changed called as well when the driver declares it), plus link.c:ieee80211_link_init for the single default link a vif carries
SUITE_SEGMENTS := S1.SHAPE,M.OPS
SUITE_WIRELESS := 1
SUITE_ROUTE := n/a
