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
 *
 * ---------------------------------------------------------------------------
 *
 * iperf3 client for embedded systems (FreeRTOS + lwIP)
 *
 * Standard iperf3 protocol compatible (esnet/iperf3).
 * Client only — TCP/UDP, forward/reverse, bandwidth limiting.
 *
 * Portability:
 *   - RTOS:      FreeRTOS (task creation, delays, yielding)
 *   - TCP/IP:    lwIP BSD socket API
 *   - DNS:       lwIP netdb (optional, requires LWIP_DNS)
 *
 * Platform abstraction — provide implementations for:
 *   - iperf3_platform_get_time_us()   microsecond counter (monotonic)
 *   - iperf3_platform_delay_us(n)     busy-wait delay in microseconds
 *
 * Optional overrides (define before including this file):
 *   - IPERF3_PRINTF(fmt, ...)         console output (default: printf)
 *   - IPERF3_LOG_ERR(fmt, ...)        error log (default: IPERF3_PRINTF)
 *   - IPERF3_LOG_WARN(fmt, ...)       warning log (default: IPERF3_PRINTF)
 *   - IPERF3_LOG_INFO(fmt, ...)       info log (default: no-op in release)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* port: <errno.h> removed - this freestanding image has no newlib reent
 * behind it; lwip/errno.h (via iperf3_port.h) provides the constants and
 * the errno variable instead, like the rest of the lwIP stack here. */

/* port: FreeRTOS glue removed; the OS/timing surface lives in
 * port/adapters/lwip/iperf3_port.h (ThreadX SMP via CMSIS-RTOS2). */
#include "iperf3_port.h"

/* lwIP */
#include "lwip/sockets.h"
#include "lwip/def.h"
#if LWIP_DNS
#include "lwip/netdb.h"
#include "lwip/tcpip.h"
#endif

#include "iperf3_embedded.h"

/* ---------------------------------------------------------------
 * Platform abstraction — must be provided by the integrator
 * --------------------------------------------------------------- */

#ifndef IPERF3_PRINTF
#define IPERF3_PRINTF(fmt, ...)   printf(fmt, ##__VA_ARGS__)
#endif

#ifndef IPERF3_LOG_ERR
#define IPERF3_LOG_ERR(fmt, ...)  IPERF3_PRINTF("[iperf3] ERROR: " fmt "\r\n", ##__VA_ARGS__)
#endif

#ifndef IPERF3_LOG_WARN
#define IPERF3_LOG_WARN(fmt, ...) IPERF3_PRINTF("[iperf3] WARN:  " fmt "\r\n", ##__VA_ARGS__)
#endif

#ifndef IPERF3_LOG_INFO
#define IPERF3_LOG_INFO(fmt, ...) /* no-op by default */
#endif

/*
 * lwIP ioctl wrapper — some lwIP ports use ioctlsocket(), others use lwip_ioctl().
 * Define IPERF3_IOCTLSOCKET to override.
 */
#ifndef IPERF3_IOCTLSOCKET
#define IPERF3_IOCTLSOCKET(fd, cmd, argp)  ioctlsocket(fd, cmd, argp)
#endif

/* ---------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------- */

#define IPERF3_TCP_PORT          5201
#define IPERF3_DEFAULT_TIME      10
#define IPERF3_DEFAULT_INTERVAL  1
/* N7 gigabit tier (deviation from the vendored 4096, PROVENANCE table):
 * one 32 KB send amortizes the socket/netconn round trip over ~22 segments
 * instead of ~3 - at the R0 baseline the per-call overhead dominated the
 * whole uplink. tcp_write chunks it into MSS-sized segments internally. */
#define IPERF3_TCP_BUF_SIZE      (32 * 1024)
#define IPERF3_UDP_BUF_SIZE      1460
#define IPERF3_JSON_MAX          512
#define IPERF3_RECV_BUF_SIZE     2048
#define IPERF3_COOKIE_SIZE       37    /* Standard iperf3 COOKIE_SIZE */
#define IPERF3_TASK_PRIORITY     4
#define IPERF3_TASK_STACK        4096
#define IPERF3_STOP_TIMEOUT_MS   5000
#define IPERF3_SOCKET_TIMEOUT_S  2

/* iperf3 protocol state-machine constants (esnet/iperf3) */
#define IPERF3_TEST_END          0x04
#define IPERF3_PARAM_COOKIE      0x09
#define IPERF3_PARAM_CLIENT_VER  0x0a

/* esnet iperf3 control-connection states (src/iperf_api.h) */
#define IPERF3_STATE_TEST_START       0x01
#define IPERF3_STATE_TEST_END         0x04
#define IPERF3_STATE_CREATE_STREAMS   0x0a
#define IPERF3_PARAM_STREAM_ID   0x01
#define IPERF3_EXCHANGE_RESULTS  0x0d
#define IPERF3_DISPLAY_RESULTS   0x0e
#define IPERF3_IPERF_DONE        0x10

/* ---------------------------------------------------------------
 * Global state (singleton — one test at a time)
 * --------------------------------------------------------------- */

static iperf3_client_cfg_t  s_cfg;
static volatile bool        s_finish;
static bool                 s_running;
static TaskHandle_t         s_task_hdl;
static uint64_t             s_bytes_total;
static uint64_t             s_bytes_last;

/* ---------------------------------------------------------------
 * send_all / recv_all — handle TCP fragmentation / coalescing
 * --------------------------------------------------------------- */

static int iperf3_send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0 && !s_finish) {
        int n = send(fd, p, (int)len, 0);
        if (n <= 0) {
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int iperf3_recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len > 0 && !s_finish) {
        int n = recv(fd, p, (int)len, 0);
        if (n <= 0) {
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

/*
 * Drain residual bytes from TCP receive buffer.
 * Uses FIONREAD to detect stale bytes left by a previous protocol phase
 * (e.g. partial server ack read in iperf3_exchange_params Step 4).
 * Must be called before any phase that reads fixed-size markers,
 * to prevent stream misalignment.
 */
static void iperf3_tcp_drain(int fd)
{
    uint8_t drain_buf[64];
    unsigned long avail = 0;

    if (IPERF3_IOCTLSOCKET(fd, FIONREAD, &avail) == 0 && avail > 0) {
        IPERF3_LOG_WARN("draining %lu residual byte(s)", avail);
        while (avail > 0) {
            size_t to_read = (avail > sizeof(drain_buf)) ? sizeof(drain_buf) : (size_t)avail;
            int n = recv(fd, drain_buf, (int)to_read, 0);
            if (n <= 0) break;
            avail -= (unsigned long)n;
        }
    }
}

/* ---------------------------------------------------------------
 * Cookie generation
 *
 * Standard iperf3 cookie format:
 *   "<hostname>.<timestamp>.<pid>"
 * --------------------------------------------------------------- */

static void iperf3_gen_cookie(char *buf, size_t len)
{
    /* Use task tick + task handle as unique identifiers. */
    uint32_t tick = (uint32_t)xTaskGetTickCount();
    uint32_t pid  = (s_task_hdl)
                    ? ((uint32_t)(uintptr_t)s_task_hdl & 0xFFFF)
                    : 1;

    snprintf(buf, len, "iperf3-embedded.%lu.%lu",
             (unsigned long)tick, (unsigned long)pid);
}

/* ---------------------------------------------------------------
 * TCP client connect
 * --------------------------------------------------------------- */

static int iperf3_client_connect(uint32_t ip, uint16_t port)
{
    int fd;
    struct sockaddr_in addr;
    struct timeval tv;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        IPERF3_LOG_WARN("socket create failed, errno %d", errno);
        return -1;
    }

    /* Receive timeout */
    tv.tv_sec  = IPERF3_SOCKET_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = ip;

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        IPERF3_LOG_WARN("connect to %lu.%lu.%lu.%lu:%u failed, errno %d",
                        (ip >> 0) & 0xFF,  (ip >> 8) & 0xFF,
                        (ip >> 16) & 0xFF, (ip >> 24) & 0xFF,
                        port, errno);
        close(fd);
        return -1;
    }

    return fd;
}

/* ---------------------------------------------------------------
 * Parameter exchange (standard iperf3 length-prefixed JSON)
 *
 * Standard iperf3 sends parameters as:
 *   [4-byte length (network byte order)] [JSON string \0]
 * --------------------------------------------------------------- */

static int iperf3_send_json(int fd, const char *json)
{
    uint32_t len_host = (uint32_t)(strlen(json) + 1); /* include null terminator */
    uint32_t len_net  = htonl(len_host);

    if (iperf3_send_all(fd, &len_net, sizeof(len_net)) < 0) return -1;
    if (iperf3_send_all(fd, json, len_host) < 0) return -1;

    return 0;
}

static int iperf3_recv_json(int fd, char *buf, size_t buf_size)
{
    uint32_t len_net;
    uint32_t len_host;

    if (iperf3_recv_all(fd, &len_net, sizeof(len_net)) < 0) return -1;

    len_host = ntohl(len_net);

    if (len_host == 0 || len_host >= buf_size) {
        IPERF3_LOG_WARN("invalid JSON length %u", len_host);
        return -1;
    }

    if (iperf3_recv_all(fd, buf, len_host) < 0) return -1;

    buf[len_host] = '\0';
    return 0;
}

static int iperf3_exchange_params(int ctrl_fd, const char *cookie)
{
    uint8_t ack_buf[8];
    char json[IPERF3_JSON_MAX];
    int ret;

    /*
     * Standard iperf3 control connection handshake:
     *   1. Client sends cookie as null-terminated string (padded to 37 bytes)
     *   2. Server replies with 1 byte (\x09 = PARAM_COOKIE acknowledgment)
     *   3. Client sends [4-byte length BE] [JSON\0]
     *   4. Server replies with parameter confirmations / stream ID
     */

    /* Step 1: Send cookie padded to 37 bytes (standard iperf3 COOKIE_SIZE) */
    {
        char cookie_buf[IPERF3_COOKIE_SIZE];
        memset(cookie_buf, 0, sizeof(cookie_buf));
        strncpy(cookie_buf, cookie, sizeof(cookie_buf) - 1);
        cookie_buf[sizeof(cookie_buf) - 1] = '\0';
        IPERF3_LOG_INFO("sending cookie: %s (37 bytes padded)", cookie);
        if (iperf3_send_all(ctrl_fd, cookie_buf, sizeof(cookie_buf)) < 0) {
            IPERF3_LOG_WARN("failed to send cookie");
            return -1;
        }
    }

    /* Step 2: Receive 1-byte cookie acknowledgment (PARAM_COOKIE = 0x09) */
    ret = iperf3_recv_all(ctrl_fd, ack_buf, 1);
    if (ret < 0) {
        IPERF3_LOG_WARN("failed to receive cookie ack");
        return -1;
    }
    if (ack_buf[0] != IPERF3_PARAM_COOKIE) {
        IPERF3_LOG_WARN("unexpected cookie ack 0x%02x (expected 0x%02x)",
                        ack_buf[0], IPERF3_PARAM_COOKIE);
    }

    /* Step 3: Build and send JSON params.
     * Uses "udp":true or "tcp":true to signal the protocol. */
    {
        const char *rev_str = s_cfg.reverse ? ",\"reverse\":true" : "";

        if (s_cfg.udp) {
            ret = snprintf(json, sizeof(json),
                "{"
                "\"udp\":true,"
                "\"omit\":0,"
                "\"time\":%lu,"
                "\"num\":0,"
                "\"blockcount\":0,"
                "\"parallel\":1"
                "%s"
                ",\"len\":%u,"
                "\"bandwidth\":%u,"
                "\"pacing_timer\":1000,"
                "\"client_version\":\"3.19.1\""
                "}",
                (unsigned long)s_cfg.time_sec,
                rev_str,
                (unsigned)IPERF3_UDP_BUF_SIZE,
                (unsigned)(s_cfg.bw_limit_kbps > 0 ? s_cfg.bw_limit_kbps * 1000 : 0));
        } else {
            ret = snprintf(json, sizeof(json),
                "{"
                "\"tcp\":true,"
                "\"omit\":0,"
                "\"time\":%lu,"
                "\"num\":0,"
                "\"blockcount\":0,"
                "\"parallel\":1"
                "%s"
                ",\"len\":%u,"
                "\"pacing_timer\":1000,"
                "\"client_version\":\"3.19.1\""
                "}",
                (unsigned long)s_cfg.time_sec,
                rev_str,
                (unsigned)IPERF3_TCP_BUF_SIZE);
        }
    }
    if (ret <= 0 || (size_t)ret >= sizeof(json)) {
        IPERF3_LOG_WARN("params JSON too long");
        return -1;
    }

    IPERF3_LOG_INFO("sending params: %s", json);
    if (iperf3_send_json(ctrl_fd, json) < 0) {
        IPERF3_LOG_WARN("failed to send params");
        return -1;
    }

    /*
     * Step 4: Receive server parameter confirmations.
     *
     * Standard iperf3 server responds with per-parameter confirmations:
     *   PARAM_CLIENT_VERSION (0x0a)                   — 1 byte
     *   PARAM_STREAM_ID (0x01) + 4-byte stream ID     — 5 bytes per stream
     *
     * A single recv() with SO_RCVTIMEO is sufficient.
     * Then drain remaining bytes to guarantee a clean buffer
     * before the results phase.
     */
    {
        int n = recv(ctrl_fd, ack_buf, sizeof(ack_buf), 0);
        if (n > 0) {
            IPERF3_LOG_INFO("server ack %d byte(s): %02x %02x %02x %02x %02x %02x",
                            n, ack_buf[0],
                            n > 1 ? ack_buf[1] : 0,
                            n > 2 ? ack_buf[2] : 0,
                            n > 3 ? ack_buf[3] : 0,
                            n > 4 ? ack_buf[4] : 0,
                            n > 5 ? ack_buf[5] : 0);
        } else {
            IPERF3_LOG_INFO("server ack timeout (ok)");
        }

        iperf3_tcp_drain(ctrl_fd);
    }

    return 0;
}

/* ---------------------------------------------------------------
 * Data connection (TCP only — UDP uses the control socket)
 * --------------------------------------------------------------- */

static int iperf3_data_connect(uint32_t ip, uint16_t port, const char *cookie)
{
    int fd;
    struct sockaddr_in addr;
    struct timeval tv;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        IPERF3_LOG_WARN("data socket create failed, errno %d", errno);
        return -1;
    }

    tv.tv_sec  = IPERF3_SOCKET_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = ip;

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        IPERF3_LOG_WARN("data connect failed, errno %d", errno);
        close(fd);
        return -1;
    }

    /* Send cookie padded to 37 bytes for server to match */
    {
        char cookie_buf[IPERF3_COOKIE_SIZE];
        memset(cookie_buf, 0, sizeof(cookie_buf));
        strncpy(cookie_buf, cookie, sizeof(cookie_buf));
        if (iperf3_send_all(fd, cookie_buf, sizeof(cookie_buf)) < 0) {
            IPERF3_LOG_WARN("data socket cookie send failed");
            close(fd);
            return -1;
        }
    }

    return fd;
}

/* ---------------------------------------------------------------
 * Report helper
 * --------------------------------------------------------------- */

/* port: this image is built -mgeneral-regs-only (no FP registers), so the
 * bandwidth reports below compute Mbits/sec in integer arithmetic and print
 * whole.fraction instead of %.2f. */
static uint64_t iperf3_kbps(uint64_t bytes, uint32_t sec)
{
    if (sec == 0) {
        sec = 1;
    }
    return bytes * 8U / ((uint64_t)sec * 1000U);
}

static void iperf3_report(uint32_t elapsed_sec)
{
    uint64_t bytes = s_bytes_total - s_bytes_last;
    uint64_t kbps = iperf3_kbps(bytes, s_cfg.interval_sec);

    IPERF3_PRINTF("%4lu-%4lu sec       %lu.%02lu Mbits/sec\r\n",
                  (unsigned long)(elapsed_sec - s_cfg.interval_sec),
                  (unsigned long)elapsed_sec,
                  (unsigned long)(kbps / 1000U),
                  (unsigned long)((kbps % 1000U) / 10U));

    s_bytes_last = s_bytes_total;
}

/* ---------------------------------------------------------------
 * TCP send (forward mode)
 * --------------------------------------------------------------- */

static void iperf3_tcp_send(int data_fd)
{
    uint8_t *buf;
    size_t buf_size = IPERF3_TCP_BUF_SIZE;
    uint32_t elapsed = 0;
    uint32_t total_ms = s_cfg.time_sec * 1000;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_report = start_tick;

    buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        IPERF3_LOG_WARN("tcp_send malloc failed");
        return;
    }
    memset(buf, 0, buf_size);

    IPERF3_LOG_INFO("TCP forward start, %lu seconds", (unsigned long)s_cfg.time_sec);

    while (!s_finish) {
        int n = send(data_fd, buf, (int)buf_size, 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                taskYIELD();
                continue;
            }
            IPERF3_LOG_WARN("tcp send error, errno %d", errno);
            break;
        }
        s_bytes_total += (uint64_t)n;

        /* Check elapsed time */
        TickType_t now = xTaskGetTickCount();
        uint32_t ms = (uint32_t)((now - start_tick) * portTICK_PERIOD_MS);

        if (ms - (uint32_t)((last_report - start_tick) * portTICK_PERIOD_MS)
                >= s_cfg.interval_sec * 1000) {
            elapsed = ms / 1000;
            if (elapsed > s_cfg.time_sec) elapsed = s_cfg.time_sec;
            iperf3_report(elapsed);
            last_report = now;
        }

        if (ms >= total_ms) {
            break;
        }
    }

    /* Final report */
    elapsed = s_cfg.time_sec;
    iperf3_report(elapsed);

    free(buf);
}

/* ---------------------------------------------------------------
 * TCP recv (reverse mode)
 * --------------------------------------------------------------- */

static void iperf3_tcp_recv(int data_fd, int ctrl_fd)
{
    uint8_t *buf;
    size_t buf_size = IPERF3_TCP_BUF_SIZE;
    uint32_t elapsed = 0;
    uint32_t total_ms = s_cfg.time_sec * 1000;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_report = start_tick;

    buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        IPERF3_LOG_WARN("tcp_recv malloc failed");
        return;
    }

    /* esnet iperf3 reverse-mode state exchange: after params the server
     * sends CREATE_STREAMS (0x0a) and then waits for the client's
     * TEST_START (0x01) on the control socket before pushing downlink
     * data.  The old code never signalled TEST_START, so the server
     * stayed silent and this loop span forever (board 2026-09-30,
     * `iperf3 600 -r` vs iperf 3.21).  Consume the pending state bytes
     * (CREATE_STREAMS, the server's own TEST_START), announce ours, and
     * only then start receiving. */
    {
        uint8_t state = IPERF3_STATE_TEST_START;
        int i;

        if (iperf3_send_all(ctrl_fd, &state, sizeof(state)) == 0) {
            for (i = 0; i < 4; i++) {
                if (iperf3_recv_all(ctrl_fd, &state, sizeof(state)) < 0) {
                    break; /* timeout: proceed without the server ack */
                }
                if (state == IPERF3_STATE_TEST_START) {
                    break; /* server confirmed: data may flow */
                }
                /* CREATE_STREAMS and other interim states: consumed */
            }
        }
    }

    IPERF3_LOG_INFO("TCP reverse start, %lu seconds", (unsigned long)s_cfg.time_sec);

    while (!s_finish) {
        TickType_t now = xTaskGetTickCount();
        uint32_t ms = (uint32_t)((now - start_tick) * portTICK_PERIOD_MS);

        /* the elapsed-time check lives at the loop head: the old code
         * only checked it after a successful recv, so an idle reverse
         * server (EAGAIN on every recv) looped forever */
        if (ms >= total_ms) {
            break;
        }

        int n = recv(data_fd, buf, (int)buf_size, 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                taskYIELD();
                continue;
            }
            if (n == 0) {
                IPERF3_LOG_INFO("server closed data connection");
            } else {
                IPERF3_LOG_WARN("tcp recv error, errno %d", errno);
            }
            break;
        }
        s_bytes_total += (uint64_t)n;

        now = xTaskGetTickCount();
        ms = (uint32_t)((now - start_tick) * portTICK_PERIOD_MS);

        if (ms - (uint32_t)((last_report - start_tick) * portTICK_PERIOD_MS)
                >= s_cfg.interval_sec * 1000) {
            elapsed = ms / 1000;
            if (elapsed > s_cfg.time_sec) elapsed = s_cfg.time_sec;
            iperf3_report(elapsed);
            last_report = now;
        }
    }

    elapsed = s_cfg.time_sec;
    iperf3_report(elapsed);

    free(buf);
}

/* ---------------------------------------------------------------
 * UDP send (forward)
 * --------------------------------------------------------------- */

static void iperf3_udp_send(void)
{
    int fd;
    struct sockaddr_in addr;
    struct timeval tv;
    uint8_t *buf;
    size_t buf_size = IPERF3_UDP_BUF_SIZE;
    uint32_t pkt_cnt = 0;
    uint32_t elapsed = 0;
    uint32_t total_ms = s_cfg.time_sec * 1000;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_report = start_tick;
    int period_us = 0;
    int delay_us = 0;
    int64_t prev_time = 0;
    int64_t send_time = 0;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        IPERF3_LOG_WARN("udp socket create failed, errno %d", errno);
        return;
    }

    tv.tv_sec  = IPERF3_SOCKET_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(s_cfg.port);
    addr.sin_addr.s_addr = s_cfg.dest_ip;

    buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        IPERF3_LOG_WARN("udp_send malloc failed");
        close(fd);
        return;
    }
    memset(buf, 0, buf_size);

    if (s_cfg.bw_limit_kbps > 0) {
        /* period_us = bytes * 8 * 1000 / bw_lim(Kbps)
         *  -> time in microseconds to send one packet at target rate */
        period_us = (int)((int64_t)buf_size * 8 * 1000 / s_cfg.bw_limit_kbps);
    }

    IPERF3_LOG_INFO("UDP forward start, %lu seconds", (unsigned long)s_cfg.time_sec);

    while (!s_finish) {
        TickType_t now_ticks;
        uint32_t ms;

        /* Bandwidth pacing — use microsecond timer */
        if (period_us > 0) {
            send_time = (int64_t)iperf3_platform_get_time_us();
            if (pkt_cnt > 0) {
                delay_us += period_us + (int32_t)(prev_time - send_time);
            }
            prev_time = send_time;
        }

        /* Build iperf3 UDP datagram (esnet/iperf3 format):
         *   [tv_sec(4) BE] [tv_usec(4) BE] [pkt_cnt(4) BE] [payload]
         */
        {
            uint64_t now_us = iperf3_platform_get_time_us();
            uint32_t *p = (uint32_t *)buf;
            p[0] = htonl((uint32_t)(now_us / 1000000));    /* tv_sec */
            p[1] = htonl((uint32_t)(now_us % 1000000));    /* tv_usec */
            p[2] = htonl(pkt_cnt + 1);                     /* pkt_cnt from 1 */
        }

        int n = sendto(fd, buf, (int)buf_size, 0,
                       (struct sockaddr *)&addr, sizeof(addr));
        if (n > 0) {
            /* Exclude 12-byte iperf3 UDP header for accurate payload count */
            s_bytes_total += (uint64_t)((n > 12) ? (n - 12) : n);
            pkt_cnt++;
        } else if (errno != ENOMEM) {
            IPERF3_LOG_WARN("udp send error, errno %d", errno);
        }

        /* Apply delay for bandwidth limiting */
        if (delay_us > 0) {
            int delay_ms = delay_us / 1000;
            if (delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
            } else {
                iperf3_platform_delay_us((uint32_t)delay_us);
            }
        }

        now_ticks = xTaskGetTickCount();
        ms = (uint32_t)((now_ticks - start_tick) * portTICK_PERIOD_MS);

        if (ms - (uint32_t)((last_report - start_tick) * portTICK_PERIOD_MS)
                >= s_cfg.interval_sec * 1000) {
            elapsed = ms / 1000;
            if (elapsed > s_cfg.time_sec) elapsed = s_cfg.time_sec;
            iperf3_report(elapsed);
            last_report = now_ticks;
        }

        if (ms >= total_ms) {
            break;
        }
    }

    elapsed = s_cfg.time_sec;
    iperf3_report(elapsed);

    free(buf);
    close(fd);
}

/* ---------------------------------------------------------------
 * UDP recv (reverse)
 * --------------------------------------------------------------- */

static void iperf3_udp_recv(void)
{
    int fd;
    struct sockaddr_in addr;
    struct timeval tv;
    uint8_t *buf;
    size_t buf_size = IPERF3_UDP_BUF_SIZE;
    uint32_t elapsed = 0;
    uint32_t total_ms = s_cfg.time_sec * 1000;
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_report = start_tick;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        IPERF3_LOG_WARN("udp recv socket failed, errno %d", errno);
        return;
    }

    tv.tv_sec  = IPERF3_SOCKET_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(s_cfg.port);
    addr.sin_addr.s_addr = 0; /* bind to any */

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        IPERF3_LOG_WARN("udp recv bind failed, errno %d", errno);
        close(fd);
        return;
    }

    buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        IPERF3_LOG_WARN("udp_recv malloc failed");
        close(fd);
        return;
    }

    IPERF3_LOG_INFO("UDP reverse start, %lu seconds", (unsigned long)s_cfg.time_sec);

    /*
     * Send UDP probe packet so the server learns our address/port.
     * Standard iperf3 uses UDP_CONNECT_MSG = htonl(0x36373839),
     * which is "9876" on the wire (0x39 0x38 0x37 0x36).
     * Without this probe, the server doesn't know where to send data.
     */
    {
        struct sockaddr_in srv_addr;
        srv_addr.sin_family      = AF_INET;
        srv_addr.sin_port        = htons(s_cfg.port);
        srv_addr.sin_addr.s_addr = s_cfg.dest_ip;
        uint32_t probe = htonl(0x36373839);
        sendto(fd, &probe, sizeof(probe), 0,
               (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    }

    while (!s_finish) {
        TickType_t now = xTaskGetTickCount();
        uint32_t ms = (uint32_t)((now - start_tick) * portTICK_PERIOD_MS);

        /* same head-of-loop timeout rule as iperf3_tcp_recv: EAGAIN must
         * not skip the elapsed-time check */
        if (ms >= total_ms) {
            break;
        }

        socklen_t addr_len = sizeof(addr);
        int n = recvfrom(fd, buf, (int)buf_size, 0,
                         (struct sockaddr *)&addr, &addr_len);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                taskYIELD();
                continue;
            }
            IPERF3_LOG_WARN("udp recv error, errno %d", errno);
            break;
        }
        /* Exclude 12-byte iperf3 UDP header for accurate byte count */
        s_bytes_total += (uint64_t)((n > 12) ? (n - 12) : n);

        now = xTaskGetTickCount();
        ms = (uint32_t)((now - start_tick) * portTICK_PERIOD_MS);

        if (ms - (uint32_t)((last_report - start_tick) * portTICK_PERIOD_MS)
                >= s_cfg.interval_sec * 1000) {
            elapsed = ms / 1000;
            if (elapsed > s_cfg.time_sec) elapsed = s_cfg.time_sec;
            iperf3_report(elapsed);
            last_report = now;
        }

        if (ms >= total_ms) {
            break;
        }
    }

    elapsed = s_cfg.time_sec;
    iperf3_report(elapsed);

    free(buf);
    close(fd);
}

/* ---------------------------------------------------------------
 * Read results from control connection
 * --------------------------------------------------------------- */

static int iperf3_read_results(int ctrl_fd)
{
    uint8_t marker;
    uint32_t len_net;
    uint32_t len_host;
    char buf[IPERF3_RECV_BUF_SIZE];

    /*
     * Standard iperf3 end-of-test state machine (esnet/iperf3):
     *   1. Client sends TEST_END         (0x04)
     *   2. Server sends EXCHANGE_RESULTS (0x0d)
     *   3. Client sends [4B len BE][JSON] results
     *   4. Server sends [4B len BE][JSON] results
     *      (typically appends DISPLAY_RESULTS 0x0e in same TCP segment)
     *   5. Client reads DISPLAY_RESULTS  (0x0e) from buffer
     *   6. Client sends IPERF_DONE       (0x10)
     */

    /* Drain stale bytes before starting the results exchange. */
    iperf3_tcp_drain(ctrl_fd);

    /* Step 1: Send TEST_END */
    marker = IPERF3_TEST_END;
    if (iperf3_send_all(ctrl_fd, &marker, 1) < 0) {
        IPERF3_LOG_WARN("failed to send TEST_END");
        return -1;
    }

    /* Step 2: Receive EXCHANGE_RESULTS from server */
    if (iperf3_recv_all(ctrl_fd, &marker, 1) < 0) {
        IPERF3_LOG_WARN("no EXCHANGE_RESULTS from server");
        return -1;
    }
    if (marker != IPERF3_EXCHANGE_RESULTS) {
        IPERF3_LOG_WARN("unexpected EXCHANGE_RESULTS marker 0x%02x "
                        "(expected 0x%02x), continuing anyway",
                        marker, IPERF3_EXCHANGE_RESULTS);
    }
    IPERF3_LOG_INFO("EXCHANGE_RESULTS (0x%02x)", marker);

    /* Step 3: Send client results — [4B len BE][JSON\0] */
    {
        char stats[IPERF3_JSON_MAX];
        snprintf(stats, sizeof(stats),
            "{"
            "\"cpu_util_total\":0,"
            "\"cpu_util_user\":0,"
            "\"cpu_util_system\":0,"
            "\"sender_has_retransmits\":0,"
            "\"streams\":[{"
                "\"id\":1,"
                "\"bytes\":%llu,"
                "\"retransmits\":0,"
                "\"jitter\":0,"
                "\"errors\":0,"
                "\"packets\":0"
            "}]"
            "}",
            (unsigned long long)s_bytes_total);
        IPERF3_LOG_INFO("sending client results: %s", stats);
        if (iperf3_send_json(ctrl_fd, stats) < 0) {
            IPERF3_LOG_WARN("failed to send client results");
            return -1;
        }
    }

    /*
     * Step 4: Receive server results — [4B len BE][JSON\0].
     *
     * The server appends DISPLAY_RESULTS (0x0e) after the JSON payload,
     * often in the same TCP segment.  iperf3_recv_all reads exactly
     * 'len_host' bytes, so the 0x0e byte remains in the receive buffer
     * and is consumed by Step 5 below.
     */
    if (iperf3_recv_all(ctrl_fd, &len_net, sizeof(len_net)) < 0) {
        IPERF3_LOG_WARN("failed to read result length");
        return -1;
    }

    len_host = ntohl(len_net);

    if (len_host == 0 || len_host >= sizeof(buf)) {
        IPERF3_LOG_WARN("result length %u out of range (max %u)",
                        len_host, (unsigned)(sizeof(buf) - 1));
        return -1;
    }

    if (iperf3_recv_all(ctrl_fd, buf, len_host) < 0) {
        IPERF3_LOG_WARN("failed to read result data");
        return -1;
    }

    buf[len_host] = '\0';
    IPERF3_LOG_INFO("server result: %s", buf);

    /* Print server bandwidth report */
    {
        char *bytes_str = strstr(buf, "\"bytes\":");
        if (bytes_str) {
            unsigned long server_bytes = strtoul(bytes_str + 8, NULL, 10);
            uint64_t server_kbps = iperf3_kbps((uint64_t)server_bytes,
                                               s_cfg.time_sec);
            IPERF3_PRINTF("\r\nServer report: %lu.%02lu Mbits/sec"
                          " (%lu bytes in %lu sec)\r\n",
                          (unsigned long)(server_kbps / 1000U),
                          (unsigned long)((server_kbps % 1000U) / 10U),
                          server_bytes,
                          (unsigned long)s_cfg.time_sec);
        }
    }

    /*
     * Step 5: Receive DISPLAY_RESULTS from server.
     *
     * This byte is appended to the JSON payload (same TCP segment as the
     * server results).  Because iperf3_recv_all in Step 4 reads exactly
     * len_host bytes, the 0x0e byte remains in the TCP receive buffer.
     */
    if (iperf3_recv_all(ctrl_fd, &marker, 1) < 0) {
        IPERF3_LOG_WARN("no DISPLAY_RESULTS from server");
        return -1;
    }
    if (marker != IPERF3_DISPLAY_RESULTS) {
        IPERF3_LOG_WARN("unexpected DISPLAY_RESULTS marker 0x%02x "
                        "(expected 0x%02x)",
                        marker, IPERF3_DISPLAY_RESULTS);
    }
    IPERF3_LOG_INFO("DISPLAY_RESULTS (0x%02x)", marker);

    /* Step 6: Send IPERF_DONE */
    marker = IPERF3_IPERF_DONE;
    if (iperf3_send_all(ctrl_fd, &marker, 1) < 0) {
        IPERF3_LOG_WARN("failed to send IPERF_DONE");
        return -1;
    }

    return 0;
}

/* ---------------------------------------------------------------
 * Main task
 * --------------------------------------------------------------- */

static void iperf3_task(void *arg)
{
    int ctrl_fd = -1;
    int data_fd = -1;
    char cookie[IPERF3_COOKIE_SIZE];
    (void)arg;

    s_finish       = false;
    s_bytes_total  = 0;
    s_bytes_last   = 0;

    /* Generate cookie */
    iperf3_gen_cookie(cookie, sizeof(cookie));
    IPERF3_LOG_INFO("cookie=%s", cookie);

    /* 1. Connect control */
    ctrl_fd = iperf3_client_connect(s_cfg.dest_ip, s_cfg.port);
    if (ctrl_fd < 0) {
        goto exit;
    }
    IPERF3_LOG_INFO("control connected");

    /* 2. Parameter exchange */
    if (iperf3_exchange_params(ctrl_fd, cookie) < 0) {
        goto exit;
    }

    /* 3. Run throughput test */
    IPERF3_PRINTF("\r\n%16s %s\r\n", "Interval", "Bandwidth");

    if (s_cfg.udp) {
        if (s_cfg.reverse) {
            iperf3_udp_recv();
        } else {
            iperf3_udp_send();
        }
    } else {
        data_fd = iperf3_data_connect(s_cfg.dest_ip, s_cfg.port, cookie);
        if (data_fd < 0) {
            goto exit;
        }
        if (s_cfg.reverse) {
            iperf3_tcp_recv(data_fd, ctrl_fd);
        } else {
            iperf3_tcp_send(data_fd);
        }
    }

    /* 4. Read server results */
    iperf3_read_results(ctrl_fd);

    IPERF3_PRINTF("iperf3 done: total %lu.%02lu Mbits/sec (%llu bytes in %lu sec)\r\n",
                  (unsigned long)(iperf3_kbps(s_bytes_total, s_cfg.time_sec) / 1000U),
                  (unsigned long)((iperf3_kbps(s_bytes_total, s_cfg.time_sec) % 1000U) / 10U),
                  (unsigned long long)s_bytes_total,
                  (unsigned long)s_cfg.time_sec);

exit:
    if (data_fd >= 0) {
        shutdown(data_fd, 0);
        close(data_fd);
    }
    if (ctrl_fd >= 0) {
        shutdown(ctrl_fd, 0);
        close(ctrl_fd);
    }

    s_finish   = true;
    s_running  = false;
    s_task_hdl = NULL;
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */

int iperf3_client_start(const iperf3_client_cfg_t *cfg)
{
    BaseType_t ret;

    if (!cfg) return -1;

    if (s_running) {
        IPERF3_LOG_WARN("already running");
        return -1;
    }

    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    s_running  = true;
    s_finish   = false;
    s_task_hdl = NULL;

    ret = xTaskCreate(iperf3_task, "iperf3c", IPERF3_TASK_STACK,
                      NULL, IPERF3_TASK_PRIORITY, &s_task_hdl);
    if (ret != pdPASS) {
        IPERF3_LOG_WARN("task create failed");
        s_running = false;
        return -1;
    }

    return 0;
}

int iperf3_client_stop(void)
{
    if (!s_running) {
        IPERF3_LOG_WARN("not running");
        return 0;
    }

    s_finish = true;

    int timeout = IPERF3_STOP_TIMEOUT_MS;
    while (s_running && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout -= 100;
    }

    if (s_running) {
        IPERF3_LOG_WARN("stop timeout, force");
        s_running  = false;
        s_task_hdl = NULL;
    }

    return 0;
}

bool iperf3_client_is_running(void)
{
    return s_running;
}

/* ---------------------------------------------------------------
 * DNS resolution (optional — requires LWIP_DNS)
 * --------------------------------------------------------------- */

#if LWIP_DNS

struct iperf3_dns_param {
    iperf3_client_cfg_t cfg;
    char                hostname[256];
};

static void iperf3_dns_found(const char *name, const ip_addr_t *ipaddr,
                             void *callback_arg)
{
    struct iperf3_dns_param *param = (struct iperf3_dns_param *)callback_arg;

    if (ipaddr != NULL) {
        param->cfg.dest_ip = ip_2_ip4(ipaddr)->addr;
        IPERF3_LOG_INFO("DNS resolved %s -> %lu.%lu.%lu.%lu",
                        name,
                        (unsigned long)(param->cfg.dest_ip >> 0) & 0xFF,
                        (unsigned long)(param->cfg.dest_ip >> 8) & 0xFF,
                        (unsigned long)(param->cfg.dest_ip >> 16) & 0xFF,
                        (unsigned long)(param->cfg.dest_ip >> 24) & 0xFF);

        IPERF3_PRINTF("iperf3: mode=%s-%s server=%lu.%lu.%lu.%lu:%u"
                      " time=%lu interval=%lu bw=%dK\r\n",
                      param->cfg.udp ? "udp" : "tcp",
                      param->cfg.reverse ? "reverse" : "forward",
                      (unsigned long)(param->cfg.dest_ip >> 0) & 0xFF,
                      (unsigned long)(param->cfg.dest_ip >> 8) & 0xFF,
                      (unsigned long)(param->cfg.dest_ip >> 16) & 0xFF,
                      (unsigned long)(param->cfg.dest_ip >> 24) & 0xFF,
                      param->cfg.port,
                      (unsigned long)param->cfg.time_sec,
                      (unsigned long)param->cfg.interval_sec,
                      param->cfg.bw_limit_kbps);

        iperf3_client_start(&param->cfg);
    } else {
        IPERF3_LOG_WARN("DNS failed to resolve %s", name);
    }

    free(param);
}

static void iperf3_dns_resolve_work(void *ctx)
{
    struct iperf3_dns_param *param = (struct iperf3_dns_param *)ctx;
    ip_addr_t addr;
    err_t err;

    err = dns_gethostbyname(param->hostname, &addr, iperf3_dns_found, param);
    if (err == ERR_OK) {
        /* DNS cache hit — callback is not invoked for ERR_OK.
         * Call it manually with the resolved address. */
        iperf3_dns_found(param->hostname, &addr, param);
    } else if (err == ERR_INPROGRESS) {
        /* DNS query sent — callback will be called later */
        IPERF3_LOG_INFO("DNS query sent for %s", param->hostname);
    } else {
        IPERF3_LOG_WARN("DNS query failed for %s, err=%d", param->hostname, err);
        free(param);
    }
}

/**
 * Start an iperf3 test with DNS hostname resolution.
 *
 * Resolves @p hostname asynchronously via lwIP DNS and starts the test
 * once the address is known.  This function must be called from the
 * lwIP TCP/IP thread context (or use tcpip_callback to schedule it).
 *
 * @param cfg       Test configuration (dest_ip is ignored, filled by DNS).
 * @param hostname  Null-terminated server hostname (copied internally).
 * @return 0 if DNS resolution was started, -1 on error.
 */
int iperf3_client_start_host(const iperf3_client_cfg_t *cfg,
                             const char *hostname)
{
    struct iperf3_dns_param *dns_param;

    if (!cfg || !hostname) return -1;

    dns_param = (struct iperf3_dns_param *)malloc(sizeof(*dns_param));
    if (!dns_param) {
        IPERF3_LOG_WARN("out of memory");
        return -1;
    }

    memcpy(&dns_param->cfg, cfg, sizeof(*cfg));
    strncpy(dns_param->hostname, hostname, sizeof(dns_param->hostname) - 1);
    dns_param->hostname[sizeof(dns_param->hostname) - 1] = '\0';

    IPERF3_PRINTF("iperf3: resolving %s ...\r\n", hostname);

    if (tcpip_callback(iperf3_dns_resolve_work, dns_param) != ERR_OK) {
        IPERF3_LOG_WARN("failed to schedule DNS resolution");
        free(dns_param);
        return -1;
    }

    return 0;
}

#endif /* LWIP_DNS */
