# Board test suites (Gate2)

Every board image in this tree is a *suite*: a directory with a declaration
(`suite.mk`), a self-contained build, and a judge. A host runner drives the
registered suites over the lab's serial/TFTP/power tools and judges each
transcript; the repository itself never carries lab paths, so the runner lives
in the development workspace and this file is its contract.

## Layout

```
tests/board/
  suites.mk                 registry: SUITES_BOARD := <suite> ...
  README.md                 this contract
  common/                   shared machinery (runner table, selector, judge library)
  <suite>/suite.mk          declaration (below)
  <suite>/Makefile          self-contained build, produces the declared image
  <suite>/judge.py          the suite's judge (contract below)
  <suite>/tests/*.c         case bodies (image-side suites)
  fixtures/                 protocol fixtures (declaration negatives, transcripts)
```

## Image policy

Per OS line there are at most two board images: a **smoke** image (base
bring-up only - the bisection tool) and a **regression** image (every supported
feature plus a case table). Feature work does not add images; it adds rows to
the regression image's table.

Three shapes in each image's Makefile are load-bearing, and
`tools/gates/check-board-images.sh` checks them from the source side: the
per-file flag argument must reach the compile command (`COMPILE_RULE` applies
`$(2)`, and no later rule redefines the same object without it); an image that
links the vendored formatter must link with section GC
(`-ffunction-sections -fdata-sections` plus `-Wl,--gc-sections`) - the
formatter engine's `printf_`/`vprintf_` entry points reference an unbound
`_putchar` sink and are not called by these images, so discarding their
sections is what keeps the link resolvable (`components/printf/PROVENANCE.md`);
and an image whose banner carries the build tag must depend on a tag stamp, so
a commit or a merge rebuilds it instead of certifying a stale banner.

## In-image case table

A regression image registers one entry per case through `BOARD_TEST(name, fn)`
into the `BOARD_TESTS` linker section (`tests/board/common/board_suite.h`), and
`board_suite_run()` walks it in link order. The table is data: adding a case is
a new entry plus a source file on the image's list - the runner never changes.
The case-name list lives in three places that the image's `make check`
reconciles (image section, `SUITE_CASE_NAMES`, the judge's table).

## Judgment channel

The image prints the same grammar the host-side gate uses, one line each:

```
BOARD-SUITE <name> select window_ms=<ms>                          # optional select window
BOARD-SUITE <name> start cases=<n> sel=<spec> list=<n1,n2,...>    # opening self-report
CASE <suite>/<case> PASS|FAIL                                     # one per finished case
BOARD-SUITE <name> result=pass|fail cases=<n> ok=<m> exit=<k>     # end sentinel (mandatory)
```

Noise lines between them are free. The end sentinel is mandatory: a transcript
without it is an incomplete run and is judged a failure, never a pass.

A select window lets the host run a subset of the table without rebuilding the
image (same image, same fingerprint): the runner sends `only=<glob>` or
`skip=<glob>` inside the window, silence means "run everything".

## suite.mk fields

| Field | Required | Meaning |
|---|---|---|
| SUITE_NAME | yes | must equal the directory name |
| SUITE_TRUTH_SOURCE | yes | one sentence naming the independent truth source |
| SUITE_IMAGE | yes | image path relative to the repository root |
| SUITE_BUILD | no | build command (default: `make -C <suite dir>`) |
| SUITE_PROFILE | yes | boot profile name the lab uses |
| SUITE_DEPLOY | yes | file name the image is deployed under |
| SUITE_SUCCESS_PATTERN | yes | early anchor the boot wait ends on |
| SUITE_SELECT_PATTERN | no | select-window anchor (needed to send a directive) |
| SUITE_END_PATTERN | yes | end sentinel pattern; absent in the transcript = incomplete |
| SUITE_SUMMARY_PATTERN | no | count/summary anchor (step-only suites have none) |
| SUITE_CASE_NAMES | no | declared case names, in order (name-level reconciliation) |
| SUITE_STEPS_FILE | no | two-column TSV of interaction steps (below) |
| SUITE_JUDGE | yes | judge command; the transcript path is appended |
| SUITE_TIMEOUT | yes | seconds the runner waits for the end pattern |
| SUITE_SEGMENTS | yes | segment-anchor declaration (`-` allowed) |
| SUITE_WIRELESS / SUITE_ROUTE | no / wireless only | route declaration for wireless suites |

## Judge contract

`<judge> <transcript>` exits **0** (pass), **1** (fail - a missing sentinel is
an incomplete run and a failure) or **2** (configuration error: the declaration
and the transcript cannot be reconciled). It prints a judgment block: per-item
reconciliation lines, a verdict line, and on failure a localization excerpt.

`<judge> --selftest` replays an embedded legal transcript plus a mutation matrix
(one mutation per class of tampering) and must go red on every mutation. A
judge that cannot go red is itself broken and its suite is blocked.

Judges consume one transcript and nothing else; they parse it with the shared
library `tests/board/common/board_judge.py`, which normalizes the two views
(stamp-stripped lines for protocol/anchors; ANSI-stripped received lines with
transmitted bytes removed for interaction steps). Expected values come from
independent sources - the emitting code in this tree, the protocol text, or a
recorded board transcript - never from the output under test.

## Interaction steps

A suite whose image carries a console shell declares `SUITE_STEPS_FILE`: a
two-column TSV (TAB-separated) of `send` and `expected response` rows. The
runner sends column 1 (with `\r`/`\t` decoded to real bytes, one command at a
time) and the judge verifies column 2, in order, on the transcript. One file is
both the runner's script and the judge's truth, so the two cannot drift.

## Evidence

A run leaves, per suite: `serial.log` (the raw byte transcript), `judge.out`,
`fingerprint.txt` (size plus the three-way crc32 check: host == staged ==
board), `boot_id.txt` and `control.tsv` (the round-over-round comparison row).
