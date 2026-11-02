# K2-06 attribution arm: the T2c image (A6 + zero-copy RX + TX checksum) with
# the five-segment per-frame instrument ON. This is an instrumented boot, not a
# baseline: the buckets cost 11.3% on A6 (K2-05), so every number from here is
# labelled accordingly. Its only job is to answer "after zero-copy, where do
# the remaining ~15.1 us/frame go" - irq, sweep, copy, feed, app - so the next
# single-variable arm attacks the largest term instead of guessing.
include $(LINE)/configs/net-netio-zc-txc.mk

CONFIG_GMAC_PROF := 1
