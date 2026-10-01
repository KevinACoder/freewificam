/*
 * MIT License
 *
 * Copyright (c) 2024 iperf3-embedded contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef IPERF3_EMBEDDED_H
#define IPERF3_EMBEDDED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 * Configuration structure
 * --------------------------------------------------------------- */

typedef struct {
    uint32_t    dest_ip;        /**< Server IP in network byte order
                                     (use inet_addr() or DNS resolution). */
    uint16_t    port;           /**< Server port (default: 5201). */
    uint32_t    time_sec;       /**< Test duration in seconds (default: 10). */
    uint32_t    interval_sec;   /**< Report interval in seconds (default: 1). */
    int         bw_limit_kbps;  /**< Bandwidth limit in Kbps, 0 = unlimited. */
    uint8_t     udp     : 1;    /**< 1 = UDP mode, 0 = TCP mode. */
    uint8_t     reverse : 1;    /**< 1 = reverse direction (server → client). */
} iperf3_client_cfg_t;

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */

/**
 * @brief Start an iperf3 client test.
 *
 * Creates a FreeRTOS task that connects to the server, runs the
 * throughput test, and prints periodic bandwidth reports.
 *
 * Only one test can run at a time (singleton).
 *
 * @param cfg  Test configuration (copied internally, may be freed after return).
 * @return 0 on success, -1 if already running or task creation failed.
 */
int iperf3_client_start(const iperf3_client_cfg_t *cfg);

/**
 * @brief Stop a running iperf3 test.
 *
 * Signals the worker task to stop and waits up to
 * IPERF3_STOP_TIMEOUT_MS (default 5000 ms) for it to finish.
 * Forces cleanup if the timeout expires.
 *
 * @return 0 on success.
 */
int iperf3_client_stop(void);

/**
 * @brief Query whether a test is currently running.
 *
 * @return true if a test is in progress, false otherwise.
 */
bool iperf3_client_is_running(void);

#if LWIP_DNS
/**
 * @brief Start an iperf3 test with DNS hostname resolution.
 *
 * Resolves @p hostname asynchronously via lwIP DNS and starts the
 * test once the address is known.
 *
 * @note This function must be called from the lwIP TCP/IP thread
 *       context (or dispatched via tcpip_callback).
 *
 * @param cfg       Test configuration (@p dest_ip is ignored).
 * @param hostname  Null-terminated server hostname or IP string.
 * @return 0 if DNS resolution was started, -1 on error.
 */
int iperf3_client_start_host(const iperf3_client_cfg_t *cfg,
                             const char *hostname);
#endif /* LWIP_DNS */

/* ---------------------------------------------------------------
 * Platform abstraction — must be provided by the integrator
 * --------------------------------------------------------------- */

/**
 * @brief Monotonic microsecond counter.
 *
 * Used for UDP packet timestamps and bandwidth pacing.
 * Implementation-specific — typically a hardware timer or
 * RTOS tick scaled to microseconds.
 *
 * @return Current time in microseconds (wrapping is acceptable).
 */
uint64_t iperf3_platform_get_time_us(void);

/**
 * @brief Busy-wait microsecond delay.
 *
 * Used for fine-grained bandwidth pacing in UDP mode.
 * For delays >= 1 ms, the library uses FreeRTOS vTaskDelay internally;
 * this function is only called for sub-millisecond waits.
 *
 * @param us  Number of microseconds to busy-wait.
 */
void iperf3_platform_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* IPERF3_EMBEDDED_H */
