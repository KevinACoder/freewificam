# freewificam - dev_v2

Final target: the **L2 linuxko line** - the rtw89 driver together with the
net80211 stack inside one Linux kernel module, then the L3 RTOS re-host.  This
is the same shape as the previous `dev` line's kernelmod work
(`components/net80211/` + `ports/linux/` + `ports/net80211/`).

This is the public release repository.  It will contain only the final L2/L3
code and the tests, tools and docs that belong to it.  L1 measurement probes,
the firmware TRM, specs, issues and run evidence live in the workspace and
never land here.

Rules:

- Do not merge or rebase the previous `dev` line. Take a file explicitly with
  `git show <sha>:<path>` and record the source commit in the importing commit
  message.
- Each commit builds; documentation and tooling commits prove it with the
  commit-message checker.
- ASCII in the repository; the workspace documents may be Chinese.
- No local paths, lab identifiers, knowledge-base references or run evidence.
- Commit format: `docs/COMMIT-CONVENTION.md`; gate: `tools/check-commit-msg.sh`.

Planned layout (the `dev` shape):

```
components/net80211/ the imported net80211 stack
ports/linux/         Linux port and the rtw89 driver glue
ports/net80211/      the RTOS-side net80211 port
tests/               host and board tests
tools/               gates and helpers
docs/                commit convention and the linux-ko recipe
```

The L1 probe is not part of this repository; it stays in the workspace.
