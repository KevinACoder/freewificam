/*
 * @file    lwip_stack.h
 * @brief   Bring the lwIP stack up exactly once for this image's cases.
 *
 * tcpip_init() initialises the stack and starts its TCP/IP thread; it may run
 * only once per image. The cases that need the stack (loopback echo, the GMAC
 * data plane) both call lwip_stack_up(); the first caller runs the init and
 * every caller then waits until the stack's own init callback has run on the
 * TCP/IP thread, which is the moment netif management is legal.
 */

#ifndef BOARD_LWIP_STACK_H
#define BOARD_LWIP_STACK_H

/* 0 when the stack is up (already or after this call), -1 on failure. */
int lwip_stack_up(void);

#endif /* BOARD_LWIP_STACK_H */
