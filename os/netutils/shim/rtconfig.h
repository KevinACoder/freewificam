/*
 * @file   rtconfig.h
 * @brief  RT-Thread config shim for the vendored netutils sources.
 *
 * The netutils components gate themselves on PKG_NETUTILS_* (menuconfig on
 * RT-Thread) and on RT_VER_NUM branches (dfs_posix vs unistd). This shim pins
 * the choices this trunk makes: only the vendored subset (ping/tftp) is on -
 * NTP/TELNET/TCPDUMP/NETIO are cut (S7b 裁剪裁决, 宁少勿多),
 * finsh command export is on (mapped to CherrySH in finsh.h), and the modern
 * (>= 0x40100) unistd branch is taken everywhere. The one number that is load
 * bearing beyond the preprocessor is RT_TICK_PER_SECOND: the ThreadX tick is
 * 1 ms (tx_user.h), which matches RT-Thread's default, so rt_tick values map
 * 1:1 onto osDelay ticks - see netutils_shim.c.
 *
 * This file is found FIRST for the netutils compile world (the shim include
 * directory precedes the toolchain's newlib headers), like the lwipopts.h
 * shadow in the lwip world.
 */

#ifndef FWC_NETUTILS_SHIM_RTCONFIG_H
#define FWC_NETUTILS_SHIM_RTCONFIG_H

/* 5.2.0: take the modern branches everywhere (unistd/fcntl, not dfs_posix).
 * The tftp port file is rewritten in the adapter anyway; the number only
 * picks preprocessor paths in vendored code. */
#define RT_VER_NUM                       0x50200

#define RT_TICK_PER_SECOND               1000
#define RT_NAME_MAX                      16

#define RT_USING_FINSH                   1
#define FINSH_USING_MSH                  1

#define PKG_NETUTILS_PING                1
#define PKG_NETUTILS_TFTP                1

#endif /* FWC_NETUTILS_SHIM_RTCONFIG_H */
