# tools/netio - the pinned NETIO peer

`third-party/netio` is the unmodified NETIO 1.33 tree (provenance and pin in
`third-party/netio/netio.provenance.md`).  It is the client half of the
throughput ruler: built for the Linux host and run against the board's netio
server.  It must stay byte-identical to the pin - the peer is the one component
shared by every arm (linuxnative / linuxko / lwIP), so it must not drift.

The build entry compiles the tree out of tree, so `third-party/netio` stays
pristine:

```sh
tools/netio/build.sh                                  # -> build/netio/netio + sha256
tools/netio/build.sh --stage pi@192.168.0.17:/tmp/netio
```

Run it against the board:

```sh
./netio -u -b 1440 <board-ip>      # UDP, 1440 B payload
./netio -u -b 64   <board-ip>      # UDP, 64 B payload
./netio -t -b 1440 <board-ip>      # TCP
```

The board-side server is `tests/board/rtos/app/cases/net_mdk_netio.c`; the wire
protocol (TCP 18767 control, UDP 18768 data) is recorded in
`third-party/netio/netio.provenance.md`.

The 64 B pps arm needs a sender that can hold a requested rate for 10 s (two
busy threads, 256-frame batches, no sleeping pacer).  That sender is not part of
this repository yet; `docs/NET-TESTING.md` describes the required properties and
the validity gates.
