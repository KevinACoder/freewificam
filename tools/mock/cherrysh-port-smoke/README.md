# cherrysh-port-smoke

Container suite for the CherrySH console shell on the project's port layer:
it drives one session per CMSIS-RTOS2 host runtime (ThreadX and FreeRTOS) over
a fixed input vector and turns the assertions into ten case lines.

## What it runs

`check.sh` runs both binaries built by `tests/host/cherrysh-port-smoke` with
`input.txt` on stdin (`help`, TAB completion of `cl`, an unknown command,
`exit`) and asserts on the shell's byte stream with the SGR colour codes
stripped. Five cases per backend:

| Case | Assertion | Expected value comes from |
|---|---|---|
| `<backend>-prompt` | the prompt line `user@freewificam:/$ ` appears | the prompt segment construction in `chry_shell.c` |
| `<backend>-help` | the listing shows `total function 4`, `total variable 2` and `shsize` | the counts are computed by the vendored `help` command from the tables; `shsize` never appears as typed input |
| `<backend>-completion` | `clear` was submitted (no `cl: command not found`) | TAB completion must resolve `cl` to a real command |
| `<backend>-notfound` | `nosuchcmd: command not found` appears | the literal in `chry_shell.c` |
| `<backend>-exit` | the process exits 0 | `exit` maps to the CMSIS host seam |

## Red control

The suite must be able to go red on purpose-built input: change the not-found
expectation in `check.sh` by one character and both `notfound` cases fail (the
container then exits 1); restore it and the suite is green again. The run
evidence records both outputs.

## Notes

* The build packs the command and variable tables (`-malign-data=abi` plus the
  `SUBALIGN` clauses in `tests/host/cherrysh-port-smoke/symtab.ld`): the core
  reads them as flat arrays, and x86-64 GCC's default over-alignment would
  otherwise leave padding between entries.
* A truncated or broken vector times the backend out (the console backend
  parks the task on end-of-input instead of spinning), which fails the `exit`
  case - a suite that cannot fail this way would be worth nothing.
