#!/bin/sh
# Fail if the one-module build leaked a mac80211 symbol to the kernel.
#
# wlan_ko.ko must define every ieee80211_* the verbatim driver calls, except
# the helpers cfg80211 itself exports (channel<->freq, hdrlen, ie_split,
# data_to_8023, vht nss).  Those are builtin on this image and are not the
# mac80211 stack; allowing them keeps the gate honest without reimplementing
# cfg80211.  Everything else must be local.
set -eu
ko=${1:?usage: check-closure.sh <wlan_ko.ko>}
cfg80211_provided="
ieee80211_channel_to_freq_khz
ieee80211_freq_khz_to_channel
ieee80211_get_channel_khz
ieee80211_hdrlen
ieee80211_ie_split_ric
ieee80211_data_to_8023_exthdr
ieee80211_get_vht_max_nss
"
bad=""
allowed=""
for sym in $(nm -u "$ko" 2>/dev/null | awk '/ieee80211_/ {print $2}'); do
	case "$cfg80211_provided" in
	*"$sym"*) allowed="$allowed $sym" ;;
	*) bad="$bad $sym" ;;
	esac
done
if [ -n "$bad" ]; then
	echo "closure FAIL: undefined mac80211 symbols:$bad" >&2
	exit 1
fi
echo "closure OK: no undefined mac80211 symbols"
[ -n "$allowed" ] && echo "cfg80211-provided (allowed):$allowed"
echo "cfg80211 imports (expected from builtin cfg80211):"
nm -u "$ko" 2>/dev/null | grep -E '\bcfg80211_' || true
