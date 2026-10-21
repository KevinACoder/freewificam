# freewificam - dev_v2

From-zero code repository for the L1 firmware-contract work on the RTL8851BU
(rtw89, USB), and later for the L2 stack swap and the L3 RTOS re-host.

Status: bootstrap. Only the repository rules, the commit convention and the
migration record exist; there is no build or driver code yet.

Rules:

- Do not merge or rebase the previous `freewificam` `dev` line. Take a file
  explicitly with `git show <sha>:<path>` and record the source commit in the
  importing commit message.
- Each commit builds; documentation and tooling commits prove it with the
  commit-message checker.
- ASCII in the repository; the workspace documents may be Chinese.
- Commit format: `docs/COMMIT-CONVENTION.md`; gate: `tools/check-commit-msg.sh`.
- No local paths, lab identifiers or knowledge-base references.

Layout (planned):

```
contract/    message, status and coupling ledgers
catalog/     protocol catalogue (class/func, direction, length, ack)
proxy/       H2C/C2H observation and injection
model/       stub firmware
conformance/ output comparison for L2/L3
fw-trm/      the RTL8851BU TRM
tests/       host and board tests
```

Workspace documents: `../README.md`, `../AGENTS.md`, `../ROADMAP.md`,
`../docs/INDEX.md`.
