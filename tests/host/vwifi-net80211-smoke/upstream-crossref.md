# Upstream cross reference (the ③ segment's anchors)

This file is the deliverable of two disciplines: the upstream test corpus of the
Linux tree is read as an *intent and semantics* reference only (never copied,
never compiled), and the capabilities of the upstream virtual radio are mapped
against what this suite chooses to do. The truth of every expectation stays with
the 802.11 frame formats and the suite's own vector tables.

Upstream tree: `linux-upstream` at `7.3.0-rc2` (read-only, on this machine). All
files cited are GPL-2.0 (`-only` where marked); **none of their content is in
this repository and none of it is compiled into this suite** - the check is at
the bottom of this file.

## The comparison table (our case -> upstream case -> clause route)

| our case (segment ③) | upstream counterpart (7.3.0-rc2) | how we use it | clause route |
|---|---|---|---|
| probe request / probe response element encoding (SSID 0, rates 1, DS 3) | `net/wireless/tests/scan.c:227` `test_gen_new_ie` | grouping and naming of the element cases; the bytes come from the format, not from this file | 802.11-2020 frame formats and element ids |
| truncated body and over-long element (`S3.NEG_TRUNC`, `S3.NEG_IE`) | `scan.c:287` `test_gen_new_ie_malformed` | the "malformed input -> named rejection" pattern | frame body length consistency |
| scan result handling and the miss path (`S3.RX_PROBE_RESP`, `S3.EVENT_SCAN_DONE`) | `scan.c:335` `test_inform_bss_ssid_only`, `scan.c:405` `test_get_bss_miss_reason` | the shape of scan-result cases (we walk the single-AP hit path; the miss path is registered as a follow-up) | scan to join state machine |
| open-system authentication (`S3.TX_AUTH`, `S3.RX_AUTH`) | `net/mac80211/tests/mfp.c:212` `accept_mfp` | the accept/reject *intent* only (the upstream object is protected-frame handling, ours is the open exchange) | open-system authentication sequence |
| association request/response and element parsing (`S3.TX_ASSOC`, `S3.RX_ASSOC`) | `net/mac80211/tests/elems.c:243` `mle_defrag`, `elems.c:325` `mesh_preq_parse` | the structure of element-parsing cases (different element families, same intent) | association response content |
| channel definition consistency (single channel 1) | `net/wireless/tests/chan.c:198` `test_chandef_compat`, `net/mac80211/tests/chan-mode.c:148` `test_determine_chan_mode` | reference for channel semantics; **no counterpart case** - this suite has one fixed channel and asserts no channel logic | channel element (DS parameter set) |
| fragment reassembly and its drop paths (`S3.DEFRAG`, `S3.DEFRAG_NEG`) | `net/wireless/tests/fragmentation.c:11/41/84/133` `defragment_0/1/2/at_end` | intent: reassembly cases; **the upstream one is the receiving-stack defragmenter**, ours walks the component's own path | fragment and duplicate rules (second source: the component's defragmentation) |
| AES-CCM sealing and the MIC negative (`S3.CCMP`) | no counterpart in these trees (the KUnit corpora carry no cipher vectors) | the vector and the rejection come from the library's own test (RFC 3610) and the CCMP clause | RFC 3610; CCMP |
| EAPOL boundary (`S4.EAPOL_CLASSIFY`) | no counterpart (the upstream boundary is in mac80211's rx path, not a test) | registered as "no counterpart" | EAPOL ethertype |
| S1G / TTLM / TPE / MLO / AP co-location cases (`s1g_tim.c`, `ttlm.c`, `tpe.c`, `scan.c:556`, `scan.c:896`) | `net/mac80211/tests/*` | **no counterpart in this suite** - the single-AP, single-link world of this mock does not carry these features | n/a |

If a dual-source disagreement ever appears (the clause and the upstream case
disagreeing), the clause wins and the disagreement is recorded as an issue; none
has appeared so far.

## The upstream virtual radio, mapped (behavior reference, not truth)

`drivers/net/wireless/virtual/mac80211_hwsim_main.c` (7684 lines,
`GPL-2.0-only`) and its companions are read as the "reference original" of a
virtual radio. The table records what this suite does with each behaviour; every
row carries its upstream anchor.

| # | upstream behaviour | this suite | why |
|---|---|---|---|
| 1 | multi-radio relay: "perfect medium simulation" when wmediumd is absent (`mac80211_hwsim_tx_frame_no_nl:1902`, delivery `mac80211_hwsim_rx:2028`) | not done | one AP / one station scripted world; a second radio has no consumer here |
| 2 | scan state (`data->scanning`, `scan_addr`) (`:1470`) | done (equivalent) | the stack owns its scan state; the driver substitute only advances the dwell (`ieee80211_next_scan`) and the suite observes `S2.SCAN_START` / `S3.EVENT_SCAN_DONE` |
| 3 | association gate (`vp->assoc` gating PS-Poll / NullFunc, `:986`, `:1018`) | done (equivalent) | the equivalent is the stack's node state plus the port's association event (`S3.EVENT_ASSOC`); no driver-private flag is invented |
| 4 | ACK frame synthesis (`mac80211_hwsim_monitor_ack:1390`) | not done | this mock has no ACK / retransmission / contention semantics; the scripted responder answers immediately |
| 5 | genetlink control plane (family `:718`, `HWSIM_CMD_FRAME:1682`, radio add/remove `:4502`/`:6222`, wmediumd `:6344`) | not done | no runtime control protocol: the suite's control plane is compile-time constants plus the fault invocation group |
| 6 | packet injection (upstream `/* TODO: allow packet injection */` `:1260`) | done (beyond the upstream TODO) | receive injection (`ieee80211_input`) is this suite's main path |
| 7 | optional packet loss simulation (upstream `/* TODO: simulate optional packet loss */` `:1943`) | not done | determinism first; only named injections (truncation, element length, MIC) exist |
| 8 | TSF sync, IBSS contention, rx filter, virtio queues, NAN, PMSR/MLO (`:11-13`, `:13-14`, `:902`, `mac80211_hwsim_nan.c`, `:3788`) | not done | outside this mock's single-AP world |
| 9 | methodology: the hwsim ecosystem runs one set of join/data cases against a virtual radio (hostapd/wpa_supplicant suites) | borrowed (shape) | borrowed as this file's organisation: our case -> upstream case -> clause route |

Control plane, stated once: the script's constants are compile time, the fault
variants are a separate invocation, and no runtime control protocol is built.

### Design alternative registered, not adopted

`drivers/net/wireless/virtual/virt_wifi.c` (716 lines, `GPL-2.0`) attaches
"wireless" properties to an existing ethernet device. Not adopted: the surface
under test here is the net80211 stack and the port seams, not netdev
properties; adopting it would bypass the object under test and drag in
cfg80211/mac80211 semantics.
`source=linux-upstream:drivers/net/wireless/virtual/virt_wifi.c@7.3.0-rc2`.

Provenance of the borrowed shapes:
`source=linux-upstream:net/wireless/tests/{scan,chan,fragmentation}.c@7.3.0-rc2`,
`source=linux-upstream:net/mac80211/tests/{elems,mfp,chan-mode}.c@7.3.0-rc2`,
`source=linux-upstream:drivers/net/wireless/virtual/mac80211_hwsim_main.c@7.3.0-rc2`.

## Zero copy, zero compilation

None of these files is in this repository: `git grep` over the tree finds no
content of the upstream test corpora or the virtual radio, and no build source
list names any of their paths. That check is run in the round's evidence log
(`A8-zero-copy.log`) and repeated by the wrapper's reviewers when the file list
changes.
