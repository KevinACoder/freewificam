# STATUS

- Updated: 2026-11-10
- Stage: **compile and link** (the stack is not built against this repository yet)

## Stages

Each stage is stated as what it is. A stage is announced only when the one
below it has been reached; "compiles" and "links" are not "runs".

| Stage | Meaning | State |
|---|---|---|
| C0 compile | the stack's translation units produce object files | not started |
| C1 link | no undefined symbols | not started |
| C2 register | the stack accepts a device registration | not started |
| C3 attach | a driver probes, firmware loads, start completes | not started |
| C4 scan | a scan returns the target BSS | not started |
| C5 associate | link establishment completes | not started |
| C6 data | frames cross in both directions | not started |

## Preconditions

See `README.md`: the uniprocessor assumption is a precondition of the design,
not an implementation detail.
