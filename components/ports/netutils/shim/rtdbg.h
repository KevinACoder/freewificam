/*
 * @file   rtdbg.h
 * @brief  RT-Thread debug-log shim for the vendored netutils sources.
 *
 * rtdbg.h on RT-Thread expands LOG_I/W/E/D to rt_kprintf with a level tag.
 * The vendored netutils code writes single-fmt LOG_* calls (no two-arg
 * "tag" form), which this shim maps straight onto the session-routed
 * printf. LOG_D is compiled in only where the component's DBG switch asks
 * for it, same as upstream.
 */

#ifndef FWC_NETUTILS_SHIM_RTDBG_H
#define FWC_NETUTILS_SHIM_RTDBG_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

void netutils_shim_log(const char *level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#ifdef __cplusplus
}
#endif

#define LOG_I(...)	netutils_shim_log("I", __VA_ARGS__)
#define LOG_W(...)	netutils_shim_log("W", __VA_ARGS__)
#define LOG_E(...)	netutils_shim_log("E", __VA_ARGS__)
#define LOG_D(...)	netutils_shim_log("D", __VA_ARGS__)

/* rtdbg level constants as strings: vendored code passes them to dbg_log
 * (tcpdump calls dbg_log(DBG_ERROR, ...)), and the shim's logger takes the
 * level as text. */
#ifndef DBG_ERROR
#define DBG_ERROR	"E"
#define DBG_WARNING	"W"
#define DBG_INFO	"I"
#define DBG_LOG		"D"
#endif

#define dbg_log(level, fmt, ...)	netutils_shim_log(level, fmt, ##__VA_ARGS__)

#endif /* FWC_NETUTILS_SHIM_RTDBG_H */
