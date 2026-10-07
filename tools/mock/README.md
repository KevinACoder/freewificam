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

The registry has fourteen columns, in this order:

| # | Column | Meaning and validation |
|---|---|---|
| 1 | `seg` | segment id, one of `S1` `S2` `S3` `S4` `S5` `M` |
| 2 | `anchor_id` | the anchor, `<segment>.<name>` with a single dot; unique per active row, or `-` |
| 3 | `boundary` | what crosses this boundary |
| 4 | `in_shape` | the shape of the input |
| 5 | `out_expected` | the expected output |
| 6 | `truth_source` | where the expectation comes from; never empty |
| 7 | `route` | `n/a`, or an `ieee80211-route:` annotation for a wireless row |
| 8 | `input_vec` | the input the row's consumer loads: `-`, or a path to an existing vector file |
| 9 | `expected_vec` | the expected bytes the row's consumer loads: `-`, or a path to an existing vector file |
| 10 | `host_assert` | the host-side consumer |
| 11 | `host_fmt` | the host print template, carrying the anchor id and the field names |
| 12 | `board_fmt` | the board print template: same id, same field-name sequence as `host_fmt` |
| 13 | `consumer` | the suite or component that consumes the row (`none` when not built) |
| 14 | `status` | `active`, `registered` or `not-built` (segment `S5` must be `not-built`) |

Columns 8 and 9 are what tie an anchor to the shared vector factory: `check-segment-map.sh`
validates that the cell is `-` or a readable vector file, and `check-vectors.sh` carries the
cross-check further, that the file's own `seg`, `anchor`, direction and vector id agree with
the row (an `input_vec` takes `dir=in` or `dir=trace`; an `expected_vec` takes `dir=exp`).

Rows whose consumer has not landed stay `registered`, and segment `S5` is marked
`not-built` because it has no consumer - an empty set is stated, never silent.

## Vector/Trace data files (`.dat`)

Vectors and traces are the factory's one data format: a small ASCII header, a hex payload,
and two checksums. The format is defined here and nowhere else - every specialization
(the net80211 frame mock, the USB device model, later suites) reuses it rather than
inventing a fixture shape of its own.

### Transfer form

Line text with LF endings, payload encoded as lower-case hex. Each payload line carries at
most 32 bytes (64 hex digits). The `payload_bytes` line doubles as the payload start
marker: everything before it is header, everything after it is payload. Blank lines and
lines starting with `#` are skipped anywhere; there are no trailing hash lines (the
digests live in the header).

### Header

`key=value` single lines, in this fixed order, exactly these thirteen keys:

| # | Key | Meaning and validation |
|---|---|---|
| 1 | `mockvec` | fixed magic token `mockvec1` (a format change becomes `mockvec2`) |
| 2 | `ver` | format version, `1` |
| 3 | `seg` | one of `S1` `S2` `S3` `S4` `M`; segment `S5` has no consumer and carries no vector |
| 4 | `dir` | `in` (input), `exp` (expected output) or `trace` (a record) |
| 5 | `seq` | integer 1..999, unique and contiguous per `(anchor, dir)` |
| 6 | `anchor` | an anchor id present in `segment-map.tsv` |
| 7 | `payload_bytes` | the decoded byte count; must equal the decoded payload |
| 8 | `crc32` | CRC-32/ISO-HDLC (polynomial `0xEDB88320`, initial and final xor `0xFFFFFFFF`) of the decoded bytes, 8 lower-case hex digits |
| 9 | `sha256` | SHA-256 of the decoded bytes, 64 lower-case hex digits; the cross-tool check of the CRC |
| 10 | `truth` | the short truth tag for `in`/`exp` (e.g. `usb2-ch9.6.1`); `n/a` for `trace` |
| 11 | `source` | how the bytes were obtained: e.g. `derived:spec` or `recorded:usbmon`; for `trace` one of `host:<suite>/<case>` or `board:<image>/<cmd>` |
| 12 | `captured` | `YYYY-MM-DD`, or empty for a derived vector |
| 13 | `redact` | the redaction rule id(s) applied, or `none`; never empty |

Expected values come from an independent truth source (a specification clause, an upstream
behaviour, a recording) - never from the implementation under test. A dump the test itself
produced is always a `trace`, and a trace never becomes an `exp`.

### Payload

Lower-case hex after the `payload_bytes` line, at most 32 bytes per line. A generator
writes it canonically at 64 hex digits per line, so two runs over the same input are
byte-identical.

### Manifest

`vectors/manifest.tsv` has one row per committed vector, sorted by `file`, with this header
row:

```
file	vec	seg	dir	anchor	truth	route	redact	norm	note
```

| Column | Meaning |
|---|---|
| `file` | repository-relative path, `tools/mock/vectors/<seg>/<vec>.dat` |
| `vec` | the globally unique vector id, `<anchor>.<dir>.<NNN>`, equal to the file stem |
| `seg` `dir` `anchor` | the same values the file's header carries |
| `truth` | the full truth note (document section, table or URL), never empty |
| `route` | `n/a`, or the `ieee80211-route` annotation for a wireless row |
| `redact` | the redaction rule id (`none` when no rule applied) |
| `norm` | `-` for a vector; the deterministic normalisation rule id for a `trace` |
| `note` | where the vector is consumed, or what it is cross-referenced against |

Only `in` and `exp` files get manifest rows: a `trace` is a run product, not test data, and
is exempt (its `norm` rule would be recorded if one were registered). The manifest does not
repeat `payload_bytes`, `crc32` or `sha256` - duplicating them would create a second
source that can drift from the file.

### Tools

| Tool | Role |
|---|---|
| `gen-vector.sh` | the single generator: `--from-spec <descriptor>` writes `<vectors>/<seg>/<vec>.dat` and refreshes its manifest row, `--list` prints the vectors it can generate (the `vectors/collect/*.spec` descriptors) |
| `check-vectors.sh` | the single validator (below): the container runs it at build time, and no suite implements a second copy of these checks |
| `check-segment-map.sh` | the registry schema, including that the two vector columns name existing files |

A descriptor is a `key=value` file with the same shape as a `.dat` minus the computed
fields: the header keys (`seg` `dir` `seq` `anchor` `truth` `source` `captured` `redact`),
the manifest-only cells (`truth_note`, `route`, `norm`), the consumer `note`, and the
payload as `hex=` lines. Descriptors live in `vectors/collect/` as `<vec>.spec`; they are
the reviewable record of where a vector's bytes come from, and `--list` prints the vector
ids the descriptors can generate. `payload_bytes`, `crc32` and `sha256` are computed,
never written by hand; the descriptor stem must equal the vector id it derives. Re-running the generator
over an unchanged descriptor reproduces the file and the manifest row byte for byte, and a
malformed descriptor is refused loudly (a message on stderr naming the key or line, and a
non-zero exit) without writing anything.

### What `check-vectors.sh` validates

One machine-readable line per finding, `<rule> <file> <field>: <detail>`, then a final
`VECTORS-RESULT pass|fail`; the exit status is non-zero when anything failed, and an empty
vector set is a clean, silent pass.

| Rule | Check |
|---|---|
| `magic` | the first line is the `mockvec1` token |
| `header` | the twelve `key=value` lines follow the fixed order, once each |
| `ver` | `ver=1` |
| `seg` | one of `S1` `S2` `S3` `S4` `M`, matching the directory the file lives in |
| `dir` | `in`, `exp` or `trace` |
| `seq` | an integer 1..999, unique and contiguous per `(anchor, dir)` |
| `anchor` | registered in the segment map, in the segment the header claims |
| `vec` | the file stem is the derived id `<anchor>.<dir>.<NNN>` |
| `payload` | lower-case hex, even digit count, at most 32 bytes per line |
| `payload_bytes` | the declared count equals the decoded length |
| `crc32` | CRC-32/ISO-HDLC over the decoded bytes equals the header value |
| `sha256` | SHA-256 over the decoded bytes equals the header value |
| `truth` | non-empty; `n/a` exactly for a `trace`, a real tag for a vector |
| `source` | non-empty; `host:`/`board:` prefixed for a `trace` |
| `redact` | non-empty (`none` when no rule applied) |
| `captured` | a `YYYY-MM-DD` date, or empty |
| `manifest` | the header and ten columns; every row's values match the file it names; unique ids; a row for every committed vector (records are exempt); rows in file-name order |
| `route` | a wireless row carries a real annotation, every other row says `n/a` |
| `segment-map` | a non-empty `input_vec`/`expected_vec` cell names an existing vector file whose segment, anchor, direction and vector id match the row |

The container runs the validator before the first suite: a red there is a configuration
error (`MOCK-CONFIG vectors: <finding>`, exit 2), because a broken factory would make every
vector-driven assertion meaningless.

## Design notes

* The container is an adapter over the suites: it owns the language (`CASE` lines,
  expectations, exit codes) and every suite keeps its own mechanism. Reference shape:
  `net80211:tests/run-host-tests.sh`.
* The suite set is a registry read by the runner (and includable by a Makefile).  Reference shape: a driver registry as in `linux:drivers/base/dd.c`.
* One mechanism per job: the runner does not know how to test anything itself, and no
  suite is allowed a private reporting format or a private retry policy.
* The factory follows the same rule: `gen-vector.sh` is the only writer of vector files
  and manifest rows, and `check-vectors.sh` is the only validator - the container calls it
  and no suite is allowed a second copy of the format's checks. Reference shape for the
  validator's report: the rule/file/field wording of `linux:scripts/checkpatch.pl`.
