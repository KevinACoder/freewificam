/**
 * @file
 * @brief Verify packet trimming used by CCMP decapsulation.
 * @author zhugengyu
 * @date 09.09.2026
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mbuf.h>

int kprintf(const char *fmt, ...) {
	va_list ap;
	int ret;
	va_start(ap, fmt);
	ret = vfprintf(stderr, fmt, ap);
	va_end(ap);
	return ret;
}

static struct mbuf *packet(int len) {
	struct mbuf *m = m_gethdr(M_NOWAIT, MT_DATA);
	assert(m != NULL);
	m->m_len = m->m_pkthdr.len = len;
	for (int i = 0; i < len; i++) {
		m->m_data[i] = i;
	}
	return m;
}

int main(void) {
	struct mbuf *m = packet(100);
	char *head = m->m_data;
	m_adj(m, -8);
	assert(m->m_len == 92 && m->m_pkthdr.len == 92);
	assert(m->m_data == head && m->m_data[0] == 0 && m->m_data[91] == 91);
	m_adj(m, 8);
	assert(m->m_len == 84 && m->m_pkthdr.len == 84 && m->m_data[0] == 8);
	m_adj(m, -100);
	assert(m->m_len == 0 && m->m_pkthdr.len == 0);
	m_freem(m);

	m = packet(10);
	m->m_next = packet(10);
	m->m_next->m_flags &= ~M_PKTHDR;
	m->m_pkthdr.len = 20;
	m_adj(m, -15);
	assert(m->m_len == 5 && m->m_next->m_len == 0 && m->m_pkthdr.len == 5);
	assert(m->m_data[0] == 0 && m->m_data[4] == 4);
	m_adj(m, 100);
	assert(m->m_len == 0 && m->m_next->m_len == 0 && m->m_pkthdr.len == 0);
	m_freem(m);
	puts("mbuf trim: PASS");
	m = packet(20);
	m->m_pkthdr.userdata = (void *)0x1234;
	M_PREPEND(m, 24, M_DONTWAIT);
	assert(m != NULL && m->m_len >= 24 && m->m_pkthdr.len == 44);
	assert(m->m_pkthdr.userdata == (void *)0x1234);
	memset(m->m_data, 0xa5, 24);
	char bytes[44];
	m_copydata(m, 0, sizeof(bytes), bytes);
	for (int i = 0; i < 24; i++)
		assert((unsigned char)bytes[i] == 0xa5);
	for (int i = 0; i < 20; i++)
		assert(bytes[24 + i] == i);
	m_freem(m);
	puts("mbuf prepend: PASS");
	m = packet(10);
	m->m_next = packet(20);
	m->m_next->m_flags &= ~M_PKTHDR;
	m->m_next->m_next = packet(30);
	m->m_next->m_next->m_flags &= ~M_PKTHDR;
	m->m_pkthdr.len = 60;
	m->m_pkthdr.userdata = (void *)0x1234;
	char before[60], after[60];
	m_copydata(m, 0, sizeof(before), before);
	m = m_pullup(m, 24);
	assert(m && m->m_len >= 24 && m->m_pkthdr.len == 60);
	assert(m->m_pkthdr.userdata == (void *)0x1234);
	m_copydata(m, 0, sizeof(after), after);
	assert(memcmp(before, after, sizeof(before)) == 0);
	m_freem(m);
	puts("mbuf pullup: PASS");
	return 0;
}
