# Suite declaration: the polling budget and the completion layout.
#
# The harness binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it.  It drives the contract header's
# polling macros and the completion object against the real linuxkpi body, and
# reports every case as an anchor line (`WIPHYWORK: <case> PASS|FAIL`).

SUITE_NAME := linuxkpi-wiphy-work-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -C tests/host/linuxkpi-wiphy-work-test
SUITE_RUN := sh tools/mock/linuxkpi-wiphy-work-test/check.sh
SUITE_TRUTH_SOURCE := upstream net/wireless/core.c (cfg80211_process_wiphy_works: one ordered list drained by one worker; wiphy_work_queue's idempotent list insert; cancel unlinking or waiting for the running callback; flush processing on the caller's thread; the delayed variant's timer)
SUITE_SEGMENTS := S1.WIPHYWORK,M.WORK
SUITE_WIRELESS := 0
SUITE_ROUTE := n/a
