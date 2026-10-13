/**
 * @file    stream_case.c
 * @brief   Cases of uvc-stream-test: the UVC stream engine against a scripted device.
 * @author  zhugengyu
 * @date    14.10.2026
 * @design  harness rows - ref tests/host/ehci-iso-schedule-test/iso_case.c (the
 *          controller-in-RAM pattern, here a device-in-RAM: the test completes
 *          the engine's urbs itself, byte for byte, and asserts what the frame
 *          assembly made of them).  Expected values come from the UVC payload
 *          header semantics (UVC 1.0/1.1 section 2.4.3.4: header length,
 *          frame-id / end-of-frame / error bits) and from the board camera's
 *          descriptor ladder (the alternate-setting selection vectors).
 *
 * The device script is packet-granular: each entry becomes one isochronous
 * packet - a two-byte payload header plus payload bytes, or an empty
 * micro-frame - and the frame content the engine must produce is the
 * concatenation of a frame's payloads, asserted byte for byte.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usbh_core.h"
#include "usbh_video.h"
#include "usbh_uvc_stream.h"

/* the device model in stubs.c */
extern struct usbh_hubport g_hport;
extern uint32_t g_stub_want;
extern uint32_t g_stub_probe_refusals;
extern uint8_t g_stub_open_alt;
extern uint32_t g_stub_probe_calls;
extern int g_stub_iface_calls;
extern uint8_t g_stub_iface_alts[16];
extern uint16_t g_frame;
extern struct usbh_urb *g_pending[];
extern int g_pending_n;
void stub_device_reset(void);

#define TEST_POOL_MAX 3
#define TEST_FRAME_BUF 4096

static struct usbh_video g_video;
static uint8_t g_frame_bufs[TEST_POOL_MAX][TEST_FRAME_BUF];
static struct usbh_videoframe g_pool[TEST_POOL_MAX];

/* ------------------------------------------------------------------------
 * fixture
 * --------------------------------------------------------------------- */

static void fixture_setup(uint32_t frame_bufsize, uint32_t pool_count)
{
    stub_device_reset();
    memset(&g_video, 0, sizeof(g_video));
    g_video.hport = &g_hport;
    g_video.ctrl_intf = 0;
    g_video.data_intf = 1;
    g_video.num_of_formats = 1;
    g_video.format[0].format_type = USBH_VIDEO_FORMAT_MJPEG;
    g_video.format[0].num_of_frames = 1;
    g_video.format[0].frame[0].wWidth = 640;
    g_video.format[0].frame[0].wHeight = 480;
    g_video.format[0].frame[0].dwDefaultFrameInterval = 333333;
    g_video.num_of_intf_altsettings = 12;

    for (uint32_t i = 0; i < pool_count; i++) {
        g_pool[i].frame_buf = g_frame_bufs[i];
        g_pool[i].frame_bufsize = frame_bufsize;
        g_pool[i].frame_size = 0;
    }
    usbh_video_stream_attach(&g_video);
    usbh_video_stream_create(g_pool, pool_count);
}

/* One scripted isochronous packet: the two-byte header carries fid/eof/err. */
struct pkt {
    const uint8_t *payload;
    uint16_t len;
    uint8_t fid;
    uint8_t eof;
    uint8_t err;
};

/* Complete the oldest submitted urb with the scripted packets (the remaining
 * micro-frames of the urb come back empty). */
static void device_complete(const struct pkt *pkts, unsigned npkts)
{
    struct usbh_urb *urb = g_pending[0];
    uint32_t total = 0;

    for (uint32_t p = 0; p < urb->num_of_iso_packets; p++) {
        struct usbh_iso_frame_packet *packet = &urb->iso_packet[p];

        if ((p < npkts) && (pkts[p].len > 0)) {
            uint8_t *buf = packet->transfer_buffer;

            buf[0] = 2; /* bHeaderLength */
            buf[1] = (uint8_t)(pkts[p].fid | (pkts[p].eof << 1) | (pkts[p].err << 6));
            memcpy(buf + 2, pkts[p].payload, pkts[p].len);
            packet->actual_length = 2 + pkts[p].len;
        } else {
            packet->actual_length = 0;
        }
        total += packet->actual_length;
    }

    for (int i = 0; i < g_pending_n - 1; i++) {
        g_pending[i] = g_pending[i + 1];
    }
    g_pending_n--;
    urb->hcpriv = NULL;
    urb->actual_length = total;
    urb->complete(urb->arg, (int)total);
}

static bool wait_urbs(uint32_t baseline, uint32_t count, unsigned timeout_ms)
{
    struct uvc_stream_stats stats;

    for (unsigned waited = 0; waited < timeout_ms; waited += 2) {
        usbh_video_stream_stats(&stats);
        if (stats.urbs >= baseline + count) {
            return true;
        }
        usb_osal_msleep(2);
    }
    return false;
}

static void fill_payload(uint8_t *dst, uint16_t len, uint8_t seed)
{
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = (uint8_t)(seed + i * 7u);
    }
}

/* Dequeue one frame and compare it against expected bytes. */
static bool expect_frame(const uint8_t *want, uint32_t want_len, char *why, size_t n)
{
    struct usbh_videoframe *frame = NULL;

    if (usbh_video_stream_dequeue(&frame, 1000) != 0) {
        snprintf(why, n, "no frame arrived");
        return false;
    }
    if (frame->frame_size != want_len) {
        snprintf(why, n, "frame size %u, expected %u", (unsigned)frame->frame_size, (unsigned)want_len);
        usbh_video_stream_enqueue(frame);
        return false;
    }
    if (memcmp(frame->frame_buf, want, want_len) != 0) {
        snprintf(why, n, "frame content differs");
        usbh_video_stream_enqueue(frame);
        return false;
    }
    usbh_video_stream_enqueue(frame);
    return true;
}

static bool case_reset(char *why, size_t n, uint32_t bufsize, uint32_t pool_count, uint32_t want)
{
    usbh_video_stream_stop();
    (void)why;
    (void)n;
    fixture_setup(bufsize, pool_count);
    g_stub_want = want;
    return true;
}

/* ------------------------------------------------------------------------
 * cases
 * --------------------------------------------------------------------- */

/* The alternate setting is the smallest one whose endpoint carries the
 * bandwidth the device asks for (bytes per USB frame). */
static bool case_alt_select(char *why, size_t n)
{
    static const struct {
        uint32_t want;
        uint8_t alt;
        uint16_t mps;
    } vectors[] = {
        { 192, 1, 192 }, { 640, 4, 640 }, { 641, 5, 800 }, { 3060, 11, 3060 },
    };

    for (unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, vectors[i].want);
        if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
            snprintf(why, n, "start refused for want=%u", (unsigned)vectors[i].want);
            return false;
        }
        if (g_stub_open_alt != vectors[i].alt) {
            snprintf(why, n, "want=%u chose alt %u, expected %u",
                     (unsigned)vectors[i].want, g_stub_open_alt, vectors[i].alt);
            return false;
        }
        if (g_video.isoin_mps != vectors[i].mps) {
            snprintf(why, n, "want=%u mps %u, expected %u",
                     (unsigned)vectors[i].want, g_video.isoin_mps, vectors[i].mps);
            return false;
        }
        if (g_stub_probe_calls != 1) {
            snprintf(why, n, "probe exchange ran %u times", (unsigned)g_stub_probe_calls);
            return false;
        }
        usbh_video_stream_stop();
    }

    /* an ask beyond the ladder has no setting and must be refused */
    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 3061);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) >= 0) {
        snprintf(why, n, "an ask beyond the ladder was accepted");
        usbh_video_stream_stop();
        return false;
    }
    return true;
}

/* A camera that refuses the probe until the streaming interface is taken
 * through one enter-and-leave cycle (the state the board camera enters after
 * a stream): the engine must run the cycle and then succeed. */
static bool case_probe_rearm(char *why, size_t n)
{
    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 640);
    /* the board's state after a stream: closed, but the last open's endpoint
     * payload (640 bytes = alt4) is still recorded - it is what the re-arm
     * cycle enters */
    g_video.isoin_mps = 640;
    g_stub_probe_refusals = 2; /* two refusals: two cycles */
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start refused although the cycle cures the device");
        return false;
    }
    if (g_stub_probe_calls != 3) {
        snprintf(why, n, "probe ran %u times, expected 3 (two refusals, one answer)",
                 (unsigned)g_stub_probe_calls);
        return false;
    }
    /* each failed attempt runs exactly one cycle: alt=<payload>, then 0 */
    if (g_stub_iface_calls != 4 || g_stub_iface_alts[0] != 4 || g_stub_iface_alts[1] != 0 ||
        g_stub_iface_alts[2] != 4 || g_stub_iface_alts[3] != 0) {
        snprintf(why, n, "re-arm cycles: %d calls, alts %u/%u/%u/%u (expected 4/0/4/0)",
                 g_stub_iface_calls, g_stub_iface_alts[0], g_stub_iface_alts[1],
                 g_stub_iface_alts[2], g_stub_iface_alts[3]);
        return false;
    }
    if (g_stub_open_alt != 4) {
        snprintf(why, n, "open at alt %u, expected 4", g_stub_open_alt);
        return false;
    }
    usbh_video_stream_stop();

    /* no refusals: no cycle, and the device's answer still picks the setting */
    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 640);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start refused with a willing device");
        return false;
    }
    if (g_stub_probe_calls != 1 || g_stub_iface_calls != 0) {
        snprintf(why, n, "a willing probe ran %u times with %d cycles, expected 1/0",
                 (unsigned)g_stub_probe_calls, g_stub_iface_calls);
        return false;
    }
    usbh_video_stream_stop();

    /* a device that never answers: the attempt ladder is bounded */
    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 640);
    g_stub_probe_refusals = 100;
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) >= 0) {
        snprintf(why, n, "a permanently refusing device was accepted");
        usbh_video_stream_stop();
        return false;
    }
    if (g_stub_probe_calls != 4) {
        snprintf(why, n, "the refusal ladder ran %u attempts, expected 4",
                 (unsigned)g_stub_probe_calls);
        return false;
    }
    return true;
}

/* Frames bounded by both channels: fid flips and end-of-frame bits agree. */
static bool case_frame_assembly(char *why, size_t n)
{
    static uint8_t a1[260], a2[260], a3[100];
    static uint8_t b1[300], b2[50];
    uint8_t want_a[620];
    uint8_t want_b[350];
    struct pkt script_a[3], script_b[2];

    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 640);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start failed");
        return false;
    }
    fill_payload(a1, sizeof(a1), 0x11);
    fill_payload(a2, sizeof(a2), 0x22);
    fill_payload(a3, sizeof(a3), 0x33);
    fill_payload(b1, sizeof(b1), 0x44);
    fill_payload(b2, sizeof(b2), 0x55);
    script_a[0] = (struct pkt){ a1, sizeof(a1), 0, 0, 0 };
    script_a[1] = (struct pkt){ a2, sizeof(a2), 0, 0, 0 };
    script_a[2] = (struct pkt){ a3, sizeof(a3), 0, 1, 0 };
    script_b[0] = (struct pkt){ b1, sizeof(b1), 1, 0, 0 };
    script_b[1] = (struct pkt){ b2, sizeof(b2), 1, 1, 0 };
    memcpy(want_a, a1, sizeof(a1));
    memcpy(want_a + 260, a2, sizeof(a2));
    memcpy(want_a + 520, a3, sizeof(a3));
    memcpy(want_b, b1, sizeof(b1));
    memcpy(want_b + 300, b2, sizeof(b2));

    device_complete(script_a, 3);
    device_complete(script_b, 2);
    if (!wait_urbs(0, 2, 1000)) {
        snprintf(why, n, "the pump did not consume both urbs");
        return false;
    }

    if (!expect_frame(want_a, sizeof(want_a), why, n)) {
        return false;
    }
    if (!expect_frame(want_b, sizeof(want_b), why, n)) {
        return false;
    }
    usbh_video_stream_stop();
    return true;
}

/* A camera that never flips the frame id: end-of-frame alone bounds frames. */
static bool case_eof_boundary(char *why, size_t n)
{
    static uint8_t p1[200], p2[150], p3[300];
    uint8_t want_a[350];
    uint8_t want_b[300];
    struct pkt s1[2], s2[1];

    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 640);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start failed");
        return false;
    }
    fill_payload(p1, sizeof(p1), 0x66);
    fill_payload(p2, sizeof(p2), 0x77);
    fill_payload(p3, sizeof(p3), 0x88);
    s1[0] = (struct pkt){ p1, sizeof(p1), 0, 0, 0 };
    s1[1] = (struct pkt){ p2, sizeof(p2), 0, 1, 0 };
    s2[0] = (struct pkt){ p3, sizeof(p3), 0, 1, 0 };
    memcpy(want_a, p1, sizeof(p1));
    memcpy(want_a + 200, p2, sizeof(p2));
    memcpy(want_b, p3, sizeof(p3));

    device_complete(s1, 2);
    device_complete(s2, 1);
    if (!wait_urbs(0, 2, 1000)) {
        snprintf(why, n, "the pump did not consume both urbs");
        return false;
    }

    if (!expect_frame(want_a, sizeof(want_a), why, n)) {
        return false;
    }
    if (!expect_frame(want_b, sizeof(want_b), why, n)) {
        return false;
    }
    usbh_video_stream_stop();
    return true;
}

/* A camera that never sets end-of-frame: the fid flip alone bounds frames. */
static bool case_flip_no_eof(char *why, size_t n)
{
    static uint8_t p1[120], p2[130], p3[140], p4[150];
    uint8_t want_a[250];
    uint8_t want_b[290];
    struct pkt s1[2], s2[2];

    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 640);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start failed");
        return false;
    }
    fill_payload(p1, sizeof(p1), 0x99);
    fill_payload(p2, sizeof(p2), 0xaa);
    fill_payload(p3, sizeof(p3), 0xbb);
    fill_payload(p4, sizeof(p4), 0xcc);
    s1[0] = (struct pkt){ p1, sizeof(p1), 0, 0, 0 };
    s1[1] = (struct pkt){ p2, sizeof(p2), 0, 0, 0 };
    s2[0] = (struct pkt){ p3, sizeof(p3), 1, 0, 0 };
    s2[1] = (struct pkt){ p4, sizeof(p4), 1, 0, 0 };
    memcpy(want_a, p1, sizeof(p1));
    memcpy(want_a + 120, p2, sizeof(p2));
    memcpy(want_b, p3, sizeof(p3));
    memcpy(want_b + 140, p4, sizeof(p4));

    device_complete(s1, 2);
    device_complete(s2, 2);
    /* frame B stays under assembly until a boundary arrives: the first packet
     * of the next frame (fid flipped back) completes it */
    {
        static uint8_t tiny[10];
        struct pkt boundary;

        fill_payload(tiny, sizeof(tiny), 0xdd);
        boundary = (struct pkt){ tiny, sizeof(tiny), 0, 0, 0 };
        device_complete(&boundary, 1);
    }
    if (!wait_urbs(0, 3, 1000)) {
        snprintf(why, n, "the pump did not consume three urbs");
        return false;
    }

    if (!expect_frame(want_a, sizeof(want_a), why, n)) {
        return false;
    }
    if (!expect_frame(want_b, sizeof(want_b), why, n)) {
        return false;
    }
    usbh_video_stream_stop();
    return true;
}

/* The error bit drops the frame being assembled; the next frame is intact. */
static bool case_error_drop(char *why, size_t n)
{
    static uint8_t p1[100], p2[50], p3[80], q1[120];
    struct uvc_stream_stats before, after;
    uint8_t want[120];
    struct pkt s1[3], s2[1];

    case_reset(why, n, TEST_FRAME_BUF, TEST_POOL_MAX, 640);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start failed");
        return false;
    }
    usbh_video_stream_stats(&before);
    fill_payload(p1, sizeof(p1), 0xd1);
    fill_payload(p2, sizeof(p2), 0xd2);
    fill_payload(p3, sizeof(p3), 0xd3);
    fill_payload(q1, sizeof(q1), 0xd4);
    s1[0] = (struct pkt){ p1, sizeof(p1), 0, 0, 0 };
    s1[1] = (struct pkt){ p2, sizeof(p2), 0, 0, 1 }; /* error bit */
    s1[2] = (struct pkt){ p3, sizeof(p3), 0, 1, 0 };
    s2[0] = (struct pkt){ q1, sizeof(q1), 1, 1, 0 };
    memcpy(want, q1, sizeof(q1));

    device_complete(s1, 3);
    device_complete(s2, 1);
    if (!wait_urbs(0, 2, 1000)) {
        snprintf(why, n, "the pump did not consume both urbs");
        return false;
    }

    if (!expect_frame(want, sizeof(want), why, n)) {
        return false;
    }
    usbh_video_stream_stats(&after);
    if (after.dropped != before.dropped + 1) {
        snprintf(why, n, "dropped %u, expected 1 more", (unsigned)(after.dropped - before.dropped));
        return false;
    }
    if (after.err_packets <= before.err_packets) {
        snprintf(why, n, "the error packet was not counted");
        return false;
    }
    usbh_video_stream_stop();
    return true;
}

/* A frame that overflows its buffer is dropped whole; the next one is intact. */
static bool case_overflow_drop(char *why, size_t n)
{
    static uint8_t p1[80], p2[80], q1[60];
    struct uvc_stream_stats before, after;
    uint8_t want[60];
    struct pkt s1[2], s2[1];

    case_reset(why, n, 100 /* frame_bufsize */, TEST_POOL_MAX, 640);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start failed");
        return false;
    }
    usbh_video_stream_stats(&before);
    fill_payload(p1, sizeof(p1), 0xe1);
    fill_payload(p2, sizeof(p2), 0xe2);
    fill_payload(q1, sizeof(q1), 0xe3);
    s1[0] = (struct pkt){ p1, sizeof(p1), 0, 0, 0 };
    s1[1] = (struct pkt){ p2, sizeof(p2), 0, 1, 0 }; /* 160 > 100: overflow */
    s2[0] = (struct pkt){ q1, sizeof(q1), 1, 1, 0 };
    memcpy(want, q1, sizeof(q1));

    device_complete(s1, 2);
    device_complete(s2, 1);
    if (!wait_urbs(0, 2, 1000)) {
        snprintf(why, n, "the pump did not consume both urbs");
        return false;
    }

    if (!expect_frame(want, sizeof(want), why, n)) {
        return false;
    }
    usbh_video_stream_stats(&after);
    if (after.dropped != before.dropped + 1) {
        snprintf(why, n, "overflow not counted as one drop");
        return false;
    }
    usbh_video_stream_stop();
    return true;
}

/* The pool API: a dry pool drops frames instead of stalling, dequeue times
 * out cleanly and a returned frame re-enters the assembly. */
static bool case_pool_drain(char *why, size_t n)
{
    static uint8_t p1[100], p2[100], p3[100];
    static uint8_t want1[100], want3[100];
    struct usbh_videoframe *held = NULL;
    struct uvc_stream_stats before, after;
    struct pkt s[1];

    case_reset(why, n, TEST_FRAME_BUF, 1 /* single frame in the pool */, 640);
    if (usbh_video_stream_start(640, 480, USBH_VIDEO_FORMAT_MJPEG) != 0) {
        snprintf(why, n, "start failed");
        return false;
    }
    fill_payload(p1, sizeof(p1), 0xf1);
    fill_payload(p2, sizeof(p2), 0xf2);
    fill_payload(p3, sizeof(p3), 0xf3);
    memcpy(want1, p1, sizeof(p1));
    memcpy(want3, p3, sizeof(p3));

    s[0] = (struct pkt){ p1, sizeof(p1), 0, 1, 0 };
    device_complete(s, 1);
    if (usbh_video_stream_dequeue(&held, 1000) != 0) {
        snprintf(why, n, "first frame not delivered");
        return false;
    }
    if ((held->frame_size != sizeof(want1)) || (memcmp(held->frame_buf, want1, sizeof(want1)) != 0)) {
        snprintf(why, n, "the held frame's content differs");
        return false;
    }
    if (usbh_video_stream_dequeue(&held, 50) != -USB_ERR_TIMEOUT) {
        snprintf(why, n, "an empty ready queue did not time out");
        return false;
    }
    /* the application holds the only frame: a new frame has no buffer */
    usbh_video_stream_stats(&before);
    s[0] = (struct pkt){ p2, sizeof(p2), 1, 1, 0 };
    device_complete(s, 1);
    if (!wait_urbs(before.urbs, 1, 1000)) {
        snprintf(why, n, "the pump did not consume the urb");
        return false;
    }
    usbh_video_stream_stats(&after);
    if (after.dropped != before.dropped + 1) {
        snprintf(why, n, "a dry pool did not drop the frame");
        return false;
    }
    /* returning the frame lets the next one through */
    usbh_video_stream_enqueue(held);
    s[0] = (struct pkt){ p3, sizeof(p3), 0, 1, 0 };
    device_complete(s, 1);
    if (!expect_frame(want3, sizeof(want3), why, n)) {
        return false;
    }
    usbh_video_stream_stop();
    return true;
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*fn)(char *, size_t);
    } cases[] = {
        { "alt-select", case_alt_select },
        { "probe-rearm", case_probe_rearm },
        { "frame-assembly", case_frame_assembly },
        { "eof-boundary", case_eof_boundary },
        { "flip-no-eof", case_flip_no_eof },
        { "error-drop", case_error_drop },
        { "overflow-drop", case_overflow_drop },
        { "pool-drain", case_pool_drain },
    };
    int failed = 0;

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char why[256] = "";

        if (cases[i].fn(why, sizeof(why))) {
            printf("UVCSTREAM: %s PASS\n", cases[i].name);
        } else {
            printf("UVCSTREAM: %s FAIL (%s)\n", cases[i].name, why);
            failed++;
        }
    }
    printf("UVCSTREAM: summary cases=%u failed=%d\n",
           (unsigned)(sizeof(cases) / sizeof(cases[0])), failed);
    return failed == 0 ? 0 : 1;
}
