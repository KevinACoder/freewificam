# Suite declaration: the software transmit-queue scheduler.
#
# The harness binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it.  It drives the txq entry points
# of the mac80211 face against the real engine and the real skb pool, and
# reports every case as an anchor line (`TXQ: <case> PASS|FAIL`).

SUITE_NAME := linuxkpi-txq-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -C tests/host/linuxkpi-txq-test
SUITE_RUN := sh tools/mock/linuxkpi-txq-test/check.sh
SUITE_TRUTH_SOURCE := upstream net/mac80211 tx.c (ieee80211_next_txq:3955 taking the head of the AC's active list once per round; __ieee80211_schedule_txq:4017 appending at the tail while the queue has frames; ieee80211_txq_schedule_start:4160 bumping the round counter, 0 meaning no round; txq_init:1522 for the TID/AC of each queue) and wme.c:23-32 for the 802.1D priority to access-category map
SUITE_SEGMENTS := S1.TXQ,M.QUEUE
SUITE_WIRELESS := 1
SUITE_ROUTE := n/a
