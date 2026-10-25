# @file    config.mk
# @brief   The one RTOS build configuration. Edit this file, not the command line.
#
# A build is a function of this file alone. The values below select one backend
# and one image; `make` and `tools/rtos/build.sh` with no target build exactly
# that image. The delivery gate (`make images`) is the only target that builds
# both backends.
#
# Keep this file stable during a debug round: every changed value enters the
# flag stamp and intentionally rebuilds the objects it affects. With it
# unchanged, a source edit is an incremental rebuild.

# --- image -----------------------------------------------------------------
CONFIG_BACKEND := freertos
CONFIG_CASES := wlan
CONFIG_OPT := -O2
CONFIG_PROFILE := perf
CONFIG_HEAP_SIZE := 16M

# --- WLAN observation / stability arm --------------------------------------
# 0 = delivery-like image; 1 = census + 1 Hz layer snapshots.
CONFIG_WLAN_OBS := 1
CONFIG_WLAN_TCP_SINK := 1
CONFIG_WLAN_TCP_SINK_SECONDS := 30
CONFIG_WLAN_UDP_SINK := 0
CONFIG_WLAN_UDP_SINK_SECONDS := 30
CONFIG_WLAN_UDP_PORT := 5002
CONFIG_WLAN_PING_COUNT := 10
CONFIG_WLAN_PING_INTERVAL_MS := 500
CONFIG_WLAN_PING_TRACE := 1
CONFIG_WLAN_SCAN_DIAG := 0
CONFIG_WLAN_SCAN_BATCH := 1
CONFIG_WLAN_PUMP_PRIO := below
CONFIG_WLAN_PUMP_DRAIN := 0
