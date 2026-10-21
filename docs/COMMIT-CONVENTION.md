# COMMIT-CONVENTION - commit messages for dev_v2

Adapted from `wcam@2c0d375:docs/COMMIT-CONVENTION.md` for this repository's
areas and the L1 firmware-contract scope. The checker is
`tools/check-commit-msg.sh`; install it with `tools/install-hooks.sh`.

## 1. Subject

```
<area>: (<label>) <lowercase imperative summary>
```

- `area` is the landing point, lowercase: `repo`, `docs`, `tools`, `wlan`,
  `netdev`, `bridge`, `board`, `lab`, `kernelmod`, `linuxnative`, `rtos`,
  `trm`, `proxy`, `contract`, `catalog`, `model`, `conformance`.
- `label` is the line, phase or topic, lowercase: `bootstrap`, `convention`,
  `plan`, `migration`, `gates`, `hooks`, `import`, `provenance`, `kernelmod`,
  `linuxnative`, `rtos`, `trm`, `proxy`, `contract`, `catalog`, `model`,
  `conformance`, `fix`, `docs`, `linux-ko`.
- `summary` is ASCII, lowercase imperative, at most 72 characters, no
  trailing period.

Examples:

```
repo: (bootstrap) start dev_v2 from an orphan root
docs: (convention) define the commit format and the line names
tools: (gates) add the commit-message checker and hook
trm: (trm) add the WCPU firmware-control status word
proxy: (obs) record H2C and C2H with one marker
contract: (catalog) add the scan-offload message entry
```

## 2. Body

Leave one blank line after the subject. The body is English, wrapped at 72
columns, and explains **why**; the diff explains what.

Recommended sections, as needed:

```
Why: ...
Evidence: runs/<date>-<topic>/... (command, sha256, boot_id)
Provenance: <repo>@<sha>:<path> (license, byte-identical or divergences)
Workaround: <what> / owner <x> / hides <mismatch> / remove when <y>
Doc-sweep: <files updated>
```

- Imports must carry `Provenance:` with the source commit or tag, path,
  license and whether the file is byte-identical.
- A workaround is recorded with its layer, owner, hidden mismatch and removal
  condition; an unrecorded workaround does not count as working.
- Documentation changes that belong to the same behaviour land in the same
  commit; a rule change names the rule and the reason.

## 3. Atomicity and buildability

- One commit, one change; every commit must build (bisectable).
- During bring-up, keep debugging changes in the workspace and land a clean
  series after the verdict. No `WIP`, `backup`, `fixup` or `tmp` commits.
- Do not write local paths, lab identifiers or knowledge-base references into
  this repository.

## 4. Banned terms

The checker rejects, in subject and body:

- `K arm`, `L arm`, `R arm`, `K/L`, `L/K`;
- `WIP`, `wip`, `backup`, `fixup`, `tmp`;
- `~/`, `/home/`, `/mnt/`, `KI-`, `oslab`, `itx-wiki`,
  `project_rtos_webcam`.

Historical evidence may keep old names; new messages and comments may not.

## 5. Before committing

1. `git diff` contains one change and no build output or temporary files.
2. The commit builds, or for docs/tools the checker runs.
3. The message passes `tools/check-commit-msg.sh <msg-file>`.
4. Related `docs/` pages are updated or the commit says they are not needed.
5. Any workaround is recorded.

## 6. History

- The previous line's commits (`wlan: (k1) ...`, `rtl8189f: (native) ...`)
  are not replayed. Their files may be taken explicitly with `git show` and
  must name the source commit.
- This convention starts with the first commit of `dev_v2`.
