# PROVENANCE — components/cherrysh (vendored CherrySH subset)

- Upstream: https://github.com/cherry-embedded/CherrySH
- Pin: `8efe539c6e55b71d2f2cd1116cd00c4d64222a67` (`git describe` = `v1.0.1-20-g8efe539`)
- Import date: 2026-10-01
- License: Apache-2.0 (see ./LICENSE, kept verbatim)
- Form: vendored byte-identical subset (NOT a submodule; see "Promotion" below)
- Provenance chain: the bytes were verified against the prior project's
  zero-patch pinned submodule clone
  (`~/workspace/project_web_camera/freewebcamera/third-party/cherrysh` @ the
  same pin, per its IMPORT-INFO.md "byte-identical to the pin"); every
  imported file diffs empty against that clone (run log: runs/<id>/README.md).

## Imported subset

- Top level: `chry_shell.c`, `chry_shell.h`, `csh.h`, `csh_config_template.h`,
  `CHANGELOG.md`, `README.md`, `README_zh.md`, `LICENSE`
- `cherryrl/`: `chry_readline.c`, `chry_readline.h`, `chry_readline_config.h`,
  `chry_readline_keycode.h`
- `builtin/`: `help.c`, `clear.c`, `shsize.c`

## Excluded

- `builtin/login.c` (no login/multi-user; spec N5)
- `builtin/lsusb.c` (returns with the USB feat line, S5; spec N7)
- `samples/`, `doc/`, `cherryrl/example.c` (no build value; spec §3.1.1)

## Zero-difference declaration

No byte of any imported file differs from the upstream pin. All adaptation
lives in `port/adapters/cherrysh/` (adapter = the only layer allowed to touch
this tree's build surface). `csh_config.h` is NOT upstream - it is the
adapter's shadow config; upstream ships only `csh_config_template.h`, which
must never shadow the real config (include order discipline, Makefile §3.8.2).

## Usage

Interactive console shell: readline (cherryrl) + REPL + FSymTab/VSymTab
command tables. Platform surface = two function pointers (sput/sget) in
`chry_shell_init_t`.

## Promotion back to submodule

The pin is locked; at any time:

    git submodule add https://github.com/cherry-embedded/CherrySH \
        third-party/cherrysh && \
    (cd third-party/cherrysh && git checkout 8efe539c6e55b71d2f2cd1116cd00c4d64222a67)

then `diff -r` this tree against it - zero differences = reconciliation
passed, after which standard submodule + patches/ discipline applies.
Promotion triggers (spec §5 open question 3): any upstream byte change
(e.g. fixing the builtin help quirk), a version bump, or local deltas that
need replayable patches.
