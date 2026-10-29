# netio arm: the throughput instrument on the wired bench (PLAT-01).
#
# The board runs the server half of the netio program (the MDK-Middleware
# implementation, ported to lwIP in app/cases/net_mdk_netio.c); the Pi runs the
# unmodified upstream NETIO 1.33 client. Both speak the same wire protocol, so
# one component - the client - is identical across every arm, which is what
# makes readings comparable.
#
# One boot serves any number of connections, so a single image can be measured
# with several packet sizes (`netio -b`) and both protocols, and the difference
# is attributable to what the client asked for rather than to a rebuild.
#
# The case list is this one case: the wired line is the bench's sensitivity
# standard - a link that does not vary - and a boot that also ran a wireless arm
# would answer two questions at once.
CONFIG_BACKEND := freertos
CONFIG_CASES := net_mdk_netio
CONFIG_OPT := -O2
CONFIG_PROFILE := base
CONFIG_HEAP_SIZE := 16M
CONFIG_FREERTOS_HEAP_SIZE := 16777216u
