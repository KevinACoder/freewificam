/**
 * @file    rtw88_floodping.h
 * @brief   The one symbol the contrib ping's result hook calls back into.
 * @author  zhugengyu
 * @date    11.10.2026
 * @design  the lwIP contrib ping exposes PING_RESULT as a per-reply callback
 *          macro (third-party/lwip/contrib/apps/ping/ping.c), so the counting
 *          lives in the case and the ping module stays untouched.  The image's
 *          Makefile compiles that one translation unit with
 *          -DPING_RESULT(ok)=rtw88_flood_ping_result(ok) and force-includes
 *          this header, which is the only reason the macro can name the
 *          function before ping.c's own text is read.
 */

#ifndef RTW88_FLOODPING_H
#define RTW88_FLOODPING_H

/** One ping cycle's outcome: 1 = echo reply matched, 0 = the cycle timed out
 *  or answered with an error.  The floodping case owns the counters. */
void rtw88_flood_ping_result(int ok);

#endif /* RTW88_FLOODPING_H */
