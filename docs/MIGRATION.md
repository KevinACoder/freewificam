# MIGRATION - what dev_v2 takes from the previous freewificam line

Source: the `freewificam` `dev` branch in the workspace repository, 64 commits,
HEAD `31adcfe`. The history is not merged or replayed; this file records the
decision per group so the provenance stays auditable.

## 1. Groups

| Group | Commits | Content | Decision |
|---|---|---|---|
| `repo` | 1 | orphan root, README | adopt the shape, not the history |
| `docs`/`tools` | 8 | commit convention, checker, hook, linux-ko recipe | port and adapt |
| `wlan` (kernelmod) | 28 | net80211 bridge, rtw88/rtw89 in a Linux module | keep as the L2 reference; import files explicitly, never merge |
| `rtl8189f` (native) | 27 | vendor FullMAC front-end | do not import; FullMAC is a separate family |

The first commit is `repo: (bootstrap) start dev from an orphan root`
(`a072a7c`). The from-zero shape is kept: `dev_v2` has no common ancestor with
the old line.

## 2. Rules for taking a file

1. Do not merge or rebase the old branch.
2. Take one file explicitly: `git show <sha>:<path> > <path>`.
3. Name the source in the commit body:

   ```
   Provenance: <repo>@<sha>:<path> (license, byte-identical or divergences)
   ```

4. If the file is changed, record the divergence in `docs/DIVERGENCES.md` or
   the import is not complete.
5. Every import commit must build, or for documentation/tooling must run the
   commit-message checker.

## 3. Initial series

| # | Subject | Content | State |
|---|---|---|---|
| D0 | `repo: (bootstrap) start dev_v2 from an orphan root` | `README.md`, `.gitignore` | landed |
| D1 | `docs: (convention) define the commit format and the line names` | `docs/COMMIT-CONVENTION.md` | landed |
| D2 | `tools: (gates) add the commit-message checker and hook` | `tools/check-commit-msg.sh`, `tools/install-hooks.sh` | landed |
| D3 | `docs: (migration) record what dev_v2 takes from freewificam dev` | this file | landed |
| D4 | `repo: (build) add the aarch64 build entry` | build entry | gated on L1 coding |
| D5 | `docs: (linux-ko) import the kernelmod line recipe` | `docs/linux-ko.md` | gated on L2 |
| D6 | `wlan: (import) vendor the pinned rtw89 8851BU driver tree` | driver import | gated on L1 need |
| D7 | `contract: (trm) add the message ledger skeleton` | `contract/messages.yaml` | gated on L1 W1 |
| D8 | `proxy: (obs) add the H2C/C2H observation skeleton` | `proxy/obs/` | gated on L1 W1 |

## 4. Scope boundary

- `dev_v2` starts with the L1 firmware-contract work on the RTL8851BU (rtw89,
  USB). The L2 stack swap and the L3 RTOS re-host follow only after the G1
  gate in the workspace roadmap.
- The `rtl8189f` vendor FullMAC line is out of scope. If it returns, it enters
  as its own family after the six-question go/no-go.
- Driver and firmware licensing follows the workspace analysis: rtw89/rtw88
  are taken under BSD-3-Clause; GPL-only trees need a separate decision.
