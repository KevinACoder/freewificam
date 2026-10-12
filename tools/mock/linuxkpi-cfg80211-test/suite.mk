# SPDX-License-Identifier: BSD-3-Clause
#
# @file    suite.mk
# @brief   Suite declaration for the cfg80211 substitute suite.
# @design  the suite registry's declaration fields, mirroring the sibling body
#          suite (tools/mock/linuxkpi-body-test/suite.mk:1-17): the same field set
#          with this suite's build and run commands.
# @date    12.10.2026
#
# Suite declaration: the cfg80211 substitute driven through its downward seam.
#
# The suite binary is built by its own Makefile under tests/host/ - the container
# must not grow a second copy of it. It links the substitute body, the net-device
# registry it looks its interface up in and the skbuff pool its vendor-reply
# surface allocates from, registers a wiphy and a net device through the
# substitute, and reports every case as an anchor line (`CFG: <id> PASS|FAIL`).

SUITE_NAME := linuxkpi-cfg80211-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/linuxkpi-cfg80211-test
SUITE_RUN := sh tools/mock/linuxkpi-cfg80211-test/check.sh
SUITE_TRUTH_SOURCE := the seam header's per-entry contract (components/linuxkpi/linuxkpi_cfg80211.h), the cfg80211 records the driver's ops receive (the wiphy, the scan/connect request records and the key parameters), and the 802.11 RSN/WPA element layout the connect call reads its security selection from
SUITE_SEGMENTS := -
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a
