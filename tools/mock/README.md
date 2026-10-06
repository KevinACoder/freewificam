# tools/mock - the Gate1 container

Gate1 is the host side of the project's two gates: everything that can be verified without
a board is verified here, in seconds to a couple of minutes, and every suite is able to go
red on purpose-built input. `make mock` runs the lot.

## Running

```
make -C freewebcam mock              # every suite in suites.mk
tools/mock/run.sh --list             # what is registered
tools/mock/run.sh threadx-regression # one suite
```

Exit codes: `0` every suite behaved as declared, `1` a case or an expectation failed,
`2` configuration, build or environment error. A suite that cannot be built is not a test
failure, and it is never a silent pass either.

Per-suite raw output is kept under `tools/mock/build/<suite>/` (a build directory, not
tracked); the container echoes one `CASE <suite>/<name> PASS|FAIL|XFAIL|XPASS` line per
case, so an aggregated log carries exactly one line per case. `SKIP` is never an
acceptable verdict.

## Declaring a suite

A suite lives in `tools/mock/<name>/` and declares itself in `suite.mk`:

| Field | Meaning |
|---|---|
| `SUITE_NAME` | must equal the directory name |
| `SUITE_TIMEOUT` | wall-clock limit for the run step, in seconds; a timeout is a failure |
| `SUITE_EXPECT` | how the run is judged (below) |
| `SUITE_BUILD` | optional command that builds the suite before it runs |
| `SUITE_RUN` | the command the container runs, from the repository root |
| `SUITE_TRUTH_SOURCE` | where the expected values come from; a suite without one does not run |
| `SUITE_SEGMENTS` | the segment anchors this suite covers, or `-` |
| `SUITE_WIRELESS` | `1` for wireless suites, which must also declare `SUITE_ROUTE` |

Expectations, and what the container reports for each:

| `SUITE_EXPECT` | the run must | if it does | if it does not |
|---|---|---|---|
| `exit:0` | succeed | `PASS` | `FAIL` |
| `exit:<n>` (+ optional `match:<regex>`) | fail with `<n>`, output matching | `XFAIL` | `FAIL`, or `XPASS` when it succeeded |
| `signal:<n>` | die from that signal | `XFAIL` | `FAIL`, or `XPASS` when it succeeded |
| `compilefail` | fail to compile | `XFAIL` | `XPASS` |
| `self` | report its own `CASE` lines | the lines are passed through | `FAIL` |

An expected red that turns green (`XPASS`) is a failure: it means the scenario the case
was written for no longer reproduces. The container also refuses a suite whose own report
disagrees with its exit code, and refuses `CASE` lines that name another suite.

## The registry and the container's own proof

`suites.mk` lists the suites. `container-selftest` is the meta proof that this container
can go red: half of it exercises the classification rules on fixtures that hit each form
exactly, the other half drives the whole container over deliberately broken suites and
asserts that each break is reported the way it must be. A container that cannot fail its
own negative controls would be worth nothing, so that half is not optional.

## Segment registry (the two-sided contract)

`segment-map.tsv` is the authoritative registry of the data-flow segments and their
anchors: each anchor carries its input shape, its expected output, its truth source, and
one print template per side (host assertion, board print) with the same identifier and the
same field names. `gen-segment-map.sh` turns the registry into `build/segment_map.h`; the
header is a build artifact, never committed, and regenerating it must be byte-identical -
the container checks that on every run. `check-segment-map.sh` enforces the schema.

No anchor is active yet: the suites that will print them (registers, USB device model,
memory contracts) land in later rounds, so their rows are registered, and segment S5 is
marked `not-built` because it has no consumer - an empty set is stated, never silent.

## Design notes

* The container is an adapter over the suites: it owns the language (`CASE` lines,
  expectations, exit codes) and every suite keeps its own mechanism. Reference shape:
  `net80211:tests/run-host-tests.sh`.
* The suite set is a registry read by the runner (and includable by a Makefile).
  Reference shape: a driver registry as in `linux:drivers/base/dd.c`.
* One mechanism per job: the runner does not know how to test anything itself, and no
  suite is allowed a private reporting format or a private retry policy.
