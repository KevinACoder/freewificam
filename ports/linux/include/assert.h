/*
 * @file
 * @brief assert() for the kernel build.
 *
 * The imported AES self-tests assert; the port gives up through abort(), which
 * parks the task and names the line in dmesg.
 */

#ifndef _WLAN_LINUX_ASSERT_H_
#define _WLAN_LINUX_ASSERT_H_

void abort(void) __attribute__((__noreturn__));

#ifndef assert
#define assert(expr)	((expr) ? (void)0 : abort())
#endif

#endif /* _WLAN_LINUX_ASSERT_H_ */
