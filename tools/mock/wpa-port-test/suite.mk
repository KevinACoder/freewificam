# Suite declaration: the wpa_supplicant port over vectors and a scripted peer.
#
# The suite binary is built by its own Makefile under tests/host/ - the
# container must not grow a second copy of it. It links the vendored
# supplicant (the PSK-only compile set), the first-party port layer under
# ports/wpa_supplicant/ and a scripted vendor backend, once per CMSIS-RTOS2
# backend, and reports every case as an anchor line
# (`WPAPORT: <id> PASS|FAIL ...`).

SUITE_NAME := wpa-port-test
SUITE_TIMEOUT := 300
SUITE_EXPECT := self
SUITE_BUILD := make -j4 -C tests/host/wpa-port-test
SUITE_RUN := sh tools/mock/wpa-port-test/check.sh
SUITE_TRUTH_SOURCE := hand-derived vector tables: RFC 2202 test case 1 (HMAC-SHA1), the published IEEE 802.11i PBKDF2 sample pair ("password"/"IEEE"), a PTK derived with the 802.11i 8.5.1.1 PRF-512 recomputed by an independent HMAC-SHA1 oracle, and the EAPOL-Key body layout of IEEE 802.11-2020 Table 12-6 (field order and offsets); the scripted peer follows the standard procedure (key derivation, MIC over the zeroed frame, AES Key Wrap of the GTK KDE), not the implementation
SUITE_SEGMENTS := S3.WPA_4WAY,S4.EAPOL_RX
SUITE_WIRELESS := 1
SUITE_ROUTE := ieee80211-route: IEEE 802.11-2020 clause 12.7 (EAPOL-Key frames; descriptor layout per Table 12-6) and 9.4.2.25 (RSN element), IEEE 802.11i 8.5.1.1/8.5.1.2 (PRF and PTK hierarchy), RFC 2202 (HMAC-SHA1) and RFC 3394 (AES Key Wrap)
