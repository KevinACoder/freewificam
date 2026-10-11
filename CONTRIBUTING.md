# Contributing

Thanks for your interest. This is a portable RTOS WiFi stack: the wireless
core (net80211) plus the Linux drivers and the supplicant, running on more than
one RTOS kernel through a single CMSIS-RTOS2 abstraction. The project values
small, auditable, upstreamable changes.

## Before you start

- Read `README.md` for the shape of the tree and `LICENSES.md` for the license
  of every vendored, imported and binary unit.
- Build and run the host mock tier first. It is the fast feedback loop:

  ```sh
  make -C freewificam mock      # host mock suites (Gate1)
  make -C freewificam gates     # static gates (local-knowledge, build shape)
  ```

## The two gates

A change is only as good as the evidence behind it. Two tiers:

- **Gate1 (host mock)** runs on the development host, in seconds to minutes.
  It must be green before anything goes near hardware.
- **Gate2 (board)** is the only authority for "works on hardware". A board run
  is recorded with its serial transcript and image fingerprints.

A bug found on hardware is turned into a new Gate1 case (that fails first, then
passes) or registered as a board-only category. "Works" is never claimed
without the matching evidence.

## Commit conventions

- **Language**: English, everywhere in this repository.
- **Subject**: `<area>: (label) Imperative summary`, where `<label>` is one of
  `(template)`, `(driver)`, `(fix)`, `(port)`, `(docs)`, `(minor)`. Example:

  ```
  rtw89: (driver) Accept a short control read as a valid transfer
  ```

- **Body**: wrap at 72 columns and explain *why*, not *what*. The diff already
  says what changed.
- **Atomic and buildable**: one concern per commit, and every commit builds.
  This keeps `git bisect` useful.
- **Verify before committing**: run `make -C freewificam mock` and
  `make -C freewificam gates`; commit only after they pass. Do not write "pass"
  in a commit message without the evidence to back it.

## Branches

`master` is the mainline. Work happens on short-lived branches named
`feat-<topic>` or `fix-<topic>` (hyphens, not slashes), one feature per branch,
merged when green.

## Vendored and imported code

- `third-party/*` are pinned submodules. Their working trees stay clean.
- Fixes and features for upstream trees go in `patches/<repo>/` as
  community-style `git format-patch` files (one concern per patch, English,
  `Signed-off-by`), replayed at build time. `patches/<repo>/README.md` records
  the base commit and the submission status.
- Imported units under `components/` are verbatim snapshots with a
  `PROVENANCE.md` and a `DIVERGENCES.md` next to them. Do not edit their bytes;
  record a divergence instead.

## Local knowledge

This repository must read correctly to anyone who has never seen the
development machine. No absolute local paths, no lab identifiers, no references
to development ledgers, and no non-ASCII text outside `README.md`. The gate
`tools/gates/check-local-knowledge.sh` enforces this; run it before committing.
