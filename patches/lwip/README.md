# patches/lwip - index

Local patches replayed onto the vendored lwIP submodule (`third-party/lwip/`).
The vendored tree itself is never edited: every difference lives here and is
applied at build time by `tools/patches/replay.sh`.

| Item | Value |
|---|---|
| Upstream | https://github.com/lwip-tcpip/lwip.git |
| Base (= pin) | `77dcd25a72509eb83f72b033d219b1d40cd8eb95` (tag `STABLE-2_2_1_RELEASE`) |
| Patch list | `0001-lwiperf-boundary.patch`, `0002-tcp-probe-hook.patch`, `0003-tcp-ack-delay-watermark.patch`, `0004-tcp-unsent-tail-cache.patch` |
| Submission status | `0001` not yet submitted: it is written to be sent as-is (one concern, `git format-patch` form, `Signed-off-by`, no local knowledge) and is waiting on the board acceptance run that measures the modes it unblocks. `0002` not submitted: it is a neutral extension point (upstream-able as-is) that the local build points at its own timing hook; no upstream consumer yet, so it stays local until someone needs the same taps. `0003` not submitted: a build-time option (default keeps upstream behaviour), upstream-able as-is if the maintainers want the watermark tunable. `0004` not submitted: implements the source's own @todo (cache the unsent tail), default behaviour is unchanged for small queues; upstream-able once board-proven |

## 0001 - lwiperf: take plain payload at the 128 KiB boundary as data

`lwiperf_tcp_recv()` requires the packet crossing the 128 KiB boundary to start
with a repeat of the 24-byte settings block whenever `ANSWER_TEST` is set, and
closes the session with `LWIPERF_TCP_ABORTED_LOCAL_DATAERROR` when it does not.
That repeat is lwiperf's own client's behaviour; the iperf2 client never
repeats it, so the modes that set `ANSWER_TEST` - `iperf -d` (dualtest) and
`iperf -r` (tradeoff) - die on the receive leg on the second packet. The same
branch also strips 24 bytes of payload when the flag is *not* set, i.e. a long
single-direction transfer loses 24 bytes per crossing and reports 24 bytes more
than it received.

The patch consumes the repeated block only when the packet really starts with
one and lets everything else fall through to the data path. It does not change
what lwiperf's own client sends, and the receive behaviour for that client is
unchanged.

Acceptance: a board run with `iperf -c <board> -d` against the image's lwiperf
server must show both legs carrying bytes (the receive leg used to abort).

## 0002 - tcp: add an optional probe hook on the input-to-output handoff

Time inside `tcp_input` is opaque: segment processing, the application
`sent`/`recv` callbacks it runs, and the deferred `tcp_output` that sends
queued data all happen inside one call. The patch adds
`LWIP_HOOK_TCP_PROBE(site)` (default: does nothing) at eight fixed places -
`tcp_input` entry, around `TCP_EVENT_SENT`, before the deferred `tcp_output`,
and `tcp_output` entry / after the unacked walk / per data segment sent /
per control segment sent - so a build option can timestamp the pieces. The
hook must not change state; with no hook defined the calls compile to
nothing. The local build points it at `ports/lwip/ethnetif.c`, whose
`gmac tcpfine` console command buckets tick deltas per site pair.

## 0003 - tcp: make the delayed-ACK watermark a build-time option

The delayed-ACK mechanism acknowledges every second in-order data segment via
a single `TF_ACK_DELAY` bit, and is not tunable. The patch adds
`LWIP_TCP_ACK_DELAY_SEGMENTS` (default 2, upstream behaviour unchanged):
1 acknowledges every segment immediately, larger values hold the ACK across
that many segments. Non-default builds count in-order segments in a new pcb
field; the count restarts whenever `TF_ACK_DELAY` is set anew, so paths that
clear the flag elsewhere (data segment carrying the ACK, `tcp_fasttmr` flush)
need no extra bookkeeping. Local use: the downlink's per-frame cost is about
half ACK send, so a measurement build can halve the ACK count.

## 0004 - tcp: cache the unsent queue's tail in the pcb

`tcp_write()` appends to the unsent queue by walking head-to-tail on every
call; the source itself carries an `@todo` for the fix. On a 1 Gbit/s line the
walk's cost grows with `TCP_SND_BUF` and becomes the dominant term of the
per-ACK write path (measured on our board: 14.6 us per callback at 512 KB
against 9.7 us at 128 KB, with throughput up 90% when the buffer was cut).
The patch keeps the tail in
`pcb->last_unsent`, maintained at every mutator of the queue (append,
dequeue, mid-queue insert, RTO concatenation, prefix/total free), so the
append no longer depends on queue length and the buffer can be sized purely
for bandwidth-delay product.

Acceptance: board run with the patch at a 128 KB+ send buffer must match the
walk-free (small-buffer) throughput without regressions; Gate1 green.

Rules (project patch policy):

* One concern per patch, `git format-patch` style, English, `Signed-off-by`, no
  local knowledge; written so it could be sent upstream as-is.
* New capability is added as new files rather than edits to upstream files
  wherever that is possible.
* When upstream accepts a patch: remove it here, move the pin forward, and note
  both in `third-party/lwip.provenance.md`.
