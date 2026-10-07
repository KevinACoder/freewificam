/**
 * @file
 * @brief Check RFC 3610 vectors and authenticated mbuf decryption.
 * @author zhugengyu
 * @date 09.09.2026
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mbuf.h>

#include <crypto/aes/aes.h>
#include <crypto/aes/aes_ccm.h>
#include <crypto/aes/aes_ccm_mbuf.h>

int kprintf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int n = vfprintf(stderr, fmt, ap);
	va_end(ap);
	return n;
}

/* RFC 3610 packet vector 1; public test material, not a network key. */
static const uint8_t ciphertext[] = {0x58, 0x8c, 0x97, 0x9a, 0x61, 0xc6, 0x63,
    0xd2, 0xf0, 0x66, 0xd0, 0xc2, 0xc0, 0xf9, 0x89, 0x80, 0x6d, 0x5f, 0x6b,
    0x61, 0xda, 0xc3, 0x84};
static const uint8_t tag[] = {0x17, 0xe8, 0xd1, 0x2c, 0xfd, 0xf9, 0x26, 0xe0};
static const uint8_t nonce[] = {0, 0, 0, 3, 2, 1, 0, 0xa0, 0xa1, 0xa2, 0xa3,
    0xa4, 0xa5};

static void check_mbuf(int split, int corrupt) {
	uint8_t key[16], aad[8], bytes[31], result[31], mic[8];
	struct aesenc enc;
	struct aes_ccm ccm;
	struct mbuf *m = m_gethdr(M_NOWAIT, MT_DATA);
	assert(m);
	for (unsigned i = 0; i < sizeof(key); i++)
		key[i] = 0xc0 + i;
	for (unsigned i = 0; i < sizeof(aad); i++)
		aad[i] = i;
	memcpy(bytes, aad, sizeof(aad));
	memcpy(bytes + sizeof(aad), ciphertext, sizeof(ciphertext));
	memcpy(mic, tag, sizeof(mic));
	if (corrupt)
		mic[0] ^= 0x80;
	m->m_len = split;
	m->m_pkthdr.len = sizeof(bytes);
	memcpy(m->m_data, bytes, split);
	if (split < (int)sizeof(bytes)) {
		m->m_next = m_get(M_NOWAIT, MT_DATA);
		assert(m->m_next);
		m->m_next->m_len = sizeof(bytes) - split;
		memcpy(m->m_next->m_data, bytes + split, sizeof(bytes) - split);
	}
	aes_setenckey128(&enc, key);
	aes_ccm_init(&ccm, AES_128_NROUNDS, &enc, 2, 8, nonce, 13, aad, 8, 23);
	assert(aes_ccm_dec_mbuf(&ccm, m, 8, 23, mic) == !corrupt);
	m_copydata(m, 0, sizeof(result), result);
	assert(memcmp(result, aad, sizeof(aad)) == 0);
	for (unsigned i = 8; i < sizeof(result); i++) {
		assert(result[i] == (corrupt ? 0 : i));
	}
	assert(m->m_pkthdr.len == sizeof(bytes));
	m_freem(m);
}

int main(void) {
	assert(aes_ccm_selftest() == 0);
	for (int split = 1; split <= 31; split++) {
		check_mbuf(split, 0);
		check_mbuf(split, 1);
	}
	puts("RFC3610 vectors and mbuf MIC rejection: PASS");
	return 0;
}
