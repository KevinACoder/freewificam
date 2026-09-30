**[English](README.md) | 简体中文**

<h1 align="center" style="margin: 30px 0 30px; font-weight: bold;">CherrySH</h1>
<p align="center">
	<a href="https://github.com/cherry-embedded/CherrySH/releases"><img src="https://img.shields.io/github/release/cherry-embedded/CherrySH.svg"></a>
	<a href="https://github.com/cherry-embedded/CherrySH/blob/master/LICENSE"><img src="https://img.shields.io/github/license/cherry-embedded/CherrySH.svg?style=flat-square"></a>
</p>

CherrySH 是一个专为嵌入式应用程序而设计的微型Shell。

## 功能

- 支持 TAB 键补全，包括命令和路径补全
- 支持历史记录，通过 ``↑`` ``↓`` 按键
- 支持环境变量，需使用 ``$` `作为前缀，例如` `$PATH``
- 支持设定用户名、主机名、路径
- 支持非阻塞模式，支持裸机和 RTOS
- 支持光标左右移动，支持 ``HOME``、``END`` 切换光标
- 支持组合按键，包括 ``Ctrl + \<key\>`` ``Alt + \<key\>`` ``F1-F12` 等
- 支持信号处理，捕获和处理不同的信号，例如Ctrl+C``SIGINT``和Ctrl+Z``SIGTSTP``，可中断当前执行的 shell 任务
- 支持用户登录，需要实现hash函数，默认strcmp
- 支持环境变量添加、修改、删除、读出
- 支持文件系统，FatFS，FileX，LittleFS，RomFS等（TODO）
- 支持 exit 函数实现终止命令执行以及现场返回并调用设定的handler，利用setjmp实现(裸机)（TODO）
- 支持作业控制，可以在前台或后台运行命令，并使用相关的控制命令（如fg、bg、jobs）来管理和操作作业（TODO）
- 支持多用户命令权限（TODO）

## 示例

![测试1](./doc/test1.png)

![测试2](./doc/test2.png)

## 移植

以先楫半导体hpm5301evklite为例。

- 命令查找采用的是 gcc 的 section 功能，因此，我们需要先修改 linkerscript 文件，增加相关 section,举例 gcc ld 文件：

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

- 实现字符输入输出函数，接收推荐用中断 + ringbuf的形式

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

- 初始化 shell，参考 samples 中实现
- 调用 `chry_shell_task_exec` 和 `chry_shell_task_repl`，参考 samples 中实现
- 配置系统环境变量

``` c
#define __ENV_PATH "/sbin:/bin"
const char ENV_PATH[] = __ENV_PATH;
CSH_RVAR_EXPORT(ENV_PATH, PATH, sizeof(__ENV_PATH));

#define __ENV_ZERO ""
const char ENV_ZERO[] = __ENV_ZERO;
CSH_RVAR_EXPORT(ENV_ZERO, ZERO, sizeof(__ENV_ZERO));
```

- 使用 `CSH_CMD_EXPORT` 导出命令

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
