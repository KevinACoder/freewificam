/*
 * @file   cpu_boost.h
 * @brief  One-shot CPU cluster clock/voltage raise (N7 R5a).
 *
 * @author zhugengyu
 * @date   02.10.2026
 */

#ifndef FREEWEBCAM_CPU_BOOST_H
#define FREEWEBCAM_CPU_BOOST_H

#include <stdint.h>

/*
 * Raise the little cluster to @mhz (a Linux rk3568 opp row: 1104/1416/1608/
 * 1800) before the scheduler comes up. Pairs the frequency with its opp
 * voltage (rk3568.dtsi) over i2c0->RK809 BUCK1, applies the cpuclk child
 * dividers of that row, then relocks APLL. Returns 0 on success; on any
 * failure the clocks are left exactly as the bootloader left them.
 * Also safe to call for the side-effect diagnostic with mhz == 0 (probe
 * only, no writes).
 */
int rk3568_cpu_boost(unsigned int mhz);

#endif /* FREEWEBCAM_CPU_BOOST_H */
