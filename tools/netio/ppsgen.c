/* ppsgen.c - sequenced UDP packet-rate sender and counting receiver.
 *
 * Why this exists: a userspace UDP generator must exceed the device under
 * test's zero-loss rate, and its own send count must be reconcilable with the
 * device's MAC counters.  ppsgen does both: target-scheduled pacing on two
 * busy threads (no sleeping pacer), a payload sequence, and an optional TCP
 * control channel so the device-side counter window and the sender start
 * together.
 *
 * Sender:
 *   ppsgen -d <dst-ip> -p <port> [-s <bytes>] [-r <pps> | -u] [-t <secs>]
 *          [-B <batch>] [-T <threads>] [-k <tick_us>] [-c <ctl-port>]
 *          [-a <cpu>]
 *
 *   -r 0 / -u : unlimited (tight sendmmsg loop)
 *   -k 0      : busy pacing, never sleep
 *   -k        : pacer tick in microseconds
 *   -c        : TCP control port; ppsgen sends "READY" and waits for "GO"
 *
 * Receiver (counting sink for sender self-tests):
 *   ppsgen -L -p <port> [-i <idle-secs>]
 *
 * Payload layout (size >= 12):
 *   bytes 0..3   "PPSG"
 *   bytes 4..11  uint64 sequence, big endian
 *   bytes 12..   0xa5 fill
 *
 * Output:
 *   PPSGEN t=1 sent=... pps=...
 *   PPSGEN END sent=... secs=... pps=... req=... size=... batch=...
 *              threads=... tick_us=... ctl=...
 *   PPSGEN RECV n=... bytes=... secs=... pps=...
 *
 * Build: gcc -O2 -pthread -o ppsgen ppsgen.c
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_BATCH 4096
#define MAX_THREADS 32

static char g_dst[64] = "";
static int g_port = 9999;
static int g_size = 64;
static double g_pps = 0.0; /* 0 = unlimited */
static double g_secs = 10.0;
static int g_batch = 64;
static int g_threads = 1;
static int g_tick_us = 100;
static int g_ctl_port = 0;
static int g_cpu = -1;
static int g_listen = 0;
static double g_idle = 2.0;

static volatile int g_stop = 0;
static volatile int g_start = 0;
static long long g_sent = 0;
static double g_t0 = 0.0;
static unsigned long long g_sleeps, g_loops;
static unsigned long long g_seq = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

struct arg {
	int id;
};

static double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sleep_s(double sec)
{
	struct timespec ts;

	if (sec <= 0.0) {
		return;
	}
	ts.tv_sec = (time_t)sec;
	ts.tv_nsec = (long)((sec - (double)ts.tv_sec) * 1e9);
	nanosleep(&ts, NULL);
}

static void pin_cpu(int cpu)
{
	cpu_set_t set;

	if (cpu < 0) {
		return;
	}
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		/* Not fatal: the pps measurement does not depend on the pin. */
		fprintf(stderr, "ppsgen: sched_setaffinity(%d): %s\n", cpu, strerror(errno));
	}
}

static int make_sock(struct sockaddr_in *sa)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	int sndbuf = 4 * 1024 * 1024;

	if (fd < 0) {
		perror("socket");
		return -1;
	}
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
	memset(sa, 0, sizeof(*sa));
	sa->sin_family = AF_INET;
	sa->sin_port = htons((uint16_t)g_port);
	if (inet_pton(AF_INET, g_dst, &sa->sin_addr) != 1) {
		fprintf(stderr, "ppsgen: bad dst %s\n", g_dst);
		close(fd);
		return -1;
	}
	return fd;
}

static void fill_payload(char *buf, int batch, unsigned long long base)
{
	int i;

	for (i = 0; i < batch; i++) {
		char *p = buf + (size_t)i * g_size;
		uint64_t seq = htobe64(base + (unsigned long long)i);

		memcpy(p, "PPSG", 4);
		memcpy(p + 4, &seq, 8);
	}
}

static void *sender(void *unused)
{
	struct arg *a = unused;
	struct sockaddr_in sa;
	struct mmsghdr *mmsg;
	struct iovec *iov;
	char *buf;
	long long sent = 0;
	int fd, batch, i;
	double rate_share = g_pps > 0.0 ? g_pps / (double)g_threads : 0.0;
	double tick = (double)g_tick_us / 1e6;

	(void)a;
	fd = make_sock(&sa);
	if (fd < 0) {
		return NULL;
	}
	/* A blocking sendmmsg on a 256-packet batch parks in the qdisc and the
	 * sender then never catches up (observed as a stable ~115k pps). Keep the
	 * socket non-blocking so a full qdisc drops the batch attempt and the
	 * pacer retries on the next tick. */
	(void)fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	batch = g_batch;
	fprintf(stderr, "PPSGEN SENDER rate_share=%.0f tick_us=%d batch=%d\n",
		rate_share, g_tick_us, batch);
	if (batch > MAX_BATCH) {
		batch = MAX_BATCH;
	}
	if (batch < 1) {
		batch = 1;
	}
	buf = malloc((size_t)batch * (size_t)g_size);
	mmsg = calloc((size_t)batch, sizeof(*mmsg));
	iov = calloc((size_t)batch, sizeof(*iov));
	if (!buf || !mmsg || !iov) {
		fprintf(stderr, "ppsgen: out of memory\n");
		free(buf);
		free(mmsg);
		free(iov);
		close(fd);
		return NULL;
	}
	memset(buf, 0xa5, (size_t)batch * (size_t)g_size);
	for (i = 0; i < batch; i++) {
		iov[i].iov_base = buf + (size_t)i * g_size;
		iov[i].iov_len = (size_t)g_size;
		mmsg[i].msg_hdr.msg_iov = &iov[i];
		mmsg[i].msg_hdr.msg_iovlen = 1;
		mmsg[i].msg_hdr.msg_name = &sa;
		mmsg[i].msg_hdr.msg_namelen = sizeof(sa);
	}
	if (g_cpu >= 0) {
		pin_cpu(g_cpu + (a ? a->id : 0));
	}
	while (!g_start && !g_stop) {
		sleep_s(0.0002);
	}
	while (!g_stop) {
		unsigned long long base;
		int n = 0;
		int rc;

		if (rate_share <= 0.0) {
			n = batch;
		} else {
			/* Target schedule: packets due since GO, not a token
			 * bucket. A token bucket with a small cap silently
			 * under-runs whenever a wake oversleeps; this form
			 * always catches up and only pays the wake granularity
			 * (tick), which is exactly the arm being measured. */
			double el = now_s() - g_t0;
			unsigned long long due = (unsigned long long)(rate_share * el);

			if (due > (unsigned long long)sent) {
				unsigned long long gap = due - (unsigned long long)sent;

				n = gap > (unsigned long long)batch ? batch : (int)gap;
			}
			if (n == 0) {
				/* -k 0 = busy pacing: never sleep, so the stream is
				 * CPU-paced and smooth at any rate. A sleeping pacer
				 * emits tick-sized bursts; those change the receiver's
				 * IRQ pattern and the loss at the same mean pps. */
				if (g_tick_us == 0) {
					continue;
				}
				g_sleeps++;
				sleep_s(tick);
				continue;
			}
		}
		g_loops++;
		if ((g_loops & 0x3ff) == 0) {
			double el = now_s() - g_t0;
			unsigned long long due = rate_share > 0.0 ? (unsigned long long)(rate_share * el) : 0;
			fprintf(stderr, "PPSGEN TRACE el=%.3f due=%llu sent=%lld n=%d sleeps=%llu loops=%llu\n",
				el, due, sent, n, g_sleeps, g_loops);
		}
		base = __sync_fetch_and_add(&g_seq, (unsigned long long)n);
		fill_payload(buf, n, base);
		rc = sendmmsg(fd, mmsg, (unsigned int)n, 0);
		if (rc < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == ENOBUFS || errno == EAGAIN) {
				sleep_s(0.00005);
				continue;
			}
			perror("sendmmsg");
			break;
		}
		sent += rc;
		__sync_fetch_and_add(&g_sent, rc);
	}
	free(buf);
	free(mmsg);
	free(iov);
	close(fd);
	return NULL;
}

static int ctl_wait_go(void)
{
	struct sockaddr_in sa;
	struct pollfd pfd;
	int lfd, cfd, one = 1;
	char line[64];
	ssize_t n;
	size_t off = 0;

	lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		perror("ctl socket");
		return -1;
	}
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons((uint16_t)g_ctl_port);
	if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		perror("ctl bind");
		close(lfd);
		return -1;
	}
	if (listen(lfd, 1) != 0) {
		perror("ctl listen");
		close(lfd);
		return -1;
	}
	printf("PPSGEN CTL port=%d waiting=30s\n", g_ctl_port);
	fflush(stdout);
	pfd.fd = lfd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 30000) <= 0) {
		fprintf(stderr, "ppsgen: no control connection\n");
		close(lfd);
		return -1;
	}
	cfd = accept(lfd, NULL, NULL);
	close(lfd);
	if (cfd < 0) {
		perror("ctl accept");
		return -1;
	}
	if (write(cfd, "READY\n", 6) != 6) {
		perror("ctl write");
		close(cfd);
		return -1;
	}
	while (off < sizeof(line) - 1) {
		n = read(cfd, line + off, 1);
		if (n <= 0) {
			break;
		}
		if (line[off] == '\n') {
			break;
		}
		off++;
	}
	line[off] = 0;
	close(cfd);
	if (strncmp(line, "GO", 2) != 0) {
		fprintf(stderr, "ppsgen: control got '%s', expected GO\n", line);
		return -1;
	}
	return 0;
}

static int run_receiver(void)
{
#define RX_BATCH 64
#define RX_BUFSZ 2048
	struct sockaddr_in sa;
	struct sockaddr_in src[RX_BATCH];
	struct mmsghdr mm[RX_BATCH];
	struct iovec iov[RX_BATCH];
	char *rbuf;
	long long n = 0, nbytes = 0;
	double t0, last;
	int fd, one = 1, rcvbuf = 8 * 1024 * 1024;
	struct pollfd pfd;
	int i;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons((uint16_t)g_port);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		perror("bind");
		close(fd);
		return 1;
	}
	rbuf = malloc((size_t)RX_BATCH * RX_BUFSZ);
	if (!rbuf) {
		close(fd);
		return 1;
	}
	memset(mm, 0, sizeof(mm));
	for (i = 0; i < RX_BATCH; i++) {
		iov[i].iov_base = rbuf + (size_t)i * RX_BUFSZ;
		iov[i].iov_len = RX_BUFSZ;
		mm[i].msg_hdr.msg_iov = &iov[i];
		mm[i].msg_hdr.msg_iovlen = 1;
		mm[i].msg_hdr.msg_name = &src[i];
		mm[i].msg_hdr.msg_namelen = sizeof(src[i]);
	}
	printf("PPSGEN RECV listening port=%d idle=%.1fs\n", g_port, g_idle);
	fflush(stdout);
	t0 = now_s();
	last = t0;
	pfd.fd = fd;
	pfd.events = POLLIN;
	while (!g_stop) {
		int pr = poll(&pfd, 1, 200);

		if (pr > 0) {
			for (;;) {
				int r = recvmmsg(fd, mm, RX_BATCH, MSG_DONTWAIT, NULL);

				if (r <= 0) {
					break;
				}
				for (i = 0; i < r; i++) {
					nbytes += mm[i].msg_len;
				}
				n += r;
				last = now_s();
				if (r < RX_BATCH) {
					break;
				}
			}
		}
		if (now_s() - last > g_idle) {
			break;
		}
	}
	{
		double el = now_s() - t0;

		printf("PPSGEN RECV n=%lld bytes=%lld secs=%.3f pps=%.0f\n",
		       n, nbytes, el, el > 0.0 ? (double)n / el : 0.0);
	}
	free(rbuf);
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int opt;
	pthread_t th[MAX_THREADS];
	struct arg args[MAX_THREADS];
	double t0, last;

	while ((opt = getopt(argc, argv, "d:p:s:r:t:B:T:k:c:a:Li:uh")) != -1) {
		switch (opt) {
		case 'd': snprintf(g_dst, sizeof(g_dst), "%s", optarg); break;
		case 'p': g_port = atoi(optarg); break;
		case 's': g_size = atoi(optarg); break;
		case 'r': g_pps = atof(optarg); break;
		case 't': g_secs = atof(optarg); break;
		case 'B': g_batch = atoi(optarg); break;
		case 'T': g_threads = atoi(optarg); break;
		case 'k': g_tick_us = atoi(optarg); break;
		case 'c': g_ctl_port = atoi(optarg); break;
		case 'a': g_cpu = atoi(optarg); break;
		case 'L': g_listen = 1; break;
		case 'i': g_idle = atof(optarg); break;
		case 'u': g_pps = 0.0; break;
		default:
			fprintf(stderr,
				"usage: %s -d dst -p port [-s 64] [-r pps|-u] [-t secs]"
				" [-B batch] [-T threads] [-k tick_us] [-c ctl_port] [-a cpu]\n"
				"       %s -L -p port [-i idle_secs]\n",
				argv[0], argv[0]);
			return 2;
		}
	}
	if (g_listen) {
		return run_receiver();
	}
	if (g_dst[0] == 0) {
		fprintf(stderr, "ppsgen: need -d dst (or -L for receiver)\n");
		return 2;
	}
	if (g_size < 12 || g_size > 1472) {
		fprintf(stderr, "ppsgen: -s must be 12..1472\n");
		return 2;
	}
	if (g_batch > MAX_BATCH) {
		g_batch = MAX_BATCH;
	}
	if (g_threads > MAX_THREADS) {
		g_threads = MAX_THREADS;
	}
	if (g_threads < 1) {
		g_threads = 1;
	}
	if (g_ctl_port > 0) {
		if (ctl_wait_go() != 0) {
			return 1;
		}
	}
	/* Warm up the ARP entry before the measured window opens. The first
	 * send on a cold ARP entry is a userspace stall, not DUT capacity. */
	{
		struct sockaddr_in sa;
		int fd = make_sock(&sa);
		char warm[64] = {0};

		if (fd >= 0) {
			if (g_size <= (int)sizeof(warm)) {
				memset(warm, 0xa5, (size_t)g_size);
				memcpy(warm, "PPSG", 4);
				(void)sendto(fd, warm, (size_t)g_size, 0, (struct sockaddr *)&sa, sizeof(sa));
			}
			close(fd);
		}
	}
	g_t0 = now_s();
	t0 = g_t0;
	g_start = 1;
	for (int i = 0; i < g_threads; i++) {
		args[i].id = i;
		if (pthread_create(&th[i], NULL, sender, &args[i]) != 0) {
			fprintf(stderr, "ppsgen: pthread_create failed\n");
			return 1;
		}
	}
	last = 0.0;
	{
		long long prev = 0;

		while (now_s() - t0 < g_secs) {
			long long s;

			sleep_s(0.05);
			pthread_mutex_lock(&g_lock);
			s = g_sent;
			pthread_mutex_unlock(&g_lock);
			if (now_s() - t0 - last >= 1.0) {
				last = now_s() - t0;
				printf("PPSGEN t=%.0f sent=%lld pps=%lld\n", last, s, s - prev);
				fflush(stdout);
				prev = s;
			}
		}
	}
	g_stop = 1;
	for (int i = 0; i < g_threads; i++) {
		pthread_join(th[i], NULL);
	}
	{
		double el = now_s() - t0;

		printf("PPSGEN END sent=%lld secs=%.3f pps=%.0f req=%.0f loops=%llu sleeps=%llu size=%d batch=%d threads=%d tick_us=%d ctl=%d\n",
		       g_sent, el, el > 0.0 ? (double)g_sent / el : 0.0, g_pps,
		       g_loops, g_sleeps,
		       g_size, g_batch, g_threads, g_tick_us, g_ctl_port);
	}
	return 0;
}
