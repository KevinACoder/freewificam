# net80211-frame-mock - the protocol-path suite

The first wireless suite of the container. It drives the imported net80211 stack
through its protocol paths on the host, with a driver substitute and a scripted
access point, and it reports every step as a segment anchor line
(`vwifi-smoke: <ID> ok=<0|1> ...`).

## What it covers

| path | anchor | case |
|---|---|---|
| attach | `S2.LINK_ATTACH` | the interface shell comes up, the BSS node and the channel are in place |
| scan | `S2.SCAN_START` `S2.TX_PROBE_REQ` `S3.RX_PROBE_RESP` `S3.EVENT_SCAN_DONE` | the probe request goes out, the responder answers, the scan completes |
| open-system authentication | `S3.TX_AUTH` `S3.RX_AUTH` | algorithm 0, sequence 1, then sequence 2 with status 0 |
| association | `S3.TX_ASSOC` `S3.RX_ASSOC` `S3.EVENT_ASSOC` `S3.RUN_STATE` | the request carries the SSID and rates elements, the response is accepted, the state lands in RUN |
| node table | `S3.NODE_TABLE` | the joined node carries the AP BSSID and the association id |
| encapsulation | `S3.ENCAP` | frame control, addresses, LLC/SNAP, ethernet type, sequence progression |
| decapsulation | `S3.DECAP` | the air self loop returns the original ethernet frame byte for byte |
| fragments | `S3.DEFRAG` `S3.DEFRAG_NEG` | reassembly, then the two named drop paths |
| management negatives | `S3.NEG_TRUNC` `S3.NEG_IE` | a truncated body and an over-long element are rejected by name |
| AES-CCM | `S3.CCMP` | a pairwise key, a stack-sealed frame, a peer-sealed frame, and a MIC negative |
| timeouts | `S3.WATCHDOG` | the inactivity entry reclaims an expired candidate; the driver tick is driven explicitly |
| EAPOL boundary | `S4.EAPOL_CLASSIFY` | the EAPOL ethertype reaches its own sink, not the data sink |
| memory | `M.PAIR_SKB` `M.PAIR_MBUF` `M.NAMED_ZERO` `M.NAMED_MISUSE` | the sk_buff seam pairs out, the port allocation seam returns to its baseline, and the seam names its misuse classes |

The frames of the responder are compared byte for byte against hand-derived
vectors (`vectors.h`), and the library's own two host tests (mbuf semantics and
the RFC 3610 vectors) are re-anchored onto this port under `upstream/`.

## Layout

| path | what it is |
|---|---|
| `main.c` | the cases, the anchors, the fault group |
| `vwifi.c`, `vwifi.h` | the driver substitute: transmit capture, receive injection, the scripted responder |
| `vectors.h` | the hand-derived vector bytes and their provenance |
| `skbuff_seam.c`, `skbuff_seam.h` | the host backend of the repository's sk_buff contract header |
| `upstream/` | the library's own two host tests, byte-identical to the pin, plus the re-anchor notes |

## Two facts about this port that the suite compensates for

* The port has no timer service yet, so nothing advances `if_timer` and the
  inactivity timeouts have no driver: the suite drives the timeout entries and
  the driver tick itself. The timer entry points the driver should call are the
  same ones, so the assertions keep their meaning when a callout service lands
  (the bridge round).
* The receive seam hands out one contiguous buffer; the port linearises a
  reassembled frame before dispatching it, which is what makes the fragment case
  deliverable (and what its first red run found).

## The fault group

`check.sh` also runs the binary with `--fault trunc-auth`: the authentication
response loses its last body byte, the stack rejects it by name, and the failing
anchor set must equal the set registered in the wrapper - the pollution may only
redden the segment it entered, and the process must exit with a code rather than
crash.
