# RELEASE - cutting a release of this repository

## Version numbers

- `VERSION` carries the number; `CHANGELOG.md` carries the per-release list.
- The numbering is this repository's own and starts at `0.0.x`: `0.0.2` is the
  first release tag. It is not comparable with the reference tree's `0.1.0`,
  which describes a different tree.
- A release number is cut from a commit, not from a working tree: the tag is
  the released state, and `VERSION` in the tag matches the tag's name without
  the `v`.
- Tags are annotated: `vX.Y.Z`. The branch model is:
  - `master` - release commits only (a release, or a fix that a release
    carries);
  - `dev_v2` - development. Work lands here first; `master` is cut from it.

## What a release contains

- the code, the build entries and the tests;
- `VERSION`, `CHANGELOG.md`, `LICENSE`, `LICENSES.md`, `docs/RELEASE.md`;
- the release notes for this version under `docs/`;
- the open issues this version carries under `known_issues/`.

It does not contain: build outputs (`build/`, `*.o`, `*.map`, `*.elf`, `*.bin`),
the host mock tier, or any measurement record. The records that back the
numbers in the release notes are kept outside this repository; the notes say
what was measured and how, not where the capture lives.

## Before tagging

Run, from the repository root, on a clean tree:

```sh
export AARCH64_CROSS_PATH=/path/to/aarch64-none-elf     # or put it on PATH
tools/rtos/build.sh images        # both kernels of the configured profile, with gates
tools/rtos/build.sh host-test     # the host conformance tier, both kernels
tools/rtos/build.sh digest        # every imported tree against its patch record
tools/check-release-docs.sh       # reader-facing docs carry no private references
```

Then check the tree itself:

```sh
git status --short                # must be empty
git describe --always --dirty     # must not end in -dirty
```

Board evidence is gathered before the release commit series is finished, and it
is gathered on **the bytes of a committed tree**: build the images at the commit
that will be released, record their size and sha256, and run one clean boot per
arm. The release notes state which commit was certified; if a documentation
commit follows, the release record says the difference is documentation only.

The gate list for a release is the four commands above plus the board round for
the arms the release notes claim. A release that cannot be certified on the
board is not a release; it is a snapshot of a branch, and the version number
stays on the development branch.

## Cutting the tag

On the release commit:

```sh
git checkout dev_v2
git status --short                       # clean
git branch -f master dev_v2              # or: git checkout -b master dev_v2
git tag -a v0.0.2 -m "freewificam 0.0.2: <one line>"
git describe --tags                      # prints v0.0.2
git describe --tags --exact-match        # prints v0.0.2
git cat-file -t v0.0.2                   # prints: tag
```

`git describe --tags` on the tagged commit must print the tag alone; a
`v0.0.2-3-g...` form means the tag is not on the commit being released.

Pushing is a separate, deliberate step. The release is not published by
creating the tag locally.

## After a release

- `master` takes only the commits a release carries: fixes, documentation
  corrections, a version bump. Development continues on `dev_v2`.
- `known_issues/<version>.md` is copied forward for the next release; closed
  entries get `fixed in <version>` and stay in the older file. Old files are
  not edited.
- `CHANGELOG.md` gains the next section; do not rewrite a released section
  except to correct a factual error, and say so in the entry.

## Rebuilding a released image

The RTOS images are a function of one profile file, not of the command line:

1. check out the tag;
2. put an `aarch64-none-elf` toolchain on `PATH`, or set `AARCH64_CROSS_PATH`;
3. select the profile in `tests/board/rtos/config.mk` (the release notes name
   it), optionally with `VENDOR_8189FS` for the vendor SDIO line;
4. `tools/rtos/build.sh images`.

The image embeds its build tag (`git describe --always --dirty`) in the banner,
so the same source built from a different commit or a dirty tree differs in
those bytes and in nothing else. The release notes' numbers describe behaviour,
not a sha256 of the tag's rebuild.

The Linux kernel module is rebuilt from the same source with
`tests/board/linux-ko/build.sh`; `DRIVER` selects the arm and the kernel tree
and its release string are the caller's.

## Reader-facing documents

`tools/check-release-docs.sh` enforces what the README states: the documents a
release puts in front of a reader carry no machine-local paths, no private
repository or bench names, no internal round identifiers, no references to
records that do not live in this repository, and no non-ASCII text. The gate
scans `README.md`, `CHANGELOG.md`, `VERSION`, `LICENSE`, `LICENSES.md`,
`docs/*.md` and `known_issues/*.md`; it skips `docs/COMMIT-CONVENTION.md`, which
has to spell out the terms it bans.

Code comments and the imported trees are outside that scan: an imported file's
comments stay as imported, and a project-authored comment that needs context
gets it in the importing record (`PROVENANCE.md`, `SOURCE.md`,
`DIVERGENCES.md`).
