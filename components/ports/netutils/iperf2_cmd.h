/*
 * @file   iperf2_cmd.h
 * @brief  lwiperf client front end - programmatic start for boot autorun.
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#ifndef OS_NETUTILS_IPERF2_CMD_H
#define OS_NETUTILS_IPERF2_CMD_H

/* Start the 10 s lwiperf TCP client against ipstr ("192.168.0.18").
 * Must run with the tcpip thread up (net READY). 0 on success. */
int iperf2_client_start(const char *ipstr);

#endif /* OS_NETUTILS_IPERF2_CMD_H */
