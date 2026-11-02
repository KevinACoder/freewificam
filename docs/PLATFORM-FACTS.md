# Platform facts: the either/or rule

The RTOS tree builds one image per (board, backend, case set). Most of what
differs between boards is *data*: a register base, a clock rate, an interrupt
number. A `board_conf.h` carries it, the generic code reads it, and getting a
value wrong shows up as a wrong number in a register dump.

A few facts are not like that. They are **either/or**: the two platforms do not
have a "wrong value" of the same thing, they have *different things* whose
handling differs in shape — a different register, or the same bit with the
opposite sense. Those are where images break in ways that look like something
else entirely.

## The rule

> Any either/or platform fact must be **named**, must be **required**, and must
> **fail the build** when it is absent or unknown. It is never a boolean with a
> default.

Three parts, each load-bearing:

1. **Named.** The `board_conf.h` picks a token that says *what the platform has*
   (`BOARD_CONSOLE_PL011`), not a flag that says *which branch to take*
   (`BOARD_UART_IS_PL011 1`). A reader of the board file learns the hardware; a
   reader of the consuming code sees an exhaustive list of the shapes it knows.

2. **Required.** No `#ifndef … #define … <default>`. A default is a decision
   made on behalf of a board that never stated its hardware, and it will be
   wrong for exactly the boards that forgot. The consumer starts with
   `#ifndef X / #error` instead.

3. **Fails closed.** The consumer ends with `#elif … #else / #error`, so a token
   it does not know is a build error rather than a silently skipped branch.

### The token values live with the consumer

`board_conf.h` names a token; the file that interprets it defines the numbers:

```c
/* arch/aarch64/start.S — the tokens' values live here, with the code that
 * interprets them. */
#define BOARD_CONSOLE_PL011	1
#define BOARD_CONSOLE_DW16550	2
```

This is not style. The C preprocessor evaluates an **undefined identifier as
0**, so two bare tokens compare equal and `#if X == BOARD_CONSOLE_PL011` takes
its first branch on every platform. Defining the numbers next to the `#if` is
what makes the comparison mean something.

## The two facts this rule came from

### The console part (`BOARD_CONSOLE_PART`)

| Token | Board | Register | Wait |
|---|---|---|---|
| `BOARD_CONSOLE_PL011` | firefly v1/v2, `0x2800d000` | FR at `+0x18` | while TXFF (bit 5) is **set** — FIFO full |
| `BOARD_CONSOLE_DW16550` | rk3568-itx, `0xfe660000` | LSR at `0x14` | while THR_EMPTY (bit 5) is **clear** — no room |

The pre-MMU breadcrumb has to poll this part before any driver exists, with
DAIF masked. Selecting the wrong one does not garble a line: the wait never
satisfies and the image prints **nothing at all**. That is indistinguishable
from "the image never started", and it cost a round: one commit made the
breadcrumb a 16550 poll, every firefly image built afterwards died before its
first character, and the two images that still booted were the ones built before
that commit.

### The SDIO host port (`SDMMC_PORT_SDIO_HOST_TYPE`)

Every controller this port has a glue for exports the same three symbols
(`SDMMCHOST_Init`, `FDWMMCHOST_SDIOConfig`, `FDWMMCHOST_CardIntEnable`). An
image that picks the wrong one **links fine** and then drives the wrong
controller. The port header used to default to `kSDMMCHOST_TYPE_FDWMMC`; it now
`#error`s, and each build names its own:

| Build | Port |
|---|---|
| `wlan.mk` (8189FTV) | `kSDMMCHOST_TYPE_FDWMMC` (rk3568 dwmmc) |
| `sdio_mci.mk` | `kSDMMCHOST_TYPE_FIREFLY_MCI` |
| `rtw88_sdio.mk` | `kSDMMCHOST_TYPE_FIREFLY_MCI` |

## Related: the compile-flag form of the same mistake

The same "silently borrows another platform's answer" failure has a second
route: a feature key that gates a **declaration** rather than an
implementation. `sdmmc_port.h` hides its entry points behind
`CONFIG_FSL_SDMMC_ENABLE_SDIO`; a translation unit that calls them without that
key gets an implicit `int` declaration, and the returned pointer is truncated to
32 bits. That is a fault inside `memcpy`, far from the call that caused it.

So: **a unit that calls the port compiles with the port's own selection flags.**
Each `.mk` keeps the flag set in one variable and gives it to every unit that
reaches that boundary, cases included (`RTW88_SDIO_PORT_CFLAGS` and
`RTW88_CASE_CFLAGS` in `tests/board/rtos/build/rtw88_sdio.mk`).

## Checklist for a new board or a new port

- [ ] Every either/or fact is a named token in the board's `board_conf.h`.
- [ ] Its consumer has `#ifndef → #error`, an exhaustive `#if/#elif`, and
      `#else → #error`.
- [ ] The token numbers are defined next to the consumer, and are distinct.
- [ ] A build that names the wrong port cannot link the wrong glue: the
      selection is required, not defaulted.
- [ ] Units that call a gated boundary carry the same selection flags as the
      units that implement it.
- [ ] Both branches have been preprocessed at least once
      (`gcc -E -x assembler-with-cpp` / `-fsyntax-only`) and produce the
      instructions the other platform expects.
