# Upstream USB test battery cross-reference

The upstream Linux USB test sources (`drivers/usb/misc/usbtest.c`,
`tools/usb/testusb.c`, `drivers/usb/gadget/udc/dummy_hcd.c`, `tools/usb/hcd-tests.sh`,
GPL-2.0 family) are read for **case intent**, **negative-expectation shape** and
**behaviour form only**.  No upstream code, constant table or comment is copied into
this repository, and nothing of theirs is compiled here (zero-copy / zero-compile).
The truth of every expectation in this suite comes from the independent sources named
in each row below (the USB specification, the error-code documentation, and the bulk-only
transport specification); where an expectation was *shaped* after an upstream case, the
`source=` field of the vector or the note here says so, and the row names it.

Upstream anchors are line numbers of the 7.3.0-rc2 tree; they may drift, in which case
the file plus the symbol/case name stays the anchor.

| Family | Upstream anchor | This suite's coverage | Expectation shape | Round |
|---|---|---|---|---|
| A descriptor/status battery | `usbtest.c:826` `ch9_postconfig` (TEST 9 dispatch `:2354`), battery cases 0/1/3/4/6/9 | enumeration walker: DEVICE prefix and full read, CONFIG head and full tree, SET_ADDRESS, SET_CONFIGURATION, GET_STATUS (endpoint) | service bytes equal the specification-derived vectors; setup line fields checked field by field | R1 |
| B control-face negatives | `usbtest.c` battery cases 2 (`:1257`), 5 (`:1275`), 7 (`:1286`), 8 (`:1294`), 10 (`:1309`), 11 (`:1315`), 12 (`:1322`), 13 (`:1329`), 14 (`:1338`), 15 (`:1354`) | the `--fault negatives` run: GET_INTERFACE stall, interface-descriptor stall, endpoint-descriptor stall, device short read, config short read (two request sizes), qualifier answer-or-stall | must-red cases expect their named error code (`-EPIPE`, `-EREMOTEIO`); the qualifier case declares a set because the protocol allows both | R1 (7 cases) |
| C queued control | `usbtest.c:2367` TEST 10, implementation `:1201`, header notes `:1058-1064` | asynchronous bulk queueing: two urbs to one endpoint drain in order, one completion each | ordering plus pairing equalities | R1 (bulk shape; the control-urb queue shape lands with the engine's async face) |
| D unlink family | `usbtest.c:2378` TEST 11, `:2391` TEST 12, `:2550` TEST 24 | kill of a transfer the controller still owns delivers exactly one completion with the cancellation code | named completion code | R1 |
| E halt/toggle family | `usbtest.c:2406` TEST 13 (`verify_halted` `:1651`), `:2622` TEST 29 (`test_toggle_sync` `:1722-1750`) | endpoint halt state observable via GET_STATUS, stall reported as `-EPIPE`, clear-halt recovers the endpoint | status byte plus error code | R2 |
| F bulk stream (sink/source) | `usbtest.c:2216-2335` TEST 1-8 (`simple_io`, `perform_sglist`) | bulk-only transport data phases (inquiry, capacity reads; write phases in R2) | length and content patterns, actual length | R2 |
| G isochronous family | `usbtest.c:2432-2538` TEST 15/16/22/23 | not covered: no isochronous endpoint in the device model by design (the video milestone owns that line) | - | registered, not taken |
| H DMA alignment family | `usbtest.c:2460-2500` TEST 17-20 | not applicable: host transfers have no DMA mapping semantics; the allocation seam still exposes the coherent pair | - | explicit n/a |
| I vendor control writes | `usbtest.c:2419` TEST 14, `:2514` TEST 21 (`ctrl_out` `:1820`) | not applicable: the toy device has no vendor face; control OUT data stages are covered by the class requests | - | explicit n/a |
| J large bulk runs | `usbtest.c:2603` TEST 27, `:2612` TEST 28 | soak type family (bulk_in/bulk_out with size variation) | pairing and named-counter checkpoints | R2 |
| K interrupt endpoint family | `usbtest.c:2570` TEST 25, `:2586` TEST 26 | not covered: no interrupt endpoint in the device model (no consumer yet) | - | registered, not taken |
| L upstream gap | `usbtest.c:1456` (comment: wants more endpoint-STALL tests) | this suite's stall coverage (family B plus the halt family) is that face, expressed against the device model's scripted answers | - | registered |

Notes:

* The negative-expectation split (must-red versus lawful-set) follows the upstream
  convention that a signed expectation means "the other side must return this code"
  while an unsigned one means "this code is allowed" (`usbtest.c:1234-1245`); the
  suite implements it with the container's case verdicts and the fault run's battery.
* `dummy_hcd` is the behaviour form behind the synthetic controller: one transfer
  function moving bytes between the two sides of a synthetic link, link state advanced
  by injected edges, protocol stall signalled per request.  It is imitated, not ported.
* `hcd-tests.sh` is the methodology behind the soak round (named type sequence,
  parameterised run); its `loop` type is deliberately not taken, as upstream records
  it as unwritten.
