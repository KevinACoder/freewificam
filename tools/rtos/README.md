# tools/rtos - build and board-round entries for the RTOS bin platform

Three entries, one per job:

```sh
tools/rtos/build.sh                # the configured image, its gates, the digests
tools/rtos/build.sh images         # both kernels of the configured profile
tools/rtos/build.sh host-test      # the conformance case on the host tier (both kernels)
tools/rtos/build.sh digest         # component patch records (third-party + wireless-drivers + net80211)

# one board boot, full evidence, judged at the end
BOARD_SESSION=<client> BOARD_PYTHON=<interpreter> \
  tools/rtos/board-round.py --backend freertos \
    --out <run-dir> --boot boot1 --cases tick,cmsis2,net,emmc

# with a peer host driving the network arms
BOARD_SESSION=<client> BOARD_PYTHON=<interpreter> BOARD_PEER=<ssh-target> \
  tools/rtos/board-round.py --backend threadx --peer --board-ip <board-address> \
    --out <run-dir> --boot b1

# re-judge a capture (also the host tier's stdout)
tools/rtos/judge.py <run-dir>/boot1/serial.log --json /tmp/judge.json
```

## What the pieces are

| entry | what it owns |
|---|---|
| `build.sh` | the environment, the elapsed-time line (the dev-loop budget is one profile under a minute) and the fingerprint printout (size, sha256, the banner's build tag) around `make -C tests/board/rtos/build` |
| `fw-blob.py` | generates/verifies a compiled-in firmware blob (`--in`, `--name`, `--symbol`, `--out`; `--check` re-extracts and compares). The blobs themselves are gitignored; `docs/FIRMWARE-NOTICES.md` carries the digests, the source file and the redistribution terms |
| `board-round.py` | one boot: stage under the deploy name the board's `rtos` profile expects, boot with a power cycle, capture the serial log, drive the peer when the image says its network case is ready, power off, judge |
| `judge.py` | the anchor language of the image, per-case verdicts, and the evidence checks that must accompany a `PASS` |

`board-round.py` keeps one session open for the whole round: the one-shot
client gives a fresh serial cursor per call, and this tool needs one continuous
cursor to write an unbroken `serial.log`.

The session client and its interpreter are **environment**, not repository:
`BOARD_SESSION` names the line-protocol client, `BOARD_PYTHON` the interpreter
that runs it, `BOARD_PEER` an ssh target for the peer host and `BOARD_IP` the
address the peer drives.  This repository carries no machine layout, so none of
them has a default; `--peer-host` and `--board-ip` override the last two per run.

## Evidence, per boot

```
<out>/<boot>/command.txt      the exact invocation
             fingerprint.txt  size + sha256 + crc32 (host, staged) + boot_id
             serial.log       the full capture, nothing elided
             boot.json        the board session's boot answer (transcript, timings)
             service-status.json, staged.json
             peer-*.log       the peer side, when it ran
             judge.json       the verdict
             RESULT.md        the same verdict, readable
```

A boot is only judged from `serial.log`; `judge.json` records which anchors
were found and which were missing, so a `FAIL` can be read without the board
being online.

## Judge rules

- A case counts only when its own verdict line says `PASS` **and** its
  evidence anchors are present and consistent: the tick ladder's error inside
  its tolerance, the conformance case's failures at zero, the ping replies
  equal to the requests, the storage round trip's checksums equal and the
  error-path line saying it returned, a non-zero byte count in both network
  directions, and (camera image) the negotiated payload bound, a completed
  isochronous transport and a frame count above the release floor.
- A missing anchor is a failure, not a skip: the image's anchor language is
  the contract, and a `PASS` whose evidence is absent is a broken image.
- The `--peer` flag only changes who drives the peer host; a boot without it
  still judges everything except the two TCP directions, which the peer side
  causes.
