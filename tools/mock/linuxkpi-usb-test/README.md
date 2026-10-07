# linuxkpi-usb-test - the USB device model and the linuxkpi USB request face

The suite drives a scripted USB device model (enumeration, descriptors, class requests,
the mass-storage command/data/status flow) through the repository's linuxkpi USB API, and
reports every step as a segment anchor line (`usb-smoke: <ID> ...`). It is the first
consumer of the shared vector factory: the descriptor bytes, the setup lines and the
command block it asserts against are the `.dat` files under `tools/mock/vectors/`, which
`tools/mock/check-vectors.sh` validates at container build time.

## What it covers

| path | anchor | case |
|---|---|---|
| host controller | - | the model registers, the port connect event is injected |
| enumeration | `S1.EP0_DESC_DEV` `S1.EP0_ENUM_SETUP` | the eight-byte descriptor prefix, the addresses, the device descriptor identifiers, the configuration tree, the set-configuration |
| class claim | - | the driver claims interface 0 and its probe completes |
| class requests | `S1.CTRL_MS_RESET` `S1.CTRL_GET_MAX_LUN` | the reset request, and the legal stall of a single-LUN device |
| command flow | `S1.BOT_CBW` | TEST UNIT READY, inquiry, capacity, the tag pairing across commands |
| urb seam | - | submit/complete pairing and coherence |
| negatives | - | the named-failure counters stay zero, the queue drains in order, a killed transfer reports `-ENOENT` |
| vectors | the five vector ids above | each loaded vector is asserted byte for byte |

The anchor list is frozen in `check.sh`, order included: the enumeration chain precedes
the class claim, the probe follows it, and the smoke verdict is the last `usb-smoke:`
line. Every anchor becomes one `CASE linuxkpi-usb-test/<name>` line in the container's
aggregated log.

## The three run modes

| Invocation | What it is for |
|---|---|
| `linuxkpi-usb-test --vec-root tools/mock/vectors` | the green run: the anchors above, plus one `usb-smoke: vec=<vec-id> ok=1` line per loaded vector |
| `linuxkpi-usb-test --fault vec-tamper --vec-root tools/mock/vectors` | the must-red control: a loaded vector is perturbed, the run fails and names the vector and the byte offset (`usb-smoke: vec-mismatch vec=<id> offset=<n>`) |
| `linuxkpi-usb-test --fault negatives` | the scripted negatives: the red controls inside the run report green (`ep0-battery neg=7 ok=1`, the seam xfail lines) |

The green run is given the vector root explicitly because the tamper fault perturbs a
loaded vector: without the root there is nothing to perturb, so the fault does not engage.
The wrapper records that as its own case (`battery-tamper`: a deliberately lying device
must make the battery go red), so the negative controls are proven able to fail rather
than merely able to print `ok=1`.

The suite wrapper exits 2 with `MOCK-BUILD linuxkpi-usb-test: ...` when the binary is
missing, so a suite that was never built is a configuration error, never a silent pass.

## Truth source

USB 2.0 chapter 9 (descriptors, standard requests) and chapter 8 (data toggle semantics),
USB MSC bulk-only transport rev 1.0 chapters 5 and 6 (command block wrapper, command
status wrapper), and the in-repo linuxkpi USB API face under `components/linuxkpi/usb`. The
vector files carry their own per-file truth tags and full notes; the registry's
`input_vec`/`expected_vec` columns point at them.

## Layout

| path | what it is |
|---|---|
| `tools/mock/linuxkpi-usb-test/suite.mk` | the container declaration |
| `tools/mock/linuxkpi-usb-test/check.sh` | the wrapper: anchors to CASE lines, the three run modes |
| `tests/host/linuxkpi-usb-test/` | the suite itself (device model, seam, cases), built by its own Makefile |
| `tools/mock/vectors/S1/*.dat` | the vectors it loads, with `vectors/manifest.tsv` as their index |
