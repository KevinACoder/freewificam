/*
 * @file   finsh.h
 * @brief  RT-Thread finsh command-export shim -> CherrySH linker-set export.
 *
 * RT-Thread's MSH_CMD_EXPORT(_ALIAS) places a handler descriptor into the
 * FSymTab section; CherrySH's csh.h does the same job with
 * CSH_CMD_EXPORT_ALIAS_FULL. The vendored netutils code passes its usage
 * text as bare tokens (`MSH_CMD_EXPORT_ALIAS(cmd_ping, ping, ping network
 * host)`), so the shim stringifies them with #__VA_ARGS__ - one text
 * becomes both CherrySH's usage and help columns.
 *
 * The handler signature is identical on both sides: int (*)(int argc,
 * char **argv), with the shell instance riding argv[argc+1] (see
 * CSH_FROM_ARGV in cherrysh_adapter.h).
 */

#ifndef FWC_NETUTILS_SHIM_FINSH_H
#define FWC_NETUTILS_SHIM_FINSH_H

#include <rtthread.h>
#include "csh.h"

#define MSH_CMD_EXPORT(func, ...) \
	CSH_CMD_EXPORT_ALIAS_FULL(func, func, #__VA_ARGS__, #__VA_ARGS__)

#define MSH_CMD_EXPORT_ALIAS(func, name, ...) \
	CSH_CMD_EXPORT_ALIAS_FULL(func, name, #__VA_ARGS__, #__VA_ARGS__)

/* finsh-only C-expression export: the shell-facing command still needs to be
 * reachable, so it exports as a command of the same name (telnet uses this
 * for telnet_server). */
#define FINSH_FUNCTION_EXPORT(func, ...) \
	CSH_CMD_EXPORT_ALIAS_FULL(func, func, #__VA_ARGS__, #__VA_ARGS__)

#endif /* FWC_NETUTILS_SHIM_FINSH_H */
