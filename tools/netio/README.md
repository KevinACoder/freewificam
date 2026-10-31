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

## ppsgen - the 64 B pps sender

`tools/netio/ppsgen.c` is the sender (and counting receiver) for the pps test.
It paces against an absolute schedule on two busy threads, carries a sequence
number, and can wait for a TCP `GO` so the board's counter window and the sender
start together.

```sh
build/netio/ppsgen -d <board-ip> -p 18768 -r 260000 -t 10 -T 2 -k 0 -B 256
```

The board side is the `net_pps` case (`configs/net-pps.mk`), driven over the
serial console:

```
freq 1992000      # request; record the measured actual MHz
riwt 160          # runtime RX coalescing, after the frequency change
go cell
# host: build/netio/ppsgen -d <board-ip> -p 18768 -r 260000 -t 10 -T 2 -k 0 -B 256
stop cell
```

## pps-report.py - one result line per cell

`tools/netio/pps-report.py` joins the console log and the sender log, computes
offered/delivered pps, `c0`, `C0`, ring-full and FIFO shares, and applies the
validity gates (sender within 2%, sender count vs offered within 1%, window
10.00 +- 0.05 s, closure within 0.1%):

```sh
tools/netio/pps-report.py --serial console.log --sender sender.log --tag cell
```

`T0` is the highest offered rate with zero loss, repeated 2/2; see
`docs/NET-TESTING.md` for the full procedure.
