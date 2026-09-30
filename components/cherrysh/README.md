**English | [简体中文](README_zh.md)**

<h1 align="center" style="margin: 30px 0 30px; font-weight: bold;">CherrySH</h1>
<p align="center">
	<a href="https://github.com/cherry-embedded/CherrySH/releases"><img src="https://img.shields.io/github/release/cherry-embedded/CherrySH.svg"></a>
	<a href="https://github.com/cherry-embedded/CherrySH/blob/master/LICENSE"><img src="https://img.shields.io/github/license/cherry-embedded/CherrySH.svg?style=flat-square"></a>
</p>

CherrySH is a tiny shell specifically designed for embedded applications.

## Features

- Support TAB completion, including command and path completion
- Support command history, navigable with the `↑` and `↓` arrow keys
- Support environment variables using the `$` prefix, e.g., `$PATH`
- Support setting username, hostname, and path/prompt
- Support non-blocking mode; compatible with bare-metal and RTOS
- Support cursor left/right movement and `HOME`/`END` cursor positioning
- Support key combinations, including `Ctrl + <key>`, `Alt + <key>`, `F1-F12`, etc.
- Support signal handling: capture and handle signals such as `Ctrl+C` (`SIGINT`) and `Ctrl+Z` (`SIGTSTP`) to interrupt the currently running shell task
- Support user login; requires implementing a hash function (default: `strcmp`)
- Support adding, modifying, deleting, and reading environment variables
- Support filesystems: FatFS, FileX, LittleFS, RomFS, etc. (TODO)
- Support an `exit` mechanism to terminate command execution, return context, and call a configured handler; can be implemented with `setjmp` for bare-metal (TODO)
- Support job control: run commands in foreground or background and manage them with `fg`, `bg`, `jobs` (TODO)
- Support multi-user command permissions (TODO)

## Demo

![test1](./doc/test1.png)

![test2](./doc/test2.png)

## Porting

Taking the example of the HPMicro hpm5301evklite board.

- Command lookup uses the GCC section, so we need to modify the linkerscript file to add the relevant section, for example in gcc ld:

```
    .text : {
    .....

    . = ALIGN(4);
    __fsymtab_start = .;
    KEEP(*(FSymTab))
    __fsymtab_end = .;
    . = ALIGN(4);
    __vsymtab_start = .;
    KEEP(*(VSymTab))
    __vsymtab_end = .;
    . = ALIGN(4);
    }

```

- Implement shell put and get api, and suggest to use rx irq + ringbuf to get chars.

``` c
#include "csh.h"

static chry_shell_t csh;

static uint16_t csh_sput_cb(chry_readline_t *rl, const void *data, uint16_t size)
{
    uint16_t i;
    (void)rl;
    for (i = 0; i < size; i++) {
        if (status_success != uart_send_byte(HPM_UART0, ((uint8_t *)data)[i])) {
            break;
        }
    }

    return i;
}

static uint16_t csh_sget_cb(chry_readline_t *rl, void *data, uint16_t size)
{
    uint16_t i;
    (void)rl;
    for (i = 0; i < size; i++) {
        if (status_success != uart_receive_byte(HPM_UART0, (uint8_t *)data + i)) {
            break;
        }
    }

    return i;
}
```

- Init shell, refer to samples
- Call `chry_shell_task_exec` and `chry_shell_task_repl` in task，refer to samples
- Set system env variables

``` c
#define __ENV_PATH "/sbin:/bin"
const char ENV_PATH[] = __ENV_PATH;
CSH_RVAR_EXPORT(ENV_PATH, PATH, sizeof(__ENV_PATH));

#define __ENV_ZERO ""
const char ENV_ZERO[] = __ENV_ZERO;
CSH_RVAR_EXPORT(ENV_ZERO, ZERO, sizeof(__ENV_ZERO));
```

- Use `CSH_CMD_EXPORT` to export command

``` c
static int write_led(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: write_led <status>\r\n\r\n");
        printf("  status    0 or 1\r\n\r\n");
        return -1;
    }

    board_led_write(atoi(argv[1]) == 0);
    return 0;
}
CSH_CMD_EXPORT(write_led, );

```
