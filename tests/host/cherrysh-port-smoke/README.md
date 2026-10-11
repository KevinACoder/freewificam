# cherrysh-port-smoke

Host smoke test of the CherrySH console shell on the project's port layer
(`ports/cherrysh/`). The shell core is the imported subset under
`components/cherrysh/`; everything this test actually brings up is the port:
the config header, the instance wiring, the repl task and the console backend,
on both CMSIS-RTOS2 host runtimes (ThreadX and FreeRTOS).

## Build and run

```
make -C tests/host/cherrysh-port-smoke
tests/host/cherrysh-port-smoke/build/threadx/cherrysh-smoke-threadx  < tests/host/cherrysh-port-smoke/input.txt
tests/host/cherrysh-port-smoke/build/freertos/cherrysh-smoke-freertos < tests/host/cherrysh-port-smoke/input.txt
```

The container suite (`tools/mock/cherrysh-port-smoke`, the project's fast set)
builds both binaries, feeds the same vector and turns the assertions below
into ten case lines. `make cross32` compiles the shell side for a 32-bit
target (arm-none-eabi, cortex-m4) as a syntax-only contract check.

## What it checks

One session per backend, driven by `input.txt` (`help`, TAB completion of `cl`,
an unknown command, then `exit`). The run is accepted when:

* the prompt appears (`user@freewificam:/$ `, plain-text anchors `freewificam`
  and `$ ` - the segments carry SGR colour codes);
* `help` lists the registered commands (the decisive name is `shsize`, which
  never appears as typed input);
* TAB completes `cl` to a real command - a broken completion chain submits the
  literal `cl` and prints `cl: command not found`;
* the unknown command prints `nosuchcmd: command not found` (the upstream
  literal from `chry_shell.c`);
* `exit` ends the run with exit code 0 through the port's host backend.

Expected values come from the upstream sources, never from this build: the
not-found text and the prompt construction are upstream literals, and the
command names are the export sites of the vendored `builtin/*.c`.

## What is compiled

| Source | Role |
|---|---|
| `components/cherrysh/**` (5 of 14 kept files) | the shell core and readline; byte-identical to the upstream pin |
| `ports/cherrysh/csh_config.h` | the port's configuration object that the core includes |
| `ports/cherrysh/shell_port.c` | instance wiring, the repl task, the `exit` command, `PATH`/`ZERO` |
| `ports/cherrysh/console_host.c` | console backend: stdout write loop, blocking stdin read loop, host exit |
| `symtab.ld` | FSymTab / VSymTab boundaries, inserted into the default linker script |
| `third-party/{threadx,FreeRTOS-Kernel,CMSIS_6,CMSIS-FreeRTOS}` | the two kernels' host runtimes and the CMSIS-RTOS2 header, compiled as-is |

## Host notes

* The host kernels drive their threads with signals (ThreadX suspend/resume,
  the FreeRTOS POSIX tick), so a blocking read can return early with EINTR;
  the console backend retries on that and parks the task on end-of-input
  instead of spinning - a truncated vector times the suite out rather than
  passing quietly.
* `exit` maps to `cmsis2_host_end_scheduler()`: the ThreadX backend ends the
  process, the FreeRTOS backend stops its scheduler and `main` returns 0.
* The board backend (`console_board.c`) is the sibling of `console_host.c` and
  arrives with the board round; `shell_port.c` is shared by both.
